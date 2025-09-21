import { useMemo } from "react";
import ChainGraph from "../components/ChainGraph";
import MetricChart from "../components/MetricChart";
import BlockCard from "../components/BlockCard";
import { useAppStore } from "../store/useAppStore";

export default function Dashboard() {
  const status = useAppStore((state) => state.status);
  const blocks = useAppStore((state) => state.blocks);
  const effHistory = useAppStore((state) => state.effHistory);
  const complHistory = useAppStore((state) => state.complHistory);

  const chartData = useMemo(() => {
    const complMap = new Map(complHistory.map((entry) => [entry.step, entry.compl]));
    const fallback = complHistory[complHistory.length - 1]?.compl ?? 0;
    return effHistory.map((entry) => ({
      step: entry.step,
      eff: entry.eff,
      compl: complMap.get(entry.step) ?? fallback
    }));
  }, [effHistory, complHistory]);

  const latestBlocks = blocks.slice(-4).reverse();

  return (
    <section className="space-y-6">
      <div className="grid gap-4 md:grid-cols-3">
        <div className="card">
          <h2 className="text-sm font-semibold uppercase tracking-wide text-slate-400">Run ID</h2>
          <p className="mt-2 text-lg font-semibold">{status?.run_id ?? "loading"}</p>
          <p className="text-xs text-slate-400">Version {status?.version}</p>
        </div>
        <div className="card">
          <h2 className="text-sm font-semibold uppercase tracking-wide text-slate-400">Last update</h2>
          <p className="mt-2 text-lg font-semibold">{status?.time_utc ?? "pending"}</p>
          <p className="text-xs text-slate-400">Fmt {status?.fmt}</p>
        </div>
        <div className="card">
          <h2 className="text-sm font-semibold uppercase tracking-wide text-slate-400">Blocks</h2>
          <p className="mt-2 text-lg font-semibold">{blocks.length}</p>
          <p className="text-xs text-slate-400">FA-10 stability streaming</p>
        </div>
      </div>
      <MetricChart
        title="Efficiency & Complexity"
        data={chartData}
        series={[
          { name: "eff", color: "#22d3ee", key: "eff" },
          { name: "compl", color: "#f97316", key: "compl" }
        ]}
      />
      <ChainGraph
        label="Efficiency (val)"
        data={effHistory.map((entry) => ({ step: entry.step, value: entry.eff }))}
        color="#38bdf8"
      />
      <div className="grid gap-4 md:grid-cols-2">
        {latestBlocks.map((block) => (
          <BlockCard key={block.hash} block={block} />
        ))}
      </div>
    </section>
  );
}
