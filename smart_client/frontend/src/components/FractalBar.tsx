interface Props {
  fa: number;
  faStab: number;
  faMap: number;
}

export function FractalBar({ fa, faStab, faMap }: Props) {
  const clamp = (value: number) => Math.min(Math.max(value, 0), 1);
  const metrics = [
    { label: "FA", value: clamp(fa), color: "bg-sky-500" },
    { label: "Stab", value: clamp(faStab), color: "bg-emerald-500" },
    { label: "Map", value: clamp(faMap), color: "bg-purple-500" }
  ];
  return (
    <div className="space-y-1 text-xs text-slate-300">
      {metrics.map(metric => (
        <div key={metric.label} className="space-y-1">
          <div className="flex items-center justify-between">
            <span>{metric.label}</span>
            <span className="font-mono text-[11px]">{metric.value.toFixed(3)}</span>
          </div>
          <div className="h-2 w-full rounded-full bg-slate-800">
            <div
              className={`${metric.color} h-2 rounded-full transition-all`}
              style={{ width: `${metric.value * 100}%` }}
            />
          </div>
        </div>
      ))}
    </div>
  );
}
