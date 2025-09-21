import { useEffect, useState } from "react";
import { useAppStore } from "../store/useAppStore";
import { onInstallPrompt, showInstallPrompt } from "../lib/pwa";

export default function SseStatus() {
  const online = useAppStore((state) => state.online);
  const connect = useAppStore((state) => state.connect);
  const [installReady, setInstallReady] = useState(false);

  useEffect(() => {
    connect();
  }, [connect]);

  useEffect(() => {
    const unsubscribe = onInstallPrompt(setInstallReady);
    return () => {
      unsubscribe();
    };
  }, []);

  return (
    <div className="flex items-center gap-3 text-xs text-slate-300">
      <span className={`flex items-center gap-2 rounded-full px-3 py-1 ${online ? "bg-emerald-500/20 text-emerald-200" : "bg-rose-500/20 text-rose-200"}`}>
        <span className={`h-2 w-2 rounded-full ${online ? "bg-emerald-400" : "bg-rose-400"}`} />
        SSE {online ? "connected" : "offline"}
      </span>
      <button
        type="button"
        className="rounded-lg border border-brand-500/60 px-3 py-1 text-brand-100 transition hover:bg-brand-500/20"
        onClick={() => showInstallPrompt()}
        disabled={!installReady}
      >
        Install PWA
      </button>
    </div>
  );
}
