import { useMemo } from "react";
import { useChatStore } from "../state/useChatStore";
import { Card } from "./ui/card";

export function SourcesPanel() {
  const messages = useChatStore(state => state.messages);
  const sources = useMemo(() => {
    const last = [...messages].reverse().find(msg => msg.sources && msg.sources.length > 0);
    return last?.sources ?? [];
  }, [messages]);

  if (sources.length === 0) {
    return (
      <Card className="text-sm text-slate-400">
        <p className="font-semibold text-slate-300">Источники</p>
        <p className="mt-2 text-xs text-slate-500">Источники появятся, когда ассистент обратится к графу знаний.</p>
      </Card>
    );
  }

  return (
    <Card className="space-y-3 text-sm">
      <p className="font-semibold text-slate-200">Источники</p>
      <ul className="space-y-2 text-xs text-slate-300">
        {sources.map(item => (
          <li key={item.source} className="rounded-lg bg-slate-900/80 p-2">
            <p className="font-medium text-sky-300">{item.source}</p>
            {item.text && <p className="text-slate-400">{item.text}</p>}
          </li>
        ))}
      </ul>
    </Card>
  );
}
