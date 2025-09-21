import { create } from "zustand";
import { persist } from "zustand/middleware";

export type Role = "user" | "assistant" | "tool";

export interface SourceItem {
  source: string;
  text?: string;
  score?: number;
}

export interface ChatMessage {
  id: string;
  role: Role;
  content: string;
  pending?: boolean;
  sources?: SourceItem[];
  toolName?: string;
}

export interface ChainBlock {
  id: string;
  timestamp: string;
  payload: Record<string, unknown>;
}

interface ChatState {
  sessionId: string;
  messages: ChatMessage[];
  chain: ChainBlock[];
  activeView: "chat" | "ledger" | "settings";
  connected: boolean;
  chatStream: EventSource | null;
  chainStream: EventSource | null;
  preferences: {
    ttsRate: number;
  };
  connect: () => void;
  sendMessage: (content: string) => Promise<void>;
  setView: (view: ChatState["activeView"]) => void;
  pushChainBlock: (block: ChainBlock) => void;
  setPreference: <K extends keyof ChatState["preferences"]>(key: K, value: ChatState["preferences"][K]) => void;
}

const randomId = () => {
  if (typeof crypto !== "undefined" && "randomUUID" in crypto) {
    return crypto.randomUUID();
  }
  return Math.random().toString(36).slice(2);
};

const API_BASE = "";

export const useChatStore = create<ChatState>()(
  persist(
    (set, get) => ({
      sessionId: randomId(),
      messages: [],
      chain: [],
      activeView: "chat",
      connected: false,
      chatStream: null,
      chainStream: null,
      preferences: {
        ttsRate: 1
      },
      connect: () => {
        const { sessionId, connected, chatStream: existingChatStream, chainStream: existingChainStream } = get();
        if (connected && existingChatStream && existingChatStream.readyState !== EventSource.CLOSED) {
          return;
        }
        existingChatStream?.close();
        existingChainStream?.close();

        const chatStream = new EventSource(
          `${API_BASE}/api/v1/chat/stream?session_id=${sessionId}`
        );
        chatStream.onmessage = () => {};
        let currentAssistantId: string | null = null;
        chatStream.addEventListener("token", event => {
          const data = JSON.parse((event as MessageEvent).data) as { content: string };
          set(state => {
            const messages = [...state.messages];
            if (!currentAssistantId) {
              currentAssistantId = randomId();
              messages.push({
                id: currentAssistantId,
                role: "assistant",
                content: "",
                pending: true
              });
            }
            const idx = messages.findIndex(msg => msg.id === currentAssistantId);
            if (idx >= 0) {
              messages[idx] = {
                ...messages[idx],
                content: `${messages[idx].content}${data.content}`
              };
            }
            return { messages };
          });
        });
        chatStream.addEventListener("tool_call", event => {
          const data = JSON.parse((event as MessageEvent).data);
          set(state => ({
            messages: [
              ...state.messages,
              {
                id: randomId(),
                role: "tool",
                content: JSON.stringify(data, null, 2),
                toolName: data.name
              }
            ]
          }));
        });
        chatStream.addEventListener("final", event => {
          const data = JSON.parse((event as MessageEvent).data) as { content: string };
          set(state => {
            const messages = [...state.messages];
            if (currentAssistantId) {
              const idx = messages.findIndex(msg => msg.id === currentAssistantId);
              if (idx >= 0) {
                messages[idx] = {
                  ...messages[idx],
                  content: data.content,
                  pending: false,
                  sources: extractSources(data.content)
                };
              }
            } else {
              messages.push({
                id: randomId(),
                role: "assistant",
                content: data.content,
                pending: false,
                sources: extractSources(data.content)
              });
            }
            currentAssistantId = null;
            return { messages };
          });
        });

        const chainStream = new EventSource(`${API_BASE}/api/v1/chain/stream`);
        chainStream.addEventListener("block", event => {
          const data = JSON.parse((event as MessageEvent).data);
          set(state => ({
            chain: [
              {
                id: data.id,
                timestamp: data.timestamp,
                payload: data.payload
              },
              ...state.chain
            ].slice(0, 50)
          }));
        });

        set({ connected: true, chatStream, chainStream });
      },
      sendMessage: async content => {
        const { sessionId } = get();
        const message: ChatMessage = {
          id: randomId(),
          role: "user",
          content,
          pending: false
        };
        set(state => ({ messages: [...state.messages, message] }));
        try {
          await fetch(`${API_BASE}/api/v1/chat`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ session_id: sessionId, message: content })
          });
        } catch (error) {
          set(state => ({
            messages: state.messages.map(msg =>
              msg.id === message.id ? { ...msg, pending: true } : msg
            )
          }));
        }
      },
      setView: view => set({ activeView: view }),
      pushChainBlock: block =>
        set(state => ({ chain: [block, ...state.chain].slice(0, 50) })),
      setPreference: (key, value) =>
        set(state => ({ preferences: { ...state.preferences, [key]: value } }))
    }),
    {
      name: "kolibri-chat-state",
      partialize: state => ({
        sessionId: state.sessionId,
        messages: state.messages,
        chain: state.chain,
        activeView: state.activeView,
        preferences: state.preferences
      })
    }
  )
);

function extractSources(content: string): SourceItem[] | undefined {
  const index = content.indexOf("Источники:");
  if (index === -1) return undefined;
  const tail = content.slice(index + "Источники:".length).trim();
  if (!tail) return undefined;
  return tail.split("\n").map(line => ({ source: line.replace(/^•\s*/, "").trim() }));
}
