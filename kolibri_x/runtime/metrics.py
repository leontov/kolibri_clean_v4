"""Runtime SLO instrumentation utilities."""
from __future__ import annotations

import json
from collections import deque
from contextlib import contextmanager
from dataclasses import dataclass, field
from statistics import median
from time import perf_counter
from typing import Deque, Dict, Iterator, Mapping, MutableMapping, Optional, Sequence


def _quantile(sorted_samples: Sequence[float], percentile: float) -> float:
    if not sorted_samples:
        return 0.0
    if percentile <= 0.0:
        return sorted_samples[0]
    if percentile >= 1.0:
        return sorted_samples[-1]
    position = percentile * (len(sorted_samples) - 1)
    lower = int(position)
    upper = min(lower + 1, len(sorted_samples) - 1)
    weight = position - lower
    return sorted_samples[lower] * (1.0 - weight) + sorted_samples[upper] * weight


@dataclass
class SLOWindow:
    """Sliding window of latency samples for a single stage."""

    limit: int = 200
    samples: Deque[float] = field(default_factory=deque)

    def observe(self, value: float) -> None:
        self.samples.append(value)
        if len(self.samples) > self.limit:
            self.samples.popleft()

    def snapshot(self) -> Mapping[str, float]:
        if not self.samples:
            return {"count": 0.0, "p50": 0.0, "p95": 0.0, "p99": 0.0}
        ordered = sorted(self.samples)
        return {
            "count": float(len(self.samples)),
            "p50": median(ordered),
            "p95": _quantile(ordered, 0.95),
            "p99": _quantile(ordered, 0.99),
        }


class SLOTracker:
    """Aggregates latency samples for runtime pipeline stages."""

    def __init__(
        self,
        *,
        window: int = 200,
        thresholds: Optional[Mapping[str, float]] = None,
        default_threshold: Optional[float] = 750.0,
    ) -> None:
        self._window_size = window
        self._stages: Dict[str, SLOWindow] = {}
        self._thresholds: Dict[str, float] = {
            stage: float(limit) for stage, limit in (thresholds or {}).items()
        }
        self._default_threshold = float(default_threshold) if default_threshold is not None else None

    def observe(self, stage: str, value: float) -> None:
        window = self._stages.setdefault(stage, SLOWindow(limit=self._window_size))
        window.observe(value)

    def report(self) -> Mapping[str, Mapping[str, float]]:
        return {stage: window.snapshot() for stage, window in self._stages.items()}

    def configure_threshold(self, stage: str, *, p95_limit: float) -> None:
        """Set SLA threshold for the specified stage (measured against p95)."""

        self._thresholds[stage] = float(p95_limit)

    def build_report(self) -> Mapping[str, object]:
        """Return structured report with snapshot and SLA evaluation."""

        snapshot = self.report()
        evaluation: MutableMapping[str, object] = {
            "stages": snapshot,
            "thresholds": dict(self._thresholds),
        }
        breaches = self._detect_breaches(snapshot)
        if breaches:
            evaluation["breaches"] = breaches
        if self._default_threshold is not None:
            evaluation["default_threshold"] = self._default_threshold
        return evaluation

    def export_json(self) -> str:
        """Serialise the current SLO report into a canonical JSON string."""

        return json.dumps(self.build_report(), ensure_ascii=False, indent=2, sort_keys=True)

    def _detect_breaches(self, snapshot: Mapping[str, Mapping[str, float]]) -> Mapping[str, Mapping[str, float]]:
        breaches: Dict[str, Mapping[str, float]] = {}
        for stage, stats in snapshot.items():
            threshold = self._thresholds.get(stage, self._default_threshold)
            if threshold is None:
                continue
            actual = float(stats.get("p95", 0.0))
            if actual > threshold:
                breaches[stage] = {"p95": actual, "limit": threshold}
        return breaches

    @contextmanager
    def time_stage(self, stage: str) -> Iterator[None]:
        start = perf_counter()
        try:
            yield
        finally:
            elapsed = (perf_counter() - start) * 1000.0
            self.observe(stage, elapsed)


__all__ = ["SLOTracker", "SLOWindow"]
