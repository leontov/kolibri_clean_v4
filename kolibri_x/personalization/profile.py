"""On-device profiling and multimodal emotional modelling."""
from __future__ import annotations

from collections import Counter, deque
from dataclasses import dataclass, field, replace
from datetime import datetime, timedelta, timezone
from statistics import fmean
from typing import Deque, Dict, Iterable, Mapping, MutableMapping, Sequence


def _utc_now() -> datetime:
    return datetime.now(timezone.utc)


@dataclass
class InteractionSignal:
    """Raw behavioural observation collected during an interaction."""

    modality: str = "text"
    metric: str | None = None
    value: float = 0.0
    confidence: float = 1.0
    weight: float | None = None
    type: str | None = None
    metadata: Mapping[str, object] = field(default_factory=dict)
    timestamp: datetime = field(default_factory=_utc_now)

    def __post_init__(self) -> None:
        if self.metric is None and self.type is not None:
            self.metric = self.type
        elif self.metric is not None and self.type is None:
            self.type = self.metric
        if self.metric is None:
            raise ValueError("metric must be provided via 'metric' or legacy 'type'")
        if self.weight is not None:
            self.confidence = self.weight
        self.weight = self.confidence


@dataclass
class EmotionalSnapshot:
    """Single emotional reading captured at a point in time."""

    sentiment: float = 0.0
    arousal: float = 0.0
    dominance: float = 0.0
    timestamp: datetime = field(default_factory=_utc_now)

    @classmethod
    def average(cls, snapshots: Sequence["EmotionalSnapshot"]) -> "EmotionalSnapshot":
        if not snapshots:
            return cls()
        return cls(
            sentiment=fmean(snapshot.sentiment for snapshot in snapshots),
            arousal=fmean(snapshot.arousal for snapshot in snapshots),
            dominance=fmean(snapshot.dominance for snapshot in snapshots),
            timestamp=snapshots[-1].timestamp,
        )

    def as_dict(self) -> Mapping[str, float | str]:
        return {
            "sentiment": self.sentiment,
            "arousal": self.arousal,
            "dominance": self.dominance,
            "timestamp": self.timestamp.isoformat(),
        }


@dataclass
class EmotionalProfile:
    """Aggregated emotional state constructed from history and signals."""

    baseline: EmotionalSnapshot
    short_term: EmotionalSnapshot
    long_term: EmotionalSnapshot
    trend: Mapping[str, float]
    modality_mix: Mapping[str, float]
    recent_signals: Sequence[InteractionSignal]

    def as_dict(self) -> Mapping[str, object]:
        return {
            "baseline": self.baseline.as_dict(),
            "short_term": self.short_term.as_dict(),
            "long_term": self.long_term.as_dict(),
            "trend": dict(self.trend),
            "modality_mix": dict(self.modality_mix),
            "recent_signals": [
                {
                    "modality": signal.modality,
                    "metric": signal.metric,
                    "value": signal.value,
                    "confidence": signal.confidence,
                    "timestamp": signal.timestamp.isoformat(),
                }
                for signal in self.recent_signals
            ],
        }


@dataclass
class Achievement:
    """Represents a long-lived motivational milestone."""

    identifier: str
    description: str
    unlocked_at: datetime = field(default_factory=_utc_now)

    def as_dict(self) -> Mapping[str, str]:
        return {
            "identifier": self.identifier,
            "description": self.description,
            "unlocked_at": self.unlocked_at.isoformat(),
        }


@dataclass
class UserProfile:
    """Aggregated preferences and emotional signals for a user."""

    user_id: str
    tone_preference: float = 0.0
    tempo_preference: float = 1.0
    formality_bias: float = 0.0
    response_length_bias: float = 0.0
    style_preferences: MutableMapping[str, float] = field(default_factory=dict)
    cognitive_preferences: MutableMapping[str, float] = field(default_factory=dict)
    achievements: MutableMapping[str, Achievement] = field(default_factory=dict)
    emotion_baseline: EmotionalSnapshot = field(default_factory=EmotionalSnapshot)
    current_emotion: EmotionalSnapshot = field(default_factory=EmotionalSnapshot)
    emotion_history: Deque[EmotionalSnapshot] = field(
        default_factory=lambda: deque(maxlen=96)
    )
    modality_exposure: Counter[str] = field(default_factory=Counter)
    interaction_history: Deque[InteractionSignal] = field(
        default_factory=lambda: deque(maxlen=256)
    )
    last_updated: datetime = field(default_factory=_utc_now)

    def as_dict(self) -> Mapping[str, object]:
        return {
            "user_id": self.user_id,
            "tone_preference": self.tone_preference,
            "tempo_preference": self.tempo_preference,
            "formality_bias": self.formality_bias,
            "response_length_bias": self.response_length_bias,
            "style_preferences": dict(self.style_preferences),
            "cognitive_preferences": dict(self.cognitive_preferences),
            "emotion_baseline": self.emotion_baseline.as_dict(),
            "achievements": {
                identifier: achievement.description
                for identifier, achievement in self.achievements.items()
            },
            "last_updated": self.last_updated.isoformat(),
        }


