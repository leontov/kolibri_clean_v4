"""Adaptive empathy and communication style controllers."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Mapping, MutableMapping

from .profile import EmotionalProfile, EmotionalSnapshot, UserProfile


@dataclass
class EmpathyContext:
    """Signals observed during the current interaction turn."""

    sentiment: float = 0.0
    urgency: float = 0.0
    energy: float = 0.0
    medium: str = "text"
    turn_index: int = 0
    latency_s: float = 1.0
    topic: str | None = None
    cognitive_load: float = 0.5


@dataclass
class ResponseStyle:
    """Container describing how the assistant should respond right now."""

    tone: float
    tempo: float
    formality: float
    latency_multiplier: float
    acknowledgement: bool
    mirroring_level: float
    hints: Mapping[str, float]

    def as_metadata(self) -> Mapping[str, float | bool]:
        return {
            "tone": self.tone,
            "tempo": self.tempo,
            "formality": self.formality,
            "style::formality": self.formality,
            "latency_multiplier": self.latency_multiplier,
            "acknowledgement": self.acknowledgement,
            "mirroring_level": self.mirroring_level,
            **self.hints,
        }


class AdaptiveCommunicator:
    """Produces tone, tempo and pacing adjustments from context and history."""

    def __init__(self, *, base_tempo: float = 1.0) -> None:
        self.base_tempo = base_tempo

    def derive_style(
        self,
        profile: UserProfile,
        emotional: EmotionalProfile,
        context: EmpathyContext,
    ) -> ResponseStyle:
        baseline = emotional.baseline
        short_term = emotional.short_term
        trend = emotional.trend
        tone = self._clamp(
            profile.tone_preference
            + context.sentiment * 0.35
            + short_term.sentiment * 0.25
            - context.urgency * 0.2
            - max(trend.get("sentiment", 0.0), 0.0) * 0.1,
            -1.0,
            1.0,
        )
        tempo_multiplier = self._clamp(
            profile.tempo_preference
            + context.urgency * 0.45
            + short_term.arousal * 0.25
            + context.energy * 0.2,
            0.4,
            3.0,
        )
        tempo = self._clamp(self.base_tempo * tempo_multiplier, 0.2, 3.0)
        formality = self._clamp(
            profile.formality_bias
            + profile.cognitive_preferences.get("structure", 0.0) * 0.2
            - baseline.dominance * 0.1
            + (1.0 - context.energy) * 0.05,
            -1.0,
            1.0,
        )
        latency_multiplier = self._clamp(
            1.0
            - context.urgency * 0.35
            + max(0.0, 0.6 - short_term.arousal) * 0.25
            + (1.0 - context.latency_s / max(context.latency_s, 1.0)) * 0.05,
            0.5,
            1.5,
        )
        acknowledgement = (
            short_term.sentiment < -0.1
            or context.sentiment < -0.1
            or trend.get("sentiment", 0.0) < -0.05
        )
        mirroring_pref = profile.style_preferences.get("mirroring", 0.5)
        mirroring_level = self._clamp(
            mirroring_pref + context.cognitive_load * 0.2 + short_term.dominance * -0.1,
            0.0,
            1.0,
        )
        hints: MutableMapping[str, float] = {
            "response_length": self._clamp(
                profile.response_length_bias + context.cognitive_load * -0.2, -1.0, 1.0
            ),
            "empathy_boost": self._clamp(-trend.get("sentiment", 0.0), 0.0, 1.0),
            "grounding": self._clamp(
                baseline.dominance + context.cognitive_load * 0.1, 0.0, 1.0
            ),
        }
        if context.medium == "voice":
            hints["prosody::warmth"] = self._clamp(tone + 0.1, -1.0, 1.0)
            hints["prosody::pace"] = tempo
        return ResponseStyle(
            tone=tone,
            tempo=tempo,
            formality=formality,
            latency_multiplier=latency_multiplier,
            acknowledgement=acknowledgement,
            mirroring_level=mirroring_level,
            hints=hints,
        )

    def _clamp(self, value: float, lower: float, upper: float) -> float:
        return max(lower, min(upper, value))


class EmpathyModulator(AdaptiveCommunicator):
    """Backwards-compatible wrapper returning modulation metadata."""

    def __init__(self, *, base_tempo: float = 1.0, short_window: int = 6) -> None:
        super().__init__(base_tempo=base_tempo)
        self.short_window = max(short_window, 1)

    def modulation(
        self, profile: UserProfile, context: EmpathyContext
    ) -> Mapping[str, float | bool]:
        emotional = self._emotional_profile(profile)
        response = self.derive_style(profile, emotional, context)
        return response.as_metadata()

    def _emotional_profile(self, profile: UserProfile) -> EmotionalProfile:
        history = list(profile.emotion_history)
        short_term = EmotionalSnapshot.average(history[-self.short_window :])
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
        return EmotionalProfile(
            baseline=profile.emotion_baseline,
            short_term=short_term,
            long_term=long_term,
            trend=trend,
            modality_mix=modality_mix,
            recent_signals=list(profile.interaction_history)[-10:],
        )


__all__ = ["AdaptiveCommunicator", "EmpathyContext", "EmpathyModulator", "ResponseStyle"]
