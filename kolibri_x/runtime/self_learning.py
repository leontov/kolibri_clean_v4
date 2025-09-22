"""Background self-learning utilities for Kolibri runtime."""
from __future__ import annotations

import json
from collections import deque
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Deque, Dict, Mapping, MutableMapping, Sequence

from kolibri_x.core.encoders import ContinualLearner
from kolibri_x.personalization.federated import ModelUpdate, SecureAggregator


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
    ) -> None:
        if clipping <= 0.0:
            raise ValueError("clipping must be positive")
        if min_weight <= 0.0:
            raise ValueError("min_weight must be positive")
        if history_size <= 0:
            raise ValueError("history_size must be positive")
        if sample_limit <= 0:
            raise ValueError("sample_limit must be positive")

        self.learner = learner or ContinualLearner()
        self._noise_scale = noise_scale
        self._clipping = clipping
        self._min_weight = min_weight
        self._aggregators: Dict[str, SecureAggregator] = {}
        self._pending_counts: MutableMapping[str, int] = {}
        self._history: Deque[Mapping[str, object]] = deque(maxlen=history_size)
        self._samples: Deque[SelfLearningSample] = deque(maxlen=sample_limit)

    def save(self, path: str | Path) -> None:
        """Persist learner state, aggregators, and history to a JSON file."""

        target = Path(path).expanduser()
        if target.parent and not target.parent.exists():
            target.parent.mkdir(parents=True, exist_ok=True)

        data = {
            "config": {
                "noise_scale": self._noise_scale,
                "clipping": self._clipping,
                "min_weight": self._min_weight,
                "history_size": self._history.maxlen,
                "sample_limit": self._samples.maxlen,
                "consolidation": self.learner.consolidation,
            },
            "aggregators": {
                task: {
                    "noise_scale": aggregator.noise_scale,
                    "sums": dict(aggregator._sums),  # type: ignore[attr-defined]
                    "counts": dict(aggregator._counts),  # type: ignore[attr-defined]
                }
                for task, aggregator in self._aggregators.items()
            },
            "pending_counts": dict(self._pending_counts),
            "history": [dict(entry) for entry in self._history],
            "samples": [
                {
                    "task_id": sample.task_id,
                    "gradients": dict(sample.gradients),
                    "confidence": float(sample.confidence),
                    "metadata": dict(sample.metadata),
                    "user_id": sample.user_id,
                    "timestamp": sample.timestamp.isoformat(),
                }
                for sample in self._samples
            ],
            "learner": self.learner.snapshot(),
        }

        serialized = json.dumps(data, ensure_ascii=False, indent=2)
        target.write_text(serialized, encoding="utf-8")

    def load(self, path: str | Path) -> None:
        """Restore learner state, aggregators, and history from a JSON file."""

        target = Path(path).expanduser()
        if not target.exists():
            return

        raw = json.loads(target.read_text(encoding="utf-8"))
        config = raw.get("config", {})
        self._noise_scale = float(config.get("noise_scale", self._noise_scale))
        self._clipping = float(config.get("clipping", self._clipping))
        self._min_weight = float(config.get("min_weight", self._min_weight))

        history_size = int(config.get("history_size", self._history.maxlen or 0) or 0)
        if history_size > 0 and history_size != self._history.maxlen:
            self._history = deque(maxlen=history_size)
        else:
            self._history.clear()

        sample_limit = int(config.get("sample_limit", self._samples.maxlen or 0) or 0)
        if sample_limit > 0 and sample_limit != self._samples.maxlen:
            self._samples = deque(maxlen=sample_limit)
        else:
            self._samples.clear()

        self._history.extend(dict(entry) for entry in raw.get("history", []))

        fromiso = datetime.fromisoformat
        for item in raw.get("samples", []):
            timestamp_str = item.get("timestamp")
            timestamp = fromiso(timestamp_str) if timestamp_str else datetime.now(timezone.utc)
            sample = SelfLearningSample(
                task_id=str(item.get("task_id", "")),
                gradients=dict(item.get("gradients", {})),
                confidence=float(item.get("confidence", 0.0)),
                metadata=dict(item.get("metadata", {})),
                user_id=str(item.get("user_id", "anonymous")),
                timestamp=timestamp,
            )
            self._samples.append(sample)

        self._pending_counts = {
            str(task_id): int(count)
            for task_id, count in raw.get("pending_counts", {}).items()
        }

        self._aggregators = {}
        for task_id, payload in raw.get("aggregators", {}).items():
            aggregator = SecureAggregator(
                noise_scale=float(payload.get("noise_scale", self._noise_scale))
            )
            aggregator._sums.update({  # type: ignore[attr-defined]
                str(name): float(value)
                for name, value in payload.get("sums", {}).items()
            })
            aggregator._counts.update({  # type: ignore[attr-defined]
                str(name): int(value)
                for name, value in payload.get("counts", {}).items()
            })
            self._aggregators[str(task_id)] = aggregator

        learner_config = raw.get("learner", {})
        weights = {
            str(task): {str(name): float(value) for name, value in mapping.items()}
            for task, mapping in learner_config.get("weights", {}).items()
        }
        self.learner.consolidation = float(
            config.get("consolidation", self.learner.consolidation)
        )
        self.learner._weights = weights  # type: ignore[attr-defined]

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
        }
        self._history.append(entry)
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
        }

    def recent_samples(self, limit: int = 5) -> Sequence[SelfLearningSample]:
        if limit <= 0:
            return tuple()
        return tuple(list(self._samples)[-limit:])


__all__ = ["BackgroundSelfLearner", "SelfLearningSample"]
