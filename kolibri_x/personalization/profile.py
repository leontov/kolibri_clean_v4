"""On-device user profiler with privacy-preserving summaries."""
from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Dict, Iterable, Mapping, MutableMapping, Optional


@dataclass
class InteractionSignal:
    """Represents a single behavioural observation used for profiling."""

    type: str
    value: float
    weight: float = 1.0
    timestamp: datetime = field(default_factory=lambda: datetime.now(timezone.utc))


@dataclass
class UserProfile:
    """Aggregated preferences inferred from local interaction signals."""

    user_id: str
    style_vector: MutableMapping[str, float] = field(default_factory=dict)
    tempo_preference: float = 1.0
    tone_preference: float = 0.0
    last_updated: datetime = field(default_factory=lambda: datetime.now(timezone.utc))

    def as_dict(self) -> Mapping[str, float | str]:
        return {
            "user_id": self.user_id,
            "tempo_preference": self.tempo_preference,
            "tone_preference": self.tone_preference,
            "style_vector": dict(self.style_vector),
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
        else:
            previous = profile.style_vector.get(signal.type, 0.0)
            profile.style_vector[signal.type] = self._blend(previous, signal.value, weight)
        return profile

    def bulk_record(self, user_id: str, signals: Iterable[InteractionSignal]) -> UserProfile:
        profile: Optional[UserProfile] = None
        for signal in signals:
            profile = self.record(user_id, signal)
        return profile or self._profile(user_id)

    def export_profile(self, user_id: str) -> Mapping[str, float | str]:
        return self._profile(user_id).as_dict()

    def profiles(self) -> Mapping[str, UserProfile]:
        return dict(self._profiles)

    def _blend(self, previous: float, value: float, weight: float) -> float:
        alpha = min(max(weight, 0.0), 10.0)
        blend = alpha / (alpha + 1.0)
        return previous * self._decay * (1.0 - blend) + value * blend


__all__ = ["InteractionSignal", "OnDeviceProfiler", "UserProfile"]
