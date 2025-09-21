"""Computation helpers for multimodal Kolibri Smartness Index (mKSI)."""
from __future__ import annotations

from dataclasses import dataclass
from statistics import mean
from typing import Mapping, Sequence

from kolibri_x.eval.missions import MissionOutcome


@dataclass(frozen=True)
class MetricBreakdown:
    """Stores individual metric values and the aggregated mKSI score."""

    generalization: float
    parsimony: float
    autonomy: float
    reliability: float
    explainability: float
    usability: float

    @property
    def mksi(self) -> float:
        return mean(
            [
                self.generalization,
                self.parsimony,
                self.autonomy,
                self.reliability,
                self.explainability,
                self.usability,
            ]
        )

    def as_dict(self) -> Mapping[str, float]:
        return {
            "generalization": self.generalization,
            "parsimony": self.parsimony,
            "autonomy": self.autonomy,
            "reliability": self.reliability,
            "explainability": self.explainability,
            "usability": self.usability,
            "mksi": self.mksi,
        }


def _clamp(value: float) -> float:
    return max(0.0, min(1.0, value))


def compute_metrics(outcomes: Sequence[MissionOutcome]) -> MetricBreakdown:
    """Derives the mKSI metrics from mission outcomes."""

    if not outcomes:
        return MetricBreakdown(0.0, 0.0, 0.0, 0.0, 0.0, 0.0)

    domains = {}
    for outcome in outcomes:
        domain = outcome.mission_id.split(":", 1)[0]
        domains.setdefault(domain, []).append(outcome)

    generalization_scores = [mean(o.score for o in items) for items in domains.values()]
    generalization = _clamp(mean(generalization_scores)) if generalization_scores else 0.0

    parsimony_values = []
    for outcome in outcomes:
        optimal = max(len(outcome.expected_skills), 1)
        used = max(len(outcome.used_skills), 1)
        parsimony_values.append(_clamp(optimal / used))
    parsimony = _clamp(mean(parsimony_values))

    autonomy_values = []
    for outcome in outcomes:
        improvement = 1.0 if outcome.score >= 0.9 and not outcome.cached else 0.5 if outcome.cached else 0.0
        autonomy_values.append(improvement)
    autonomy = _clamp(mean(autonomy_values))

    reliability_values = []
    for outcome in outcomes:
        reliability = 1.0 if outcome.score >= 0.75 else 0.5 if outcome.score >= 0.5 else 0.0
        reliability_values.append(reliability)
    reliability = _clamp(mean(reliability_values))

    explainability_values = []
    for outcome in outcomes:
        coverage = outcome.skill_coverage()
        reasoning_quality = _clamp(outcome.reasoning_steps / max(outcome.plan_steps, 1))
        explainability_values.append(_clamp((coverage + reasoning_quality) / 2))
    explainability = _clamp(mean(explainability_values))

    usability_values = []
    for outcome in outcomes:
        complexity_penalty = 0.0 if outcome.plan_steps <= 3 else 0.2 if outcome.plan_steps <= 6 else 0.4
        usability_values.append(_clamp(outcome.score - complexity_penalty))
    usability = _clamp(mean(usability_values))

    return MetricBreakdown(
        generalization=generalization,
        parsimony=parsimony,
        autonomy=autonomy,
        reliability=reliability,
        explainability=explainability,
        usability=usability,
    )


def metrics_report(outcomes: Sequence[MissionOutcome]) -> Mapping[str, float]:
    """Convenience helper returning a serialisable metrics mapping."""

    breakdown = compute_metrics(outcomes)
    return breakdown.as_dict()


__all__ = ["MetricBreakdown", "compute_metrics", "metrics_report"]
