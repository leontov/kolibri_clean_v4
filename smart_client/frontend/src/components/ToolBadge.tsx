import { ChatMessage } from "../state/useChatStore";
import { Card } from "./ui/card";

interface Props {
  message: ChatMessage;
}

type MathPayload = {
  type?: string;
  summary?: string;
  answer?: unknown;
  steps?: unknown;
  references?: unknown;
  error?: string;
};

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function renderMathPayload(payload: unknown) {
  if (!isRecord(payload)) {
    return null;
  }
  const math = payload as MathPayload;
  const isError = math.type === "error" || typeof math.error === "string";
  const summary = typeof math.summary === "string" ? math.summary : undefined;
  const answerValue = math.answer;
  const stepsValue = math.steps;
  const referencesValue = math.references;

  const answer =
    typeof answerValue === "string"
      ? answerValue
      : answerValue !== undefined
      ? JSON.stringify(answerValue, null, 2)
      : undefined;

  const steps = Array.isArray(stepsValue)
    ? stepsValue.map(step => (typeof step === "string" ? step : JSON.stringify(step)))
    : [];

  const references = Array.isArray(referencesValue)
    ? referencesValue.map(ref => (typeof ref === "string" ? ref : JSON.stringify(ref)))
    : [];

  return (
    <div className="space-y-2">
      <div className="text-xs uppercase tracking-wide text-amber-200/80">Математический решатель</div>
      {isError ? (
        <p className="text-sm text-amber-100/80">
          {math.error ?? summary ?? "Не удалось вычислить результат."}
        </p>
      ) : (
        <div className="space-y-2 text-sm text-amber-50">
          {summary && <p className="font-semibold text-amber-100">{summary}</p>}
          {answer && (
            <div>
              <div className="text-xs uppercase tracking-wide text-amber-200/70">Итог</div>
              <pre className="mt-1 whitespace-pre-wrap text-sm leading-snug text-amber-50">{answer}</pre>
            </div>
          )}
          {steps.length > 0 && (
            <div>
              <div className="text-xs uppercase tracking-wide text-amber-200/70">Шаги</div>
              <ul className="mt-1 list-disc space-y-1 pl-5">
                {steps.map((step, index) => (
                  <li key={`${step}-${index}`} className="text-amber-100/90">
                    {step}
                  </li>
                ))}
              </ul>
            </div>
          )}
          {references.length > 0 && (
            <div>
              <div className="text-xs uppercase tracking-wide text-amber-200/70">Ссылки</div>
              <ul className="mt-1 list-disc space-y-1 pl-5">
                {references.map(ref => (
                  <li key={ref} className="text-amber-100/80">
                    {ref}
                  </li>
                ))}
              </ul>
            </div>
          )}
        </div>
      )}
    </div>
  );
}

export function ToolBadge({ message }: Props) {
  if (message.role !== "tool") return null;

  const payload = renderMathPayload(message.toolPayload);

  return (
    <Card className="bg-amber-500/10 text-xs text-amber-200">
      <p className="font-semibold">Вызов инструмента: {message.toolName ?? "?"}</p>
      <div className="mt-2 max-h-48 overflow-auto text-[12px] leading-snug">
        {payload ? (
          payload
        ) : (
          <pre className="whitespace-pre-wrap text-amber-100">{message.content}</pre>
        )}
      </div>
    </Card>
  );
}
