import { Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis, CartesianGrid, Legend } from "recharts";

interface Series {
  name: string;
  color: string;
  key: string;
}

interface Props {
  data: Array<Record<string, number>>;
  series: Series[];
  title: string;
}

export default function MetricChart({ data, series, title }: Props) {
  return (
    <div className="card h-72">
      <h3 className="mb-3 text-sm font-semibold uppercase tracking-wide text-slate-400">{title}</h3>
      <ResponsiveContainer width="100%" height="100%">
        <LineChart data={data} margin={{ top: 10, right: 20, bottom: 10, left: 0 }}>
          <CartesianGrid stroke="rgba(148, 163, 184, 0.12)" />
          <XAxis dataKey="step" stroke="#94a3b8" fontSize={12} />
          <YAxis stroke="#94a3b8" fontSize={12} />
          <Tooltip contentStyle={{ background: "#0f172a", borderRadius: 8, border: "1px solid #1e293b" }} />
          <Legend />
          {series.map((serie) => (
            <Line key={serie.name} type="monotone" dataKey={serie.key} stroke={serie.color} strokeWidth={2} dot={false} name={serie.name} />
          ))}
        </LineChart>
      </ResponsiveContainer>
    </div>
  );
}
