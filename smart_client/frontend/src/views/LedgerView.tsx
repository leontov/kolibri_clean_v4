import { useChatStore } from "../state/useChatStore";
import { Card } from "../components/ui/card";
import { FractalBar } from "../components/FractalBar";

export function LedgerView() {
  const chain = useChatStore(state => state.chain);

  if (chain.length === 0) {
    return (
      <Card className="text-sm text-slate-400">
        Журнал KPRL появится после первого запуска kolibri_run.
      </Card>
    );
  }

  return (
    <div className="space-y-4">
      {chain.map(block => {
        const payload = block.payload as Record<string, string>;
        const fa = parseFloat(payload.fa as unknown as string);
        const faStab = parseFloat(payload.fa_stab as unknown as string);
        const faMap = parseFloat(payload.fa_map as unknown as string);
        return (
          <Card key={block.id} className="space-y-3 text-sm">
            <div className="flex flex-wrap items-center justify-between gap-2 text-xs text-slate-400">
              <span className="font-semibold text-slate-200">{payload.summary ?? "Шаг"}</span>
              <span className="font-mono text-[11px]">{block.timestamp}</span>
            </div>
            <FractalBar fa={fa} faStab={faStab} faMap={faMap} />
            <div className="grid grid-cols-2 gap-2 text-xs text-slate-300">
              <div>
                <p className="text-slate-400">Шагов</p>
                <p className="font-semibold">{payload.step as unknown as string}</p>
              </div>
              <div>
                <p className="text-slate-400">Вознаграждение r</p>
                <p className="font-semibold">{payload.r as unknown as string}</p>
              </div>
            </div>
          </Card>
        );
      })}
    </div>
  );
}
