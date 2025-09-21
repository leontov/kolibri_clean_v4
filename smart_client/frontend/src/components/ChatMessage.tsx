import { ChatMessage as ChatMessageType, useChatStore } from "../state/useChatStore";
import { Card } from "./ui/card";
import { Button } from "./ui/button";
import { speakText } from "../hooks/useSpeech";

interface Props {
  message: ChatMessageType;
}

export function ChatMessage({ message }: Props) {
  const isAssistant = message.role === "assistant";
  const ttsRate = useChatStore(state => state.preferences.ttsRate);
  const isUser = message.role === "user";
  return (
    <div className="flex w-full flex-col gap-2">
      <div className="flex items-center justify-between text-xs uppercase tracking-wide text-slate-400">
        <span>{isAssistant ? "Ассистент" : isUser ? "Вы" : `Инструмент: ${message.toolName ?? "?"}`}</span>
        {isAssistant && (
          <Button size="sm" variant="ghost" onClick={() => speakText(message.content, ttsRate)}>
            Озвучить
          </Button>
        )}
      </div>
      <Card className="whitespace-pre-wrap text-sm leading-relaxed">
        {message.content}
        {message.pending && <span className="ml-2 animate-pulse text-slate-500">…</span>}
      </Card>
      {message.sources && message.sources.length > 0 && (
        <div className="pl-4 text-xs text-slate-400">
          <span className="font-semibold">Источники:</span>
          <ul className="list-disc pl-5">
            {message.sources.map(item => (
              <li key={item.source}>{item.source}</li>
            ))}
          </ul>
        </div>
      )}
    </div>
  );
}
