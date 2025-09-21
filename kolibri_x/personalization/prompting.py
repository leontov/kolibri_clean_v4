"""Adaptive prompt example selection tuned to user cognition."""
from __future__ import annotations

from typing import Mapping, Sequence, Tuple

from .profile import EmotionalProfile, EmotionalSnapshot, UserProfile


class AdaptivePromptSelector:
    """Chooses prompt examples that match the user's cognitive preferences."""

    def __init__(self, *, diversity_bias: float = 0.2) -> None:
        self.diversity_bias = max(0.0, diversity_bias)

    def select_examples(
        self,
        profile: UserProfile,
        goal: str,
        examples: Sequence[Mapping[str, object]],
        emotional: EmotionalProfile | None = None,
        *,
        limit: int = 3,
    ) -> Tuple[Mapping[str, object], ...]:
        if not examples:
            return ()
        emotional = emotional or self._infer_emotional(profile)
        goal_lower = goal.lower()
        scored: list[tuple[float, Mapping[str, object]]] = []
        seen_modalities: set[str] = set()
        for example in examples:
            tags = {tag.lower() for tag in example.get("tags", [])}
            modality = str(example.get("modality", "text")).lower()
            difficulty = float(example.get("difficulty", 0.5))
            alignment = 0.0
            for tag, weight in profile.cognitive_preferences.items():
                if tag.lower() in tags:
                    alignment += weight
            if goal_lower:
                alignment += sum(0.3 for tag in tags if tag in goal_lower)
            sentiment_adjustment = 0.0
            if emotional.short_term.sentiment < -0.1 and "supportive" in tags:
                sentiment_adjustment += 0.5
            if emotional.trend.get("arousal", 0.0) > 0.2 and "concise" in tags:
                sentiment_adjustment += 0.2
            modality_match = emotional.modality_mix.get(modality, 0.0)
            novelty_penalty = self.diversity_bias if modality in seen_modalities else 0.0
            tempo_penalty = abs(
                float(example.get("pace", 1.0)) - profile.tempo_preference
            ) * 0.3
            difficulty_bias = -abs(
                difficulty - profile.cognitive_preferences.get("difficulty", 0.5)
            )
            score = (
                alignment
                + sentiment_adjustment
                + modality_match
                + difficulty_bias
                - tempo_penalty
                - novelty_penalty
            )
            scored.append((score, example))
            seen_modalities.add(modality)
        scored.sort(key=lambda item: item[0], reverse=True)
        limited = [example for _, example in scored[: max(limit, 1)]]
        # Ensure at least one example even if all scores were negative.
        if not limited:
            limited = [scored[0][1]]
        return tuple(limited)

    def _infer_emotional(self, profile: UserProfile) -> EmotionalProfile:
        history = list(profile.emotion_history)
        short_term = EmotionalSnapshot.average(history[-6:])
        long_term = EmotionalSnapshot.average(history)
        trend = {
            "sentiment": short_term.sentiment - profile.emotion_baseline.sentiment,
            "arousal": short_term.arousal - profile.emotion_baseline.arousal,
            "dominance": short_term.dominance - profile.emotion_baseline.dominance,
        }
        total = sum(profile.modality_exposure.values()) or 1.0
        modality_mix = {
            modality: value / total
            for modality, value in profile.modality_exposure.items()
        }
        return EmotionalProfile(
            baseline=profile.emotion_baseline,
            short_term=short_term,
            long_term=long_term,
            trend=trend,
            modality_mix=modality_mix,
            recent_signals=list(profile.interaction_history)[-10:],
        )


__all__ = ["AdaptivePromptSelector"]
