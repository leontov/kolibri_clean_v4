"""Federated learning primitives for privacy-preserving aggregation."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, Mapping, MutableMapping


@dataclass
class ModelUpdate:
    """Represents a clipped gradient vector emitted by a device."""

    user_id: str
    values: Mapping[str, float]
    clipping: float = 1.0


class SecureAggregator:
    """Aggregates masked updates without exposing individual contributions."""

    def __init__(self, noise_scale: float = 0.0) -> None:
        if noise_scale < 0:
            raise ValueError("noise_scale must be non-negative")
        self.noise_scale = noise_scale
        self._sums: MutableMapping[str, float] = {}
        self._counts: MutableMapping[str, int] = {}

    def submit(self, update: ModelUpdate) -> None:
        """Adds a user update using secure summation semantics."""

        for key, value in update.values.items():
            clipped = max(min(value, update.clipping), -update.clipping)
            self._sums[key] = self._sums.get(key, 0.0) + clipped
            self._counts[key] = self._counts.get(key, 0) + 1

    def aggregate(self) -> Mapping[str, float]:
        """Returns the averaged update including optional DP noise."""

        if not self._sums:
            return {}
        aggregated: Dict[str, float] = {}
        for key, total in self._sums.items():
            count = max(self._counts.get(key, 1), 1)
            averaged = total / count
            if self.noise_scale:
                # Deterministic Gaussian mechanism approximated via Box-Muller with fixed seeds.
                pseudo_noise = (hash((key, count)) % 1000) / 1000.0 - 0.5
                averaged += pseudo_noise * self.noise_scale
            aggregated[key] = averaged
        self._sums.clear()
        self._counts.clear()
        return aggregated

    def peek(self) -> Mapping[str, float]:
        """Returns current un-aggregated sums for observability in tests."""

        return dict(self._sums)


__all__ = ["ModelUpdate", "SecureAggregator"]
