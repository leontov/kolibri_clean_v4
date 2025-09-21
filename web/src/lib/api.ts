export interface BlockEvent {
  step: number;
  hash: string;
  eff: number;
  compl: number;
  fa: string;
}

export interface VerifyEvent {
  ok: boolean;
  step?: number;
  reason?: string;
  blocks?: number;
}

export interface MetricEvent {
  eff_val: number;
  compl: number;
  time_ms: number;
}

interface StreamHandlers {
  onBlock: (event: BlockEvent) => void;
  onVerify: (event: VerifyEvent) => void;
  onMetric: (event: MetricEvent) => void;
  onStatus: (online: boolean) => void;
}

const jsonHeaders = { Accept: "application/json" };

export async function fetchStatus() {
  const res = await fetch("/api/v1/status", { headers: jsonHeaders });
  if (!res.ok) {
    throw new Error("status request failed");
  }
  return res.json();
}

export async function fetchChain(tail = 50) {
  const res = await fetch(`/api/v1/chain?tail=${tail}`, { headers: jsonHeaders });
  if (!res.ok) {
    throw new Error("chain request failed");
  }
  return res.json();
}

export function subscribeToStream(handlers: StreamHandlers) {
  let source: EventSource | null = null;
  const connect = () => {
    source = new EventSource("/api/v1/chain/stream");
    source.addEventListener("block", (event) => {
      handlers.onBlock(JSON.parse((event as MessageEvent).data));
    });
    source.addEventListener("verify", (event) => {
      handlers.onVerify(JSON.parse((event as MessageEvent).data));
    });
    source.addEventListener("metric", (event) => {
      handlers.onMetric(JSON.parse((event as MessageEvent).data));
    });
    source.addEventListener("error", () => {
      handlers.onStatus(false);
      if (source) {
        source.close();
        source = null;
      }
      setTimeout(connect, 3000);
    });
    source.addEventListener("open", () => handlers.onStatus(true));
  };
  if (typeof window !== "undefined") {
    window.addEventListener("online", () => handlers.onStatus(true));
    window.addEventListener("offline", () => handlers.onStatus(false));
  }
  connect();
  return () => {
    if (source) {
      source.close();
    }
  };
}
