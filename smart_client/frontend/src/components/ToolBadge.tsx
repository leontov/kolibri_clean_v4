import { ChatMessage } from "../state/useChatStore";
import { Card } from "./ui/card";

interface Props {
  message: ChatMessage;
}

export function ToolBadge({ message }: Props) {
  if (message.role !== "tool") return null;
  return (
    <Card className="bg-amber-500/10 text-xs text-amber-200">
      <p className="font-semibold">Вызов инструмента: {message.toolName ?? "?"}</p>
      <pre className="mt-2 max-h-40 overflow-auto whitespace-pre-wrap text-[11px] leading-snug text-amber-100">
        {message.content}
      </pre>
    </Card>
  );
}
