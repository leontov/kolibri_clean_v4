interface Props {
  fa: string;
}

export default function FractalBar({ fa }: Props) {
  return (
    <div className="flex items-center gap-1 text-xs font-mono">
      {fa.split("").map((digit, index) => (
        <span
          key={`${digit}-${index}`}
          className="flex h-6 w-6 items-center justify-center rounded bg-slate-800/80 text-brand-100"
        >
          {digit}
        </span>
      ))}
    </div>
  );
}
