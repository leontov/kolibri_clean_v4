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
  toolPayload?: unknown;
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
  setPreference: <K extends keyof ChatState["preferences"]>(
    key: K,
    value: ChatState["preferences"][K],
  ) => void;
}

const KNOWN_APP_ROUTES = new Set(["chat", "ledger", "settings"]);

let resolvedApiBase: string | null = null;
let resolvingApiBase: Promise<string> | null = null;

const randomId = () => {
  if (typeof crypto !== "undefined" && "randomUUID" in crypto) {
    return crypto.randomUUID();
  }
  return Math.random().toString(36).slice(2);
};

function trimTrailingSlash(value: string): string {
  return value.replace(/\/+$/, "");
}

function normalizeCandidate(value: string | null | undefined): string {
  if (!value) {
    return "";
  }
  const trimmed = value.trim();
  if (!trimmed) {
    return "";
  }
  if (/^(?:[a-z]+:)?\/\//i.test(trimmed)) {
    return trimTrailingSlash(trimmed);
  }
  if (trimmed.startsWith("/")) {
    return trimTrailingSlash(trimmed);
  }
  return `/${trimTrailingSlash(trimmed)}`;
}

function normalizeBase(value: string): string {
  const trimmed = trimTrailingSlash(value.trim());
  if (!trimmed) {
    return "";
  }
  if (/^(?:[a-z]+:)?\/\//i.test(trimmed)) {
    return trimmed;
  }
  return trimmed.startsWith("/") ? trimmed : `/${trimmed}`;
}

function normalizePath(value: string): string {
  const trimmed = trimTrailingSlash(value.trim());
  if (!trimmed) {
    return "";
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

  const trimmedPath = trimTrailingSlash(window.location.pathname);
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
    const normalized = normalizeCandidate(value);
    if (normalized === "" && candidates.includes("")) {
      return;
    }
    if (normalized && candidates.includes(normalized)) {
      return;
    }
    candidates.push(normalized);
  };

  addCandidate(import.meta.env.VITE_API_BASE ?? "");

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
        globalWithApiBase.__KOLIBRI_API_BASE__ ?? globalWithApiBase.__kolibriApiBase ?? "",
      ),
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
        globalWithPrefix.__KOLIBRI_API_PREFIX__ ?? globalWithPrefix.__kolibriApiPrefix ?? "",
      ),
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

function extractSources(content: string): SourceItem[] | undefined {
  const index = content.indexOf("Источники:");
  if (index === -1) return undefined;
  const tail = content.slice(index + "Источники:".length).trim();
  if (!tail) return undefined;
  return tail.split("\n").map(line => ({ source: line.replace(/^•\s*/, "").trim() }));
}

function selectToolPayload(data: Record<string, unknown>): unknown {
  if ("payload" in data) {
    return data.payload;
  }
  if ("result" in data) {
    return data.result;
  }
  if ("parameters" in data) {
    return data.parameters;
  }
  return data;
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
        ttsRate: 1,
      },
      connect: () => {
        const {
          sessionId,
          connected,
          chatStream: existingChatStream,
          chainStream: existingChainStream,
        } = get();

        if (
          connected &&
          existingChatStream &&
          existingChatStream.readyState !== EventSource.CLOSED
        ) {
          return;
        }

        existingChatStream?.close();
        existingChainStream?.close();

        void ensureApiBase()
          .then(apiBase => {
            const chatStream = new EventSource(
              `${apiBase}/chat/stream?session_id=${sessionId}`,
            );
            let currentAssistantId: string | null = null;

            chatStream.onopen = () => {
              set({ connected: true });
            };

            chatStream.onerror = event => {
              console.error("Kolibri chat stream error", event);
              chatStream.close();
              set({ connected: false, chatStream: null, chainStream: null });
            };

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
                    pending: true,
                  });
                }

                const index = messages.findIndex(message => message.id === currentAssistantId);
                if (index >= 0) {
                  const current = messages[index];
                  messages[index] = {
                    ...current,
                    content: `${current.content}${data.content}`,
                  };
                }

                return { messages };
              });
            });

            chatStream.addEventListener("tool_call", event => {
              const raw = JSON.parse((event as MessageEvent).data) as Record<string, unknown> & {
                name?: string;
              };
              const payload = selectToolPayload(raw);
              set(state => ({
                messages: [
                  ...state.messages,
                  {
                    id: randomId(),
                    role: "tool",
                    content:
                      typeof payload === "string"
                        ? payload
                        : JSON.stringify(payload, null, 2),
                    pending: false,
                    toolName: typeof raw.name === "string" ? raw.name : undefined,
                    toolPayload: payload,
                  },
                ],
              }));
            });

            chatStream.addEventListener("final", event => {
              const data = JSON.parse((event as MessageEvent).data) as { content: string };
              set(state => {
                const messages = [...state.messages];
                if (currentAssistantId) {
                  const index = messages.findIndex(message => message.id === currentAssistantId);
                  if (index >= 0) {
                    messages[index] = {
                      ...messages[index],
                      content: data.content,
                      pending: false,
                      sources: extractSources(data.content),
                    };
                  }
                } else {
                  messages.push({
                    id: randomId(),
                    role: "assistant",
                    content: data.content,
                    pending: false,
                    sources: extractSources(data.content),
                  });
                }
                currentAssistantId = null;
                return { messages };
              });
            });

            const chainStream = new EventSource(`${apiBase}/chain/stream`);

            chainStream.addEventListener("block", event => {
              const data = JSON.parse((event as MessageEvent).data) as ChainBlock;
              set(state => ({
                chain: [
                  {
                    id: data.id,
                    timestamp: data.timestamp,
                    payload: data.payload,
                  },
                  ...state.chain,
                ].slice(0, 50),
              }));
            });

            chainStream.onerror = event => {
              console.error("Kolibri chain stream error", event);
              chainStream.close();
            };

            set({ chatStream, chainStream });
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
          pending: false,
        };
        set(state => ({ messages: [...state.messages, message] }));
        try {
          const apiBase = await ensureApiBase();
          await fetch(`${apiBase}/chat`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ session_id: sessionId, message: content }),
          });
        } catch (error) {
          set(state => ({
            messages: state.messages.map(msg =>
              msg.id === message.id ? { ...msg, pending: true } : msg,
            ),
          }));
          console.error("Kolibri chat request failed", error);
        }
      },
      setView: view => set({ activeView: view }),
      pushChainBlock: block =>
        set(state => ({ chain: [block, ...state.chain].slice(0, 50) })),
      setPreference: (key, value) =>
        set(state => ({ preferences: { ...state.preferences, [key]: value } })),
    }),
    {
      name: "kolibri-chat-state",
      partialize: state => ({
        sessionId: state.sessionId,
        messages: state.messages,
        chain: state.chain,
        activeView: state.activeView,
        preferences: state.preferences,
      }),
    },
  ),
);
