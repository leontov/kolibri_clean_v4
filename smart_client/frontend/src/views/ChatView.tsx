import { FormEvent, useEffect, useRef, useState } from "react";
import { useChatStore } from "../state/useChatStore";
import { ChatMessage } from "../components/ChatMessage";
import { ToolBadge } from "../components/ToolBadge";
import { Button } from "../components/ui/button";
import { VoiceButton } from "../components/VoiceButton";

export function ChatView() {
  const messages = useChatStore(state => state.messages);
  const sendMessage = useChatStore(state => state.sendMessage);
  const connect = useChatStore(state => state.connect);
  const [input, setInput] = useState("");
  const containerRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    connect();
  }, [connect]);

  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    el.scrollTo({ top: el.scrollHeight, behavior: "smooth" });
  }, [messages.length]);

  const onSubmit = async (event: FormEvent) => {
    event.preventDefault();
    if (!input.trim()) return;
    await sendMessage(input.trim());
    setInput("");
  };

  return (
    <div className="flex h-full flex-col gap-4">
      <div ref={containerRef} className="flex-1 space-y-4 overflow-y-auto pr-2">
        {messages.map(message =>
          message.role === "tool" ? (
            <ToolBadge key={message.id} message={message} />
          ) : (
            <ChatMessage key={message.id} message={message} />
          )
        )}
        {messages.length === 0 && (
          <div className="text-sm text-slate-400">
            Начните диалог: попросите меня построить план миссии или уточнить статус цепочки KPRL.
          </div>
        )}
      </div>
      <form onSubmit={onSubmit} className="flex items-center gap-3">
        <textarea
          value={input}
          onChange={event => setInput(event.target.value)}
          placeholder="Спросите про Kolibri или попросите выполнить действие"
          className="h-24 flex-1 resize-none rounded-2xl bg-slate-900/80 p-4 text-sm text-slate-100 shadow-inner focus:outline-none focus:ring-2 focus:ring-primary/60"
        />
        <div className="flex flex-col gap-2">
          <VoiceButton onResult={text => setInput(text)} />
          <Button type="submit">Отправить</Button>
        </div>
      </form>
    </div>
  );
}
