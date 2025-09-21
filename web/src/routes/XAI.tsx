import { useAppStore } from "../store/useAppStore";

export default function XAI() {
  const blocks = useAppStore((state) => state.blocks);
  const latest = blocks[blocks.length - 1];

  return (
    <section className="space-y-4">
      <h2 className="text-xl font-semibold">Explainability stream</h2>
      <div className="card space-y-3">
        <p className="text-sm text-slate-300">
          KPRL (Kolibri Proof Record Ledger) signs every payload with deterministic JSON and optional HMAC. Inspect how the
          canonical serializer stabilizes FA-10 traces.
        </p>
        {latest ? (
          <div className="rounded-lg border border-slate-800 bg-slate-900/60 p-4">
            <h3 className="text-sm font-semibold text-slate-200">Latest explain</h3>
            <p className="text-sm text-slate-400">{latest.payload.explain}</p>
            <pre className="mt-3 max-h-60 overflow-auto rounded bg-slate-950/80 p-3 text-xs text-slate-300">
              {JSON.stringify(latest.payload, null, 2)}
            </pre>
          </div>
        ) : (
          <p className="text-sm text-slate-500">Waiting for payloads…</p>
        )}
        <div className="text-xs text-slate-500">
          <p>Hash: {latest?.hash ?? "pending"}</p>
          <p>Prev: {latest?.payload.prev ?? "genesis"}</p>
        </div>
      </div>
    </section>
  );
}
