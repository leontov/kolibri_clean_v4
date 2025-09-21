import { useEffect } from "react";
import { Button } from "./ui/button";
import { useSpeechRecognition } from "../hooks/useSpeech";

interface Props {
  onResult: (text: string) => void;
}

export function VoiceButton({ onResult }: Props) {
  const { listening, start, stop } = useSpeechRecognition({ onResult });

  useEffect(() => {
    return () => {
      stop();
    };
  }, [stop]);

  return (
    <Button
      type="button"
      variant={listening ? "primary" : "ghost"}
      size="sm"
      onClick={() => (listening ? stop() : start())}
    >
      {listening ? "Слушаю…" : "Голос"}
    </Button>
  );
}
