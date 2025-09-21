import { useEffect } from "react";
import { ChatView } from "./views/ChatView";
import { LedgerView } from "./views/LedgerView";
import { SettingsView } from "./views/SettingsView";
import { SourcesPanel } from "./components/SourcesPanel";
import { Button } from "./components/ui/button";
import { useChatStore } from "./state/useChatStore";

const views = {
  chat: ChatView,
  ledger: LedgerView,
  settings: SettingsView
} as const;

export default function App() {
  const activeView = useChatStore(state => state.activeView);
  const setView = useChatStore(state => state.setView);
  const sessionId = useChatStore(state => state.sessionId);

  useEffect(() => {
    document.title = "Kolibri Smart Client";
  }, []);

  const ViewComponent = views[activeView];

  return (
    <div className="flex min-h-screen bg-slate-950 text-slate-100">
      <aside className="flex w-64 flex-col gap-4 border-r border-slate-800 bg-slate-950/70 p-6">
        <div>
          <p className="text-xs uppercase tracking-widest text-slate-500">Сессия</p>
          <p className="mt-2 break-all text-sm text-slate-300">{sessionId}</p>
        </div>
        <nav className="flex flex-col gap-2 text-sm">
          <Button
            variant={activeView === "chat" ? "primary" : "ghost"}
            onClick={() => setView("chat")}
          >
            Чат
          </Button>
          <Button
            variant={activeView === "ledger" ? "primary" : "ghost"}
            onClick={() => setView("ledger")}
          >
            Ledger
          </Button>
          <Button
            variant={activeView === "settings" ? "primary" : "ghost"}
            onClick={() => setView("settings")}
          >
            Настройки
          </Button>
        </nav>
        <div className="mt-auto text-xs text-slate-500">
          Стриминг ответов через SSE. Токены появляются мгновенно; инструменты отображаются в ленте.
        </div>
      </aside>
      <main className="flex min-h-screen flex-1 flex-col gap-6 p-8">
        <header className="flex items-center justify-between">
          <div>
            <h1 className="text-2xl font-semibold text-slate-100">Kolibri Assistant</h1>
            <p className="text-sm text-slate-400">
              Общение на русском, контроль инструментов и прозрачный журнал KPRL.
            </p>
          </div>
        </header>
        <div className="grid flex-1 grid-cols-[minmax(0,1fr)_320px] gap-6">
          <div className="flex flex-col">
            <ViewComponent />
          </div>
          <div className="hidden lg:block">
            <SourcesPanel />
          </div>
        </div>
      </main>
    </div>
  );
}
