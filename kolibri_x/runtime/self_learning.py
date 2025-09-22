"""Background self-learning utilities for Kolibri runtime."""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Deque, Dict, Mapping, MutableMapping, Optional, Sequence

from kolibri_x.core.encoders import ContinualLearner
from kolibri_x.personalization.federated import ModelUpdate, SecureAggregator
from kolibri_x.runtime.journal import ActionJournal


def _clamp(value: float, minimum: float = 0.0, maximum: float = 1.0) -> float:
    if value < minimum:
        return minimum
    if value > maximum:
        return maximum
    return value


@dataclass
class SelfLearningSample:
    """Single training signal captured for background learning."""

    task_id: str
    gradients: Mapping[str, float]
    confidence: float
    metadata: Mapping[str, str] = field(default_factory=dict)
    user_id: str = "anonymous"
    timestamp: datetime = field(default_factory=lambda: datetime.now(timezone.utc))


class BackgroundSelfLearner:
    """Aggregates weak supervision signals and updates a continual learner."""

    def __init__(
        self,
        learner: ContinualLearner | None = None,
        *,
        noise_scale: float = 0.0,
        clipping: float = 1.0,
        min_weight: float = 0.05,
        history_size: int = 32,
        sample_limit: int = 256,
        drift_alpha: float = 0.2,
        drift_threshold: float = 0.6,
        journal: Optional[ActionJournal] = None,
    ) -> None:
        if clipping <= 0.0:
            raise ValueError("clipping must be positive")
        if min_weight <= 0.0:
            raise ValueError("min_weight must be positive")
        if history_size <= 0:
            raise ValueError("history_size must be positive")
        if sample_limit <= 0:
            raise ValueError("sample_limit must be positive")
        if not 0.0 < drift_alpha <= 1.0:
            raise ValueError("drift_alpha must be within (0, 1]")
        if not 0.0 <= drift_threshold <= 1.0:
            raise ValueError("drift_threshold must be within [0, 1]")

        self.learner = learner or ContinualLearner()
        self._noise_scale = noise_scale
        self._clipping = clipping
        self._min_weight = min_weight
        self._aggregators: Dict[str, SecureAggregator] = {}
        self._pending_counts: MutableMapping[str, int] = {}
        self._history: Deque[Mapping[str, object]] = deque(maxlen=history_size)
        self._samples: Deque[SelfLearningSample] = deque(maxlen=sample_limit)
        self._drift_alpha = drift_alpha
        self._drift_threshold = drift_threshold
        self._drift: Dict[str, float] = {}
        self._journal = journal

    def enqueue(
        self,
        task_id: str,
        gradients: Mapping[str, float],
        *,
        confidence: float = 0.5,
        metadata: Mapping[str, str] | None = None,
        user_id: str | None = None,
    ) -> None:
        """Stores a training signal to be processed in the background."""

        if not gradients:
            return
        weight = max(self._min_weight, 1.0 - _clamp(float(confidence)))
        scaled: Dict[str, float] = {}
        for name, value in gradients.items():
            scaled[name] = float(value) * weight
        aggregator = self._aggregators.setdefault(
            task_id, SecureAggregator(noise_scale=self._noise_scale)
        )
        update = ModelUpdate(user_id=user_id or "anonymous", values=scaled, clipping=self._clipping)
        aggregator.submit(update)
        sample = SelfLearningSample(
            task_id=task_id,
            gradients=dict(gradients),
            confidence=_clamp(float(confidence)),
            metadata=dict(metadata or {}),
            user_id=user_id or "anonymous",
        )
        self._samples.append(sample)
        self._pending_counts[task_id] = self._pending_counts.get(task_id, 0) + 1
        self._update_drift(task_id, sample.metadata.get("status"))

    def step(self) -> Mapping[str, Mapping[str, float]]:
        """Aggregates pending updates and refreshes learner weights."""

        updates: Dict[str, Mapping[str, float]] = {}
        for task_id, aggregator in self._aggregators.items():
            pending = self._pending_counts.get(task_id, 0)
            if pending <= 0:
                continue
            aggregated = aggregator.aggregate()
            self._pending_counts[task_id] = 0
            if not aggregated:
                continue
            trained = self.learner.train(task_id, aggregated)
            updates[task_id] = trained
        entry = {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "updates": {task: dict(values) for task, values in updates.items()},
            "pending": dict(self._pending_counts),
            "drift": dict(self._drift),
            "samples": len(self._samples),
        }
        self._history.append(entry)
        self._publish_report(entry)
        return updates

    def history(self, limit: int = 5) -> Sequence[Mapping[str, object]]:
        if limit <= 0:
            return tuple()
        items = list(self._history)[-limit:]
        return tuple(dict(item) for item in items)

    def status(self) -> Mapping[str, object]:
        return {
            "tasks": sorted(self._aggregators.keys()),
            "pending": dict(self._pending_counts),
            "history": list(self.history()),
            "drift": dict(self._drift),
        }

    def recent_samples(self, limit: int = 5) -> Sequence[SelfLearningSample]:
        if limit <= 0:
            return tuple()
        return tuple(list(self._samples)[-limit:])

    def drift_scores(self) -> Mapping[str, float]:
        return dict(self._drift)

    def degraded_tasks(self) -> Mapping[str, float]:
        return {task: score for task, score in self._drift.items() if score >= self._drift_threshold}

    @property
    def drift_threshold(self) -> float:
        return self._drift_threshold

    def bind_journal(self, journal: Optional[ActionJournal]) -> None:
        self._journal = journal

    def _update_drift(self, task_id: str, status: object) -> None:
        signal = self._error_signal(status)
        previous = self._drift.get(task_id)
        if previous is None:
            updated = signal
        else:
            updated = previous + self._drift_alpha * (signal - previous)
        self._drift[task_id] = _clamp(updated)

    def _error_signal(self, status: object) -> float:
        label = str(status or "unknown").lower()
        if label in {"ok", "cached", "success"}:
            return 0.0
        if label in {"skipped", "noop"}:
            return 0.1
        return 1.0

    def _publish_report(self, entry: Mapping[str, object]) -> None:
        if not self._journal:
            return
        payload = {
            "timestamp": entry["timestamp"],
            "drift": dict(entry.get("drift", {})),
            "pending": dict(entry.get("pending", {})),
            "tasks": sorted(entry.get("updates", {}).keys()),
            "samples": entry.get("samples", 0),
        }
        self._journal.append("self_learning_report", payload)


__all__ = ["BackgroundSelfLearner", "SelfLearningSample"]