class OnDeviceProfiler:
    """Local-only profiler combining multimodal signals into a user model."""

    def __init__(self, *, decay: float = 0.85, short_window: int = 6) -> None:
        if not 0 < decay <= 1:
            raise ValueError("decay must be within (0, 1]")
        self._decay = decay
        self._short_window = max(short_window, 1)
        self._profiles: Dict[str, UserProfile] = {}

    def profile(self, user_id: str) -> UserProfile:
        return self._profiles.setdefault(user_id, UserProfile(user_id=user_id))

    def record(self, user_id: str, signal: InteractionSignal) -> UserProfile:
        profile = self.profile(user_id)
        profile.last_updated = signal.timestamp
        profile.modality_exposure[signal.modality] += max(signal.confidence, 0.0)
        profile.interaction_history.append(signal)
        metric = signal.metric
        if metric == "tone":
            profile.tone_preference = self._blend(
                profile.tone_preference, signal.value, signal.confidence
            )
        elif metric == "tempo":
            profile.tempo_preference = self._blend(
                profile.tempo_preference, signal.value, signal.confidence
            )
        elif metric == "formality":
            profile.formality_bias = self._blend(
                profile.formality_bias, signal.value, signal.confidence
            )
        elif metric == "length":
            profile.response_length_bias = self._blend(
                profile.response_length_bias, signal.value, signal.confidence
            )
        elif metric.startswith("style::"):
            key = metric.split("::", 1)[1]
            previous = profile.style_preferences.get(key, 0.0)
            profile.style_preferences[key] = self._blend(
                previous, signal.value, signal.confidence
            )
        elif metric.startswith("cog::"):
            key = metric.split("::", 1)[1]
            previous = profile.cognitive_preferences.get(key, 0.0)
            profile.cognitive_preferences[key] = self._blend(
                previous, signal.value, signal.confidence
            )
        elif metric.startswith("emotion::"):
            dimension = metric.split("::", 1)[1]
            self._update_emotion(profile, dimension, signal.value, signal.confidence)
        return profile

    def record_many(
        self, user_id: str, signals: Iterable[InteractionSignal]
    ) -> UserProfile:
        profile: UserProfile | None = None
        for signal in signals:
            profile = self.record(user_id, signal)
        return profile or self.profile(user_id)

    def bulk_record(
        self, user_id: str, signals: Iterable[InteractionSignal]
    ) -> UserProfile:
        return self.record_many(user_id, signals)

    def unlock(self, user_id: str, achievement: Achievement) -> None:
        profile = self.profile(user_id)
        profile.achievements[achievement.identifier] = achievement

    def build_emotional_profile(
        self, user_id: str, *, horizon: timedelta | None = None
    ) -> EmotionalProfile:
        profile = self.profile(user_id)
        history = list(profile.emotion_history)
        if horizon is not None:
            cutoff = _utc_now() - horizon
            history = [snapshot for snapshot in history if snapshot.timestamp >= cutoff]
        short_term = EmotionalSnapshot.average(history[-self._short_window :])
        long_term = EmotionalSnapshot.average(history)
        trend = {
            "sentiment": short_term.sentiment - profile.emotion_baseline.sentiment,
            "arousal": short_term.arousal - profile.emotion_baseline.arousal,
            "dominance": short_term.dominance - profile.emotion_baseline.dominance,
        }
        total_exposure = sum(profile.modality_exposure.values()) or 1.0
        modality_mix = {
            modality: exposure / total_exposure
            for modality, exposure in profile.modality_exposure.items()
        }
        recent_signals = list(profile.interaction_history)[-10:]
        return EmotionalProfile(
            baseline=profile.emotion_baseline,
            short_term=short_term,
            long_term=long_term,
            trend=trend,
            modality_mix=modality_mix,
            recent_signals=recent_signals,
        )

    def export_profile(self, user_id: str) -> Mapping[str, object]:
        return self.profile(user_id).as_dict()

    def profiles(self) -> Mapping[str, UserProfile]:
        return dict(self._profiles)

    def _blend(self, previous: float, value: float, confidence: float) -> float:
        alpha = min(max(confidence, 0.0), 10.0)
        blend = alpha / (alpha + 1.0)
        return previous * self._decay * (1.0 - blend) + value * blend

    def _update_emotion(
        self,
        profile: UserProfile,
        dimension: str,
        value: float,
        confidence: float,
    ) -> None:
        baseline = profile.emotion_baseline
        current = profile.current_emotion
        if dimension == "sentiment":
            baseline.sentiment = self._blend(baseline.sentiment, value, confidence)
            current.sentiment = value
        elif dimension == "arousal":
            baseline.arousal = self._blend(baseline.arousal, value, confidence)
            current.arousal = value
        elif dimension == "dominance":
            baseline.dominance = self._blend(baseline.dominance, value, confidence)
            current.dominance = value
        current.timestamp = _utc_now()
        profile.emotion_history.append(replace(current))


__all__ = [
    "Achievement",
    "EmotionalProfile",
    "EmotionalSnapshot",
    "InteractionSignal",
    "OnDeviceProfiler",
    "UserProfile",
]
