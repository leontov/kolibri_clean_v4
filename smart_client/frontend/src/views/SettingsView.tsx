import { useChatStore } from "../state/useChatStore";
import { Card } from "../components/ui/card";
import { Button } from "../components/ui/button";

export function SettingsView() {
  const sessionId = useChatStore(state => state.sessionId);
  const ttsRate = useChatStore(state => state.preferences.ttsRate);
  const setPreference = useChatStore(state => state.setPreference);

  const clearHistory = () => {
    if (!confirm("Очистить историю диалога?")) return;
    localStorage.removeItem("kolibri-chat-state");
    location.reload();
  };

  return (
    <div className="space-y-4">
      <Card className="space-y-2 text-sm">
        <p className="text-xs uppercase tracking-wide text-slate-500">Сессия</p>
        <p className="font-mono text-xs text-slate-300">{sessionId}</p>
        <Button variant="ghost" size="sm" onClick={clearHistory}>
          Очистить историю
        </Button>
      </Card>
      <Card className="space-y-3 text-sm">
        <p className="text-xs uppercase tracking-wide text-slate-500">Голос</p>
        <label className="flex items-center justify-between text-xs text-slate-400">
          Скорость синтеза речи
          <input
            type="range"
            min="0.5"
            max="1.5"
            step="0.1"
            value={ttsRate}
            onChange={event => setPreference("ttsRate", Number(event.target.value))}
            className="ml-4"
          />
        </label>
        <p className="text-[11px] text-slate-500">
          Настройка применяется к следующему озвучиванию.
        </p>
      </Card>
      <Card className="space-y-2 text-sm text-slate-300">
        <p className="font-semibold text-slate-100">Установка PWA</p>
        <p className="text-xs text-slate-400">
          Установите приложение через меню браузера «Добавить на экран» для офлайн-доступа.
        </p>
      </Card>
    </div>
  );
}
