import { useMemo, useState } from "react";
import BlockCard from "../components/BlockCard";
import { useAppStore } from "../store/useAppStore";

export default function Ledger() {
  const blocks = useAppStore((state) => state.blocks);
  const [query, setQuery] = useState("");

  const filtered = useMemo(() => {
    if (!query) return blocks;
    const lower = query.toLowerCase();
    return blocks.filter((block) => block.hash.toLowerCase().includes(lower) || block.payload.fa.includes(query));
  }, [blocks, query]);

  return (
    <section className="space-y-4">
      <div className="flex items-center justify-between gap-4">
        <h2 className="text-xl font-semibold">KPRL Ledger</h2>
        <input
          type="search"
          placeholder="Search hash or FA-10"
          value={query}
          onChange={(event) => setQuery(event.target.value)}
          className="w-64 rounded-lg border border-slate-700 bg-slate-900/70 px-3 py-2 text-sm text-slate-100"
        />
      </div>
      <div className="grid gap-4 md:grid-cols-2">
        {filtered.map((block) => (
          <BlockCard key={block.hash} block={block} />
        ))}
      </div>
    </section>
  );
}
