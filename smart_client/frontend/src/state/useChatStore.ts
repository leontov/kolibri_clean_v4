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
const RECONNECT_DELAY_MS = 2000;

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
  if (!value) return "";
  const trimmed = value.trim();
  if (!trimmed) return "";
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
  if (!trimmed) return "";
  if (/^(?:[a-z]+:)?\/\//i.test(trimmed)) {
    // Absolute URL (with protocol), leave as-is sans trailing slash
    return trimTrailingSlash(trimmed);
  }
  // Relative base (like "/app"), ensure leading slash
  return trimmed.startsWith("/") ? trimmed : `/${trimmed}`;
}

function normalizePath(value: string): string {
  const trimmed = trimTrailingSlash(value.trim());
  if (!trimmed) return "";
  return trimmed.startsWith("/") ? trimmed : `/${trimmed}`;
}

function joinBaseAndPath(base: string, path: string): string {
  if (!base) return path;
  if (!path) return base;
  if (/^(?:[a-z]+:)?\/\//i.test(path)) return path; // full URL passed as path
  const sanitizedBase = base.replace(/\/+$/, "");
  const sanitizedPath = path.replace(/^\/+/, "");
  return `${sanitizedBase}/${sanitizedPath}`;
}

function inferBaseFromLocation(): string {
  if (typeof window === "undefined") return "";
  const trimmedPath = trimTrailingSlash(window.location.pathname);
  if (!trimmedPath || trimmedPath === "/") return "";
  const segments = trimmedPath.split("/").filter(Boolean);
  if (segments.length === 0) return "";
  const last = segments[segments.length - 1];
  if (last.includes(".") || KNOWN_APP_ROUTES.has(last)) {
    segments.pop();
  }
  if (segments.length === 0) return "";
  return `/${segments.join("/")}`;
}

/* async function detectApiBase(): Promise<string> {
  const candidates = resolveApiBaseCandidates();
  for (const base of candidates) {
    try {
      // Our backend exposes /status under the API prefix
      const resp = await fetch(`${base}/status`, { method: "GET" });
      if (resp.ok) return base;
    } catch {
      // try next candidate
    }
  }
  // last resort: assume first candidate or empty string
  return candidates[0] ?? "";
}

async function ensureApiBase(): Promise<string> {
  if (resolvedApiBase !== null) return resolvedApiBase;
  if (!resolvingApiBase) {
    resolvingApiBase = detectApiBase().then(base => {
      resolvedApiBase = base;
      return base;
    });
  }
  return resolvingApiBase;
}

function resolveApiBaseCandidates(): string[] {
  const candidates: string[] = [];
  const rawBases = resolveRawBaseCandidates();
  const apiPaths = resolveApiPathCandidates();

  const add = (v: string) => {
    const n = normalizeCandidate(v);
    if (n === "" && candidates.includes("")) return;
    if (n && candidates.includes(n)) return;
    candidates.push(n);
  };

  // Combine every raw base with every api prefix
  for (const rb of rawBases) {
    for (const ap of apiPaths) {
      add(joinBaseAndPath(rb, ap));
    }
  }

  return candidates;
}

function resolveRawBaseCandidates(): string[] {
  const raw: string[] = [];
  const addBase = (v: string) => {
    const n = normalizeBase(v);
    if (raw.includes(n)) return;
    raw.push(n);
  };

  addBase(import.meta.env.VITE_API_BASE ?? "");

  if (typeof window !== "undefined") {
    const g = window as Window & { __KOLIBRI_API_BASE__?: string; __kolibriApiBase?: string };
    if (g.__KOLIBRI_API_BASE__ || g.__kolibriApiBase) {
      addBase(g.__KOLIBRI_API_BASE__ ?? g.__kolibriApiBase ?? "");
    }
    const meta = document.querySelector('meta[name="kolibri-api-base"]')?.getAttribute("content");
    if (meta) addBase(meta);

    // Vite BASE_URL and page path can hint deployment subpath
    addBase(import.meta.env.BASE_URL ?? "");
    addBase(inferBaseFromLocation());
  }

  // final fallback = root
  addBase("");
  return raw;
}

function resolveApiPathCandidates(): string[] {
  const apiPaths: string[] = [];
  const addPath = (v: string) => {
    const n = normalizePath(v);
    if (apiPaths.includes(n)) return;
    apiPaths.push(n);
  };

  // explicit env / globals / meta
  addPath(import.meta.env.VITE_API_PREFIX ?? "");
  if (typeof window !== "undefined") {
    const gwp = window as Window & { __KOLIBRI_API_PREFIX__?: string; __kolibriApiPrefix?: string };
    addPath(gwp.__KOLIBRI_API_PREFIX__ ?? gwp.__kolibriApiPrefix ?? "");
    const meta = document.querySelector('meta[name="kolibri-api-prefix"]')?.getAttribute("content");
    if (meta) addPath(meta);
  }

  // common fallbacks (prefer /api/v1 first as our backend exposes it)
  addPath("/api/v1");
  addPath("/v1");
  addPath("/api");
  addPath("");

  return apiPaths;
} */

function extractSources(content: string): SourceItem[] | undefined {
  const index = content.indexOf("Источники:");
  if (index === -1) return undefined;
  const tail = content.slice(index + "Источники:".length).trim();
  if (!tail) return undefined;
  return tail.split("\n").map(line => ({ source: line.replace(/^•\s*/, "").trim() }));
}

function selectToolPayload(data: Record<string, unknown>): unknown {
  if ("payload" in data) return (data as { payload: unknown }).payload;
  if ("result" in data) return (data as { result: unknown }).result;
  if ("parameters" in data) return (data as { parameters: unknown }).parameters;
  return data;
}

async function fetchHistoryAsMessages(sessionId: string): Promise<ChatMessage[] | null> {
  try {
    const apiBase = "http://127.0.0.1:8000/api/v1";
    const resp = await fetch(`${apiBase}/history?session_id=${encodeURIComponent(sessionId)}`);
    if (!resp.ok) return null;
    const data = (await resp.json()) as { messages?: Array<{ role: string; content: string; timestamp?: string }> };
    if (!data?.messages) return null;
    return data.messages.map((m, idx) => ({
      id: `h-${idx}-${Date.now()}`,
      role: (m.role as Role) ?? "assistant",
      content: m.content,
      pending: false,
      sources: extractSources(m.content),
    }));
  } catch {
    return null;
  }
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
      preferences: { ttsRate: 1 },

      connect: () => {
        const { sessionId, connected, chatStream: cs, chainStream: ls } = get();
        if (connected && cs && cs.readyState !== EventSource.CLOSED) return;
        cs?.close();
        ls?.close();

        const apiBase = "http://127.0.0.1:8000/api/v1";

        const chatStream = new EventSource(`${apiBase}/chat/stream?session_id=${sessionId}`);
        let currentAssistantId: string | null = null;

        chatStream.onopen = () => set({ connected: true });
        chatStream.onerror = event => {
          console.error("Kolibri chat stream error", event);
          chatStream.close();
          set({ connected: false, chatStream: null });
          // try to reconnect after a short delay
          setTimeout(() => {
            try {
              get().connect();
            } catch (e) {
              console.error("Kolibri chat reconnect failed", e);
            }
          }, RECONNECT_DELAY_MS);
        };

        chatStream.onmessage = (event) => {
          console.log("RAW SSE", event.data);
        };

        chatStream.addEventListener("token", event => {
          const data = JSON.parse((event as MessageEvent).data) as { content: string };
          set(state => {
            const messages = [...state.messages];
            if (!currentAssistantId) {
              currentAssistantId = randomId();
              messages.push({ id: currentAssistantId, role: "assistant", content: "", pending: true });
            }
            const idx = messages.findIndex(m => m.id === currentAssistantId);
            if (idx >= 0) {
              messages[idx] = { ...messages[idx], content: `${messages[idx].content}${data.content}` };
            }
            return { messages };
          });
        });

        chatStream.addEventListener("tool_call", event => {
          const raw = JSON.parse((event as MessageEvent).data) as Record<string, unknown> & { name?: string };
          const payload = selectToolPayload(raw);
          set(state => ({
            messages: [
              ...state.messages,
              {
                id: randomId(),
                role: "tool",
                content: typeof payload === "string" ? payload : JSON.stringify(payload, null, 2),
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
              const idx = messages.findIndex(m => m.id === currentAssistantId);
              if (idx >= 0) {
                messages[idx] = {
                  ...messages[idx],
                  content: data.content,
                  pending: false,
                  sources: extractSources(data.content),
                };
              }
            } else {
              messages.push({ id: randomId(), role: "assistant", content: data.content, pending: false, sources: extractSources(data.content) });
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
              { id: data.id, timestamp: data.timestamp, payload: data.payload },
              ...state.chain,
            ].slice(0, 50),
          }));
        });
        chainStream.onerror = event => {
          console.error("Kolibri chain stream error", event);
          chainStream.close();
          // attempt to re-establish the chain stream on next connect
          setTimeout(() => {
            try {
              get().connect();
            } catch (e) {
              console.error("Kolibri chain reconnect failed", e);
            }
          }, RECONNECT_DELAY_MS);
        };

        set({ chatStream, chainStream });
      },

      sendMessage: async content => {
        // ensure we have an open SSE connection before/while sending
        const st = get();
        if (!st.connected || !st.chatStream || st.chatStream.readyState === EventSource.CLOSED) {
          st.connect();
        }
        const { sessionId } = get();
        const message: ChatMessage = { id: randomId(), role: "user", content, pending: false };
        set(state => ({ messages: [...state.messages, message] }));
        try {
          const apiBase = "http://127.0.0.1:8000/api/v1";
          await fetch(`${apiBase}/chat`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ session_id: sessionId, message: content }),
          });
          // Fallback: if SSE isn't delivering, pull history once to surface assistant reply
          setTimeout(async () => {
            const msgs = await fetchHistoryAsMessages(sessionId);
            if (msgs) {
              set({ messages: msgs });
              console.log("SYNCED HISTORY", msgs);
            }
          }, 1200);
        } catch (error) {
          set(state => ({
            messages: state.messages.map(m => (m.id === message.id ? { ...m, pending: true } : m)),
          }));
          console.error("Kolibri chat request failed", error);
          // try to at least sync history so UI reflects backend state
          setTimeout(async () => {
            const msgs = await fetchHistoryAsMessages(sessionId);
            if (msgs) {
              set({ messages: msgs });
              console.log("SYNCED HISTORY", msgs);
            }
          }, 1500);
        }
      },

      setView: view => set({ activeView: view }),
      pushChainBlock: block => set(state => ({ chain: [block, ...state.chain].slice(0, 50) })),
      setPreference: (key, value) => set(state => ({ preferences: { ...state.preferences, [key]: value } })),
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
