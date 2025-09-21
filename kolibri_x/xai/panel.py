"""Structured explainability panel builder for Kolibri runtime results."""
from __future__ import annotations

from dataclasses import dataclass, field
from statistics import mean
from typing import Iterable, List, Mapping, MutableMapping, Sequence

from kolibri_x.core.planner import Plan
from kolibri_x.runtime.journal import JournalEntry
from kolibri_x.xai.reasoning import ReasoningLog, ReasoningStep


@dataclass
class EvidenceItem:
    """Represents a single evidence record surfaced to the XAI panel."""

    reference: str
    source: str | None = None
    confidence: float = 0.0

    def to_dict(self) -> Mapping[str, object]:
        return {
            "reference": self.reference,
            "source": self.source,
            "confidence": self.confidence,
        }


@dataclass
class ExplanationTimeline:
    """Chronological representation of the reasoning steps."""

    steps: Sequence[ReasoningStep] = field(default_factory=tuple)

    def confidence_profile(self) -> Mapping[str, float]:
        if not self.steps:
            return {"min": 0.0, "max": 0.0, "mean": 0.0}
        confidences = [step.confidence for step in self.steps]
        return {
            "min": min(confidences),
            "max": max(confidences),
            "mean": mean(confidences),
        }

    def to_dict(self) -> Mapping[str, object]:
        return {
            "steps": [
                {
                    "name": step.name,
                    "message": step.message,
                    "references": list(step.references),
                    "confidence": step.confidence,
                }
                for step in self.steps
            ],
            "confidence_profile": self.confidence_profile(),
        }


class ExplanationPanel:
    """Aggregates plan, reasoning, and journal data into XAI payloads."""

    def __init__(
        self,
        *,
        plan: Plan,
        reasoning: ReasoningLog,
        answer: Mapping[str, object],
        adjustments: Mapping[str, float],
        journal_entries: Sequence[JournalEntry],
    ) -> None:
        self.plan = plan
        self.timeline = ExplanationTimeline(tuple(reasoning.steps()))
        self.answer = dict(answer)
        self.adjustments = dict(adjustments)
        self.evidence = self._extract_evidence(journal_entries, answer)
        self.audit_trail = [entry.to_dict() for entry in journal_entries]

    def to_dict(self) -> Mapping[str, object]:
        return {
            "plan": self.plan.as_dict(),
            "timeline": self.timeline.to_dict(),
            "answer": self.answer,
            "adjustments": self.adjustments,
            "evidence": [item.to_dict() for item in self.evidence],
            "audit_trail": self.audit_trail,
        }

    def _extract_evidence(
        self,
        journal_entries: Sequence[JournalEntry],
        answer: Mapping[str, object],
    ) -> Sequence[EvidenceItem]:
        items: List[EvidenceItem] = []
        seen: MutableMapping[str, EvidenceItem] = {}
        support = answer.get("support")
        if isinstance(support, Iterable):
            for fact in support:
                if not isinstance(fact, Mapping):
                    continue
                reference = str(fact.get("id"))
                source = None
                sources = fact.get("source")
                if isinstance(sources, Sequence) and sources:
                    source = str(sources[0])
                item = EvidenceItem(
                    reference=reference,
                    source=source,
                    confidence=float(fact.get("confidence", 0.0)),
                )
                seen[reference] = item
                items.append(item)
        for entry in journal_entries:
            if entry.event != "rag_answer":
                continue
            support_ids = entry.payload.get("support")
            if not isinstance(support_ids, Iterable):
                continue
            for reference in support_ids:
                key = str(reference)
                if key not in seen:
                    items.append(EvidenceItem(reference=key, source=None, confidence=0.0))
        return tuple(items)


__all__ = [
    "EvidenceItem",
    "ExplanationPanel",
    "ExplanationTimeline",
]
