"""Context-aware empathy modulation layer."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Mapping, MutableMapping

from .profile import UserProfile


@dataclass
class EmpathyContext:
    """Signals observed during the current interaction."""

    sentiment: float = 0.0
    urgency: float = 0.0
    energy: float = 0.0


class EmpathyModulator:
    """Computes modulation vectors for tone and tempo adjustments."""

    def __init__(self, base_tone: float = 0.0, base_tempo: float = 1.0) -> None:
        self.base_tone = base_tone
        self.base_tempo = base_tempo

    def modulation(self, profile: UserProfile, context: EmpathyContext) -> Mapping[str, float]:
        adjustments: MutableMapping[str, float] = {}
        tone_bias = profile.tone_preference + context.sentiment * 0.5 - context.urgency * 0.2
        tempo_bias = profile.tempo_preference + context.urgency * 0.4 + context.energy * 0.3
        adjustments["tone"] = self._clamp(self.base_tone + tone_bias, -1.0, 1.0)
        adjustments["tempo"] = self._clamp(self.base_tempo * tempo_bias, 0.2, 3.0)
        for dimension, weight in profile.style_vector.items():
            adjustments[f"style::{dimension}"] = self._clamp(weight + context.energy * 0.1, -1.0, 1.0)
        return adjustments

    def _clamp(self, value: float, lower: float, upper: float) -> float:
        return max(lower, min(upper, value))


__all__ = ["EmpathyContext", "EmpathyModulator"]
