import FractalBar from "./FractalBar";
import type { KolibriBlock } from "../store/useAppStore";

interface Props {
  block: KolibriBlock;
}

export default function BlockCard({ block }: Props) {
  const { payload } = block;
  return (
    <article className="card space-y-2">
      <div className="flex items-center justify-between">
        <h3 className="text-lg font-semibold">Step {payload.step}</h3>
        <span className="badge">eff {payload.eff_val.toFixed(3)}</span>
      </div>
      <FractalBar fa={payload.fa} />
      <p className="text-sm text-slate-300">{payload.explain}</p>
      <dl className="grid grid-cols-2 gap-x-4 gap-y-1 text-xs text-slate-400">
        <dt>Hash</dt>
        <dd className="truncate font-mono text-slate-200">{block.hash}</dd>
        <dt>Prev</dt>
        <dd className="truncate font-mono text-slate-500">{payload.prev || "genesis"}</dd>
        <dt>Compl</dt>
        <dd>{payload.compl.toFixed(2)}</dd>
        <dt>Run</dt>
        <dd>{payload.run_id}</dd>
      </dl>
    </article>
  );
}
