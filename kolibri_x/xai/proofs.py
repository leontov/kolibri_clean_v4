"""Structured proof payloads for RuntimeResponse."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterable, Mapping, MutableMapping, Sequence


@dataclass(frozen=True)
class ConfidenceInterval:
    """Represents a bounded confidence interval."""

    lower: float
    upper: float

    def to_dict(self) -> Mapping[str, float]:
        return {"lower": self.lower, "upper": self.upper}

    @classmethod
    def from_payload(cls, payload: Mapping[str, object]) -> "ConfidenceInterval":
        lower = float(payload.get("lower", 0.0))
        upper = float(payload.get("upper", 0.0))
        lower = max(0.0, min(1.0, lower))
        upper = max(lower, min(1.0, upper))
        return cls(lower=lower, upper=upper)


@dataclass(frozen=True)
class StructuredProof:
    """Single verified claim returned to clients."""

    fact_id: str
    claim: str
    confidence_interval: ConfidenceInterval
    sources: Sequence[str] = field(default_factory=tuple)
    score: float = 0.0

    def to_dict(self) -> Mapping[str, object]:
        return {
            "fact_id": self.fact_id,
            "claim": self.claim,
            "confidence_interval": self.confidence_interval.to_dict(),
            "sources": list(self.sources),
            "score": self.score,
        }

    @classmethod
    def from_dict(cls, payload: Mapping[str, object]) -> "StructuredProof":
        fact_id = str(payload.get("fact_id", ""))
        claim = str(payload.get("claim", ""))
        sources = tuple(str(item) for item in payload.get("sources", []) if item is not None)
        score = float(payload.get("score", 0.0))
        interval_payload = payload.get("confidence_interval", {})
        if not isinstance(interval_payload, Mapping):
            interval_payload = {}
        interval = ConfidenceInterval.from_payload(interval_payload)
        return cls(fact_id=fact_id, claim=claim, confidence_interval=interval, sources=sources, score=score)


def _interval_for_confidence(confidence: float, verification: float) -> ConfidenceInterval:
    base = max(0.0, min(1.0, confidence))
    verifier = max(0.0, min(1.0, verification))
    # Narrower interval when both confidence and verification scores are high.
    width = max(0.1, 0.6 - 0.3 * (base + verifier))
    lower = max(0.0, base - width / 2)
    upper = min(1.0, base + width / 2)
    return ConfidenceInterval(lower=round(lower, 3), upper=round(upper, 3))


def build_structured_proofs(answer: Mapping[str, object]) -> Sequence[StructuredProof]:
    """Construct proofs from an answer payload.

    Each supporting fact becomes a structured proof with an adaptive
    confidence interval derived from fact confidence and verification
    confidence.
    """

    support = answer.get("support", [])
    verification_payload = answer.get("verification", {})
    verification_confidence = 0.0
    if isinstance(verification_payload, Mapping):
        verification_confidence = float(verification_payload.get("confidence", 0.0))
    proofs: MutableMapping[str, StructuredProof] = {}
    if isinstance(support, Iterable):
        for fact in support:
            if not isinstance(fact, Mapping):
                continue
            fact_id = str(fact.get("id", "")) or "fact"
            claim = str(fact.get("text", fact_id))
            confidence = float(fact.get("confidence", verification_confidence))
            interval = _interval_for_confidence(confidence, verification_confidence)
            raw_sources = fact.get("sources", [])
            sources = tuple(str(item) for item in raw_sources if item is not None)
            score = float(fact.get("score", 0.0))
            proofs[fact_id] = StructuredProof(
                fact_id=fact_id,
                claim=claim,
                confidence_interval=interval,
                sources=sources,
                score=score,
            )
    if proofs:
        return tuple(proofs.values())
    # Fall back to a single synthetic proof derived from the summary.
    summary = str(answer.get("summary", ""))
    interval = _interval_for_confidence(float(answer.get("confidence", 0.0)), verification_confidence)
    if not summary:
        summary = str(answer.get("query", "")) or "runtime-response"
    verification_sources: Iterable[object] = ()
    if isinstance(verification_payload, Mapping):
        verification_sources = verification_payload.get("sources", [])
    return (
        StructuredProof(
            fact_id="summary",
            claim=summary,
            confidence_interval=interval,
            sources=tuple(str(item) for item in verification_sources if item is not None),
            score=verification_confidence,
        ),
    )


__all__ = ["ConfidenceInterval", "StructuredProof", "build_structured_proofs"]
