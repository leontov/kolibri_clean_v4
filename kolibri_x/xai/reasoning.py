"""Reasoning log primitives for transparent explanations."""
from __future__ import annotations

from dataclasses import dataclass, field
import json
from typing import Iterable, List, Mapping, MutableMapping, Sequence


@dataclass
class ReasoningStep:
    name: str
    message: str
    references: Sequence[str] = field(default_factory=tuple)
    confidence: float = 0.5

    def to_dict(self) -> Mapping[str, object]:
        return {
            "name": self.name,
            "message": self.message,
            "references": list(self.references),
            "confidence": self.confidence,
        }


class ReasoningLog:
    def __init__(self) -> None:
        self._steps: List[ReasoningStep] = []

    def add_step(self, name: str, message: str, references: Iterable[str] | None = None, confidence: float = 0.5) -> None:
        refs = tuple(references or ())
        self._steps.append(ReasoningStep(name=name, message=message, references=refs, confidence=confidence))

    def clear(self) -> None:
        self._steps.clear()

    def steps(self) -> List[ReasoningStep]:
        return list(self._steps)

    def to_dict(self) -> Mapping[str, object]:
        return {"steps": [step.to_dict() for step in self._steps]}

    def to_json(self) -> str:
        return json.dumps(self.to_dict(), ensure_ascii=False, indent=2)


__all__ = ["ReasoningLog", "ReasoningStep"]
