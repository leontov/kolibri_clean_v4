"""Active learning utilities for Kolibri-x."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, List, Mapping, Sequence


@dataclass
class CandidateExample:
    """Represents an unlabeled example with a model confidence score."""

    uid: str
    confidence: float
    metadata: Mapping[str, str]


@dataclass
class AnnotationRequest:
    """A request dispatched to the annotation queue."""

    uid: str
    priority: float
    rationale: str


class UncertaintyScorer:
    """Scores examples based on model confidence and coverage gaps."""

    def score(self, example: CandidateExample, coverage: Mapping[str, float]) -> float:
        base = 1.0 - example.confidence
        domain = example.metadata.get("domain")
        coverage_penalty = 1.0 - coverage.get(domain, 0.0) if domain else 0.5
        return base * (1.0 + coverage_penalty)


class ActiveLearner:
    """Selects which examples should be escalated for annotation."""

    def __init__(self, scorer: UncertaintyScorer | None = None, budget: int = 10) -> None:
        if budget <= 0:
            raise ValueError("budget must be positive")
        self._scorer = scorer or UncertaintyScorer()
        self.budget = budget

    def propose_annotations(
        self,
        candidates: Iterable[CandidateExample],
        coverage: Mapping[str, float] | None = None,
    ) -> Sequence[AnnotationRequest]:
        coverage = coverage or {}
        scored: List[tuple[float, CandidateExample]] = []
        for example in candidates:
            score = self._scorer.score(example, coverage)
            scored.append((score, example))
        scored.sort(key=lambda item: item[0], reverse=True)
        requests: List[AnnotationRequest] = []
        for score, example in scored[: self.budget]:
            rationale = self._build_rationale(example, score, coverage)
            requests.append(AnnotationRequest(uid=example.uid, priority=score, rationale=rationale))
        return requests

    def _build_rationale(
        self, example: CandidateExample, score: float, coverage: Mapping[str, float]
    ) -> str:
        domain = example.metadata.get("domain", "general")
        coverage_gap = 1.0 - coverage.get(domain, 0.0)
        return (
            f"Confidence={example.confidence:.2f}, domain={domain}, coverage_gap={coverage_gap:.2f}, "
            f"score={score:.2f}"
        )


__all__ = [
    "ActiveLearner",
    "AnnotationRequest",
    "CandidateExample",
    "UncertaintyScorer",
]
