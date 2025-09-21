"""Personalisation helpers for the Kolibri runtime."""
from __future__ import annotations

from dataclasses import dataclass, field
from statistics import fmean
from typing import Dict, Iterable, List, Mapping, MutableMapping, Optional, Sequence


@dataclass
class InteractionSignal:
    type: str
    value: float
    weight: float = 1.0


@dataclass
class EmpathyContext:
    sentiment: float = 0.0
    urgency: float = 0.0
    energy: float = 0.0


@dataclass
class Achievement:
    identifier: str
    description: str


@dataclass
class UserProfile:
    tone_preference: float = 0.0
    tempo_bias: float = 1.0
    formality_bias: float = 0.0
    achievements: List[Achievement] = field(default_factory=list)

    def as_dict(self) -> Mapping[str, object]:
        return {
            "tone_preference": self.tone_preference,
            "tempo_bias": self.tempo_bias,
            "formality_bias": self.formality_bias,
            "achievements": [achievement.identifier for achievement in self.achievements],
        }


class OnDeviceProfiler:
    """Aggregates interaction signals into a compact profile."""

    def __init__(self, decay: float = 0.8) -> None:
        self.decay = decay
        self._profiles: MutableMapping[str, UserProfile] = {}

    def bulk_record(self, user_id: str, signals: Sequence[InteractionSignal]) -> UserProfile:
        profile = self._profiles.setdefault(user_id, UserProfile())
        for signal in signals:
            weight = max(signal.weight, 0.0)
            if signal.type == "tone":
                profile.tone_preference = self._blend(profile.tone_preference, signal.value, weight)
            elif signal.type == "tempo":
                profile.tempo_bias = self._blend(profile.tempo_bias, 1.0 + signal.value, weight)
            elif signal.type == "formality":
                profile.formality_bias = self._blend(profile.formality_bias, signal.value, weight)
        self._profiles[user_id] = profile
        return profile

    def unlock(self, user_id: str, achievement: Achievement) -> None:
        profile = self._profiles.setdefault(user_id, UserProfile())
        if achievement.identifier not in {item.identifier for item in profile.achievements}:
            profile.achievements.append(achievement)

    def profile(self, user_id: str) -> UserProfile:
        return self._profiles.setdefault(user_id, UserProfile())

    def _blend(self, current: float, incoming: float, weight: float) -> float:
        alpha = min(1.0, self.decay * max(weight, 0.0))
        return (1.0 - alpha) * current + alpha * incoming


class EmpathyModulator:
    """Produces tone and tempo adjustments from context and profile."""

    def modulation(self, profile: UserProfile, context: EmpathyContext) -> Mapping[str, float]:
        tone = profile.tone_preference + 0.5 * context.sentiment - 0.2 * context.urgency
        tempo = max(0.2, min(3.0, profile.tempo_bias + context.energy))
        formality = max(-1.0, min(1.0, profile.formality_bias + context.sentiment * 0.3))
        return {
            "tone": tone,
            "tempo": tempo,
            "style::formality": formality,
        }


class AdaptivePromptSelector:
    """Chooses examples matching profile preferences."""

    def select_examples(
        self,
        profile: UserProfile,
        goal: str,
        examples: Sequence[Mapping[str, object]],
    ) -> Sequence[Mapping[str, object]]:
        if not examples:
            return ()
        scored = []
        for example in examples:
            tags = set(example.get("tags", []))
            score = 0.0
            if "pitch" in tags and "pitch" in goal.lower():
                score += 1.0
            score += abs(profile.tone_preference)
            scored.append((score, example))
        scored.sort(key=lambda item: item[0], reverse=True)
        top_score = scored[0][0]
        return tuple(example for score, example in scored if score >= top_score * 0.5)


@dataclass
class ModelUpdate:
    user_id: str
    values: Mapping[str, float]
    clipping: float = 1.0


class SecureAggregator:
    """Aggregates clipped model updates."""

    def __init__(self) -> None:
        self._updates: List[ModelUpdate] = []

    def submit(self, update: ModelUpdate) -> None:
        self._updates.append(update)

    def aggregate(self) -> Mapping[str, float]:
        if not self._updates:
            return {}
        accumulator: Dict[str, List[float]] = {}
        for update in self._updates:
            limit = max(update.clipping, 1e-6)
            for name, value in update.values.items():
                clipped = max(-limit, min(limit, float(value)))
                accumulator.setdefault(name, []).append(clipped)
        return {name: fmean(values) / 2.0 for name, values in accumulator.items()}


class MotivationEngine:
    """Returns lightweight motivational suggestions."""

    def recommend(self, profile: UserProfile) -> Sequence[str]:
        suggestions = ["Take a short break", "Review recent wins"]
        if profile.achievements:
            suggestions.append(f"Celebrate {profile.achievements[-1].description}")
        return suggestions


__all__ = [
    "Achievement",
    "AdaptivePromptSelector",
    "EmpathyContext",
    "EmpathyModulator",
    "InteractionSignal",
    "ModelUpdate",
    "MotivationEngine",
    "OnDeviceProfiler",
    "SecureAggregator",
    "UserProfile",
]
