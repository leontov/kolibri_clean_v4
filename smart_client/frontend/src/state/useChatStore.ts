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

const KNOWN_APP_ROUTES = new Set(["chat", "ledger", "settings"]);

let resolvedApiBase: string | null = null;
let resolvingApiBase: Promise<string> | null = null;

function normalizeBase(value: string): string {
  const trimmed = value.trim().replace(/\/+$/, "");
  if (!trimmed) {
    return "";
  }
  if (/^(?:[a-z]+:)?\/\//i.test(trimmed)) {
    return trimmed;
  }
  return trimmed.startsWith("/") ? trimmed : `/${trimmed}`;
}

function normalizePath(value: string): string {
  const trimmed = value.trim().replace(/\/+$/, "");
  if (!trimmed) {
    return "";
  }
  if (/^(?:[a-z]+:)?\/\//i.test(trimmed)) {
    return trimmed;
  }
  return trimmed.startsWith("/") ? trimmed : `/${trimmed}`;
}

function joinBaseAndPath(base: string, path: string): string {
  if (!base) {
    return path;
  }
  if (!path) {
    return base;
  }
  if (/^(?:[a-z]+:)?\/\//i.test(path)) {
    return path;
  }
  const sanitizedBase = base.replace(/\/+$/, "");
  const sanitizedPath = path.replace(/^\/+/, "");
  return `${sanitizedBase}/${sanitizedPath}`;
}

async function ensureApiBase(): Promise<string> {
  if (resolvedApiBase !== null) {
    return resolvedApiBase;
  }
  if (!resolvingApiBase) {
    resolvingApiBase = resolveApiBase();
  }
  resolvedApiBase = await resolvingApiBase;
  return resolvedApiBase;
}

function inferBaseFromLocation(): string {
  if (typeof window === "undefined") {
    return "";
  }

  const trimmedPath = window.location.pathname.replace(/\/+$/, "");
  if (!trimmedPath || trimmedPath === "/") {
    return "";
  }

  const segments = trimmedPath.split("/").filter(Boolean);
  if (segments.length === 0) {
    return "";
  }

  const lastSegment = segments[segments.length - 1];
  if (lastSegment.includes(".") || KNOWN_APP_ROUTES.has(lastSegment)) {
    segments.pop();
  }

  if (segments.length === 0) {
    return "";
  }

  return `/${segments.join("/")}`;
}

async function resolveApiBase(): Promise<string> {
  const candidates = resolveApiBaseCandidates();
  for (const base of candidates) {
    try {
      const response = await fetch(`${base}/status`, { method: "GET" });
      if (response.ok) {
        return base;
      }
    } catch (error) {
      console.warn(`Kolibri API base candidate failed: ${base}`, error);
    }
  }
  return candidates[candidates.length - 1] ?? "";
}

function resolveApiBaseCandidates(): string[] {
  const candidates: string[] = [];

  const addCandidate = (value: string) => {
    if (!value || candidates.includes(value)) {
      return;
    }
    candidates.push(value);
  };

  const rawBaseCandidates = resolveRawBaseCandidates();
  const apiPathCandidates = resolveApiPathCandidates();

  for (const rawBase of rawBaseCandidates) {
    addCandidate(rawBase);
    for (const apiPath of apiPathCandidates) {
      addCandidate(joinBaseAndPath(rawBase, apiPath));
    }
  }

  addCandidate("");

  return candidates;
}

function resolveRawBaseCandidates(): string[] {
  const rawBases: string[] = [];

  const addBase = (value: string) => {
    if (!value || rawBases.includes(value)) {
      return;
    }
    rawBases.push(value);
  };

  addBase(normalizeBase(import.meta.env.VITE_API_BASE ?? ""));

  if (typeof window !== "undefined") {
    const globalWithApiBase = window as Window & {
      __KOLIBRI_API_BASE__?: string;
      __kolibriApiBase?: string;
    };
    addBase(
      normalizeBase(
        globalWithApiBase.__KOLIBRI_API_BASE__ ?? globalWithApiBase.__kolibriApiBase ?? ""
      )
    );

    const meta = document
      .querySelector('meta[name="kolibri-api-base"]')
      ?.getAttribute("content");
    addBase(normalizeBase(meta ?? ""));

    addBase(normalizeBase(import.meta.env.BASE_URL ?? ""));
    addBase(inferBaseFromLocation());
  }

  addBase("");

  return rawBases;
}

function resolveApiPathCandidates(): string[] {
  const apiPaths: string[] = [];

  const addPath = (value: string) => {
    if (apiPaths.includes(value)) {
      return;
    }
    apiPaths.push(value);
  };

  addPath(normalizePath(import.meta.env.VITE_API_PREFIX ?? ""));

  if (typeof window !== "undefined") {
    const globalWithPrefix = window as Window & {
      __KOLIBRI_API_PREFIX__?: string;
      __kolibriApiPrefix?: string;
    };
    addPath(
      normalizePath(
        globalWithPrefix.__KOLIBRI_API_PREFIX__ ?? globalWithPrefix.__kolibriApiPrefix ?? ""
      )
    );

    const meta = document
      .querySelector('meta[name="kolibri-api-prefix"]')
      ?.getAttribute("content");
    addPath(normalizePath(meta ?? ""));
  }

  addPath("/api/v1");
  addPath("/v1");
  addPath("/api");
  addPath("");

  return apiPaths;
}

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

        void ensureApiBase()
          .then(apiBase => {
            const chatStream = new EventSource(
              `${apiBase}/chat/stream?session_id=${sessionId}`
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

            const chainStream = new EventSource(`${apiBase}/chain/stream`);
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
          })
          .catch(error => {
            console.error("Kolibri chat stream connection failed", error);
            set({ connected: false, chatStream: null, chainStream: null });
          });
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
          const apiBase = await ensureApiBase();
          await fetch(`${apiBase}/chat`, {
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
          console.error("Kolibri chat request failed", error);
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
