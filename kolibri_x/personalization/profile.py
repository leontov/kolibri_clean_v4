
"""On-device profiling, achievements, and adaptive prompt selection."""

"""On-device user profiler with privacy-preserving summaries."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone

from typing import Dict, Iterable, Mapping, MutableMapping, Optional, Sequence

from typing import Dict, Iterable, Mapping, MutableMapping, Optional



@dataclass
class InteractionSignal:
    """Represents a single behavioural observation used for profiling."""

    type: str
    value: float
    weight: float = 1.0
    timestamp: datetime = field(default_factory=lambda: datetime.now(timezone.utc))


@dataclass

class EmotionalSnapshot:
    sentiment: float
    arousal: float
    dominance: float
    timestamp: datetime = field(default_factory=lambda: datetime.now(timezone.utc))


@dataclass
class Achievement:
    identifier: str
    description: str
    unlocked_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))


@dataclass

class UserProfile:
    """Aggregated preferences inferred from local interaction signals."""

    user_id: str
    style_vector: MutableMapping[str, float] = field(default_factory=dict)
    tempo_preference: float = 1.0
    tone_preference: float = 0.0

    emotion_baseline: EmotionalSnapshot = field(
        default_factory=lambda: EmotionalSnapshot(sentiment=0.0, arousal=0.0, dominance=0.0)
    )
    achievements: MutableMapping[str, Achievement] = field(default_factory=dict)
    cognitive_preferences: MutableMapping[str, float] = field(default_factory=dict)
    last_updated: datetime = field(default_factory=lambda: datetime.now(timezone.utc))

    def as_dict(self) -> Mapping[str, object]:

    last_updated: datetime = field(default_factory=lambda: datetime.now(timezone.utc))

    def as_dict(self) -> Mapping[str, float | str]:

        return {
            "user_id": self.user_id,
            "tempo_preference": self.tempo_preference,
            "tone_preference": self.tone_preference,
            "style_vector": dict(self.style_vector),

            "cognitive_preferences": dict(self.cognitive_preferences),
            "emotion_baseline": {
                "sentiment": self.emotion_baseline.sentiment,
                "arousal": self.emotion_baseline.arousal,
                "dominance": self.emotion_baseline.dominance,
                "timestamp": self.emotion_baseline.timestamp.isoformat(),
            },
            "achievements": {key: value.description for key, value in self.achievements.items()},

            "last_updated": self.last_updated.isoformat(),
        }


class OnDeviceProfiler:
    """Simple profiler that keeps all raw data local to the device."""

    def __init__(self, decay: float = 0.85) -> None:
        if not 0 < decay <= 1:
            raise ValueError("decay must be within (0, 1]")
        self._decay = decay
        self._profiles: Dict[str, UserProfile] = {}

    def _profile(self, user_id: str) -> UserProfile:
        return self._profiles.setdefault(user_id, UserProfile(user_id=user_id))

    def record(self, user_id: str, signal: InteractionSignal) -> UserProfile:
        profile = self._profile(user_id)
        profile.last_updated = signal.timestamp
        weight = signal.weight
        if signal.type == "tempo":
            profile.tempo_preference = self._blend(profile.tempo_preference, signal.value, weight)
        elif signal.type == "tone":
            profile.tone_preference = self._blend(profile.tone_preference, signal.value, weight)

        elif signal.type.startswith("emotion::"):
            dimension = signal.type.split("::", 1)[1]
            self._update_emotion(profile, dimension, signal.value)

        else:
            previous = profile.style_vector.get(signal.type, 0.0)
            profile.style_vector[signal.type] = self._blend(previous, signal.value, weight)
        return profile

    def bulk_record(self, user_id: str, signals: Iterable[InteractionSignal]) -> UserProfile:
        profile: Optional[UserProfile] = None
        for signal in signals:
            profile = self.record(user_id, signal)
        return profile or self._profile(user_id)

    def unlock(self, user_id: str, achievement: Achievement) -> None:
        profile = self._profile(user_id)
        profile.achievements[achievement.identifier] = achievement

    def record_cognitive_preference(self, user_id: str, key: str, value: float) -> None:
        profile = self._profile(user_id)
        profile.cognitive_preferences[key] = self._blend(
            profile.cognitive_preferences.get(key, 0.0), value, 1.5
        )

    def export_profile(self, user_id: str) -> Mapping[str, object]:

    def export_profile(self, user_id: str) -> Mapping[str, float | str]:

        return self._profile(user_id).as_dict()

    def profiles(self) -> Mapping[str, UserProfile]:
        return dict(self._profiles)

    def _blend(self, previous: float, value: float, weight: float) -> float:
        alpha = min(max(weight, 0.0), 10.0)
        blend = alpha / (alpha + 1.0)
        return previous * self._decay * (1.0 - blend) + value * blend

    def _update_emotion(self, profile: UserProfile, dimension: str, value: float) -> None:
        snapshot = profile.emotion_baseline
        if dimension == "sentiment":
            snapshot.sentiment = self._blend(snapshot.sentiment, value, 1.0)
        elif dimension == "arousal":
            snapshot.arousal = self._blend(snapshot.arousal, value, 1.0)
        elif dimension == "dominance":
            snapshot.dominance = self._blend(snapshot.dominance, value, 1.0)
        snapshot.timestamp = datetime.now(timezone.utc)


class AdaptivePromptSelector:
    """Chooses examples based on cognitive preferences and goals."""

    def select_examples(
        self,
        profile: UserProfile,
        goal: str,
        examples: Sequence[Mapping[str, object]],
    ) -> Sequence[Mapping[str, object]]:
        if not examples:
            return examples
        goal_l = goal.lower()
        weights = []
        for example in examples:
            tags = example.get("tags", [])
            overlap = len(set(tags) & set(profile.cognitive_preferences))
            alignment = sum(1 for tag in tags if tag in goal_l)
            weights.append((overlap + alignment, example))
        weights.sort(key=lambda item: item[0], reverse=True)
        top = [example for _, example in weights[:3]]
        return top or examples[:1]


class MotivationEngine:
    """Generates personalised nudges and motivational triggers."""

    def __init__(self, thresholds: Optional[Mapping[str, float]] = None) -> None:
        self.thresholds = dict(thresholds or {"tempo": 0.5, "focus": 0.4})

    def recommend(self, profile: UserProfile) -> Mapping[str, str]:
        suggestions: Dict[str, str] = {}
        if profile.tempo_preference < self.thresholds.get("tempo", 0.5):
            suggestions["tempo"] = "Добавить короткие спринты по 25 минут"
        if profile.cognitive_preferences.get("focus", 0.0) < self.thresholds.get("focus", 0.4):
            suggestions["focus"] = "Включить режим без уведомлений на 1 час"
        if not suggestions:
            suggestions["general"] = "Продолжайте, прогресс идёт по плану"
        return suggestions


__all__ = [
    "Achievement",
    "AdaptivePromptSelector",
    "EmotionalSnapshot",
    "InteractionSignal",
    "MotivationEngine",
    "OnDeviceProfiler",
    "UserProfile",
]


__all__ = ["InteractionSignal", "OnDeviceProfiler", "UserProfile"]

