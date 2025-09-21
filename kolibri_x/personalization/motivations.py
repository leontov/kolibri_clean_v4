"""Achievements, motivational triggers, and wellness recommendations."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Mapping, MutableMapping, Sequence

from .profile import Achievement, EmotionalProfile, EmotionalSnapshot, UserProfile


@dataclass
class GoalDefinition:
    """Represents a measurable objective for the user."""

    identifier: str
    target_value: float
    description: str


class MotivationEngine:
    """Evaluates user progress and surfaces motivational triggers."""

    def __init__(self, goals: Iterable[GoalDefinition] | None = None) -> None:
        self._goals: MutableMapping[str, GoalDefinition] = {
            goal.identifier: goal for goal in (goals or [])
        }

    def register_goal(self, goal: GoalDefinition) -> None:
        self._goals[goal.identifier] = goal

    def evaluate_progress(
        self,
        profile: UserProfile,
        emotional: EmotionalProfile,
        progress: Mapping[str, float],
    ) -> tuple[Sequence[Achievement], Sequence[str]]:
        """Returns achievements to unlock and motivational nudges."""

        unlocked: list[Achievement] = []
        triggers: list[str] = []
        for identifier, goal in self._goals.items():
            current = progress.get(identifier, 0.0)
            if goal.target_value <= 0:
                continue
            ratio = current / goal.target_value
            if ratio >= 1.0 and identifier not in profile.achievements:
                unlocked.append(
                    Achievement(
                        identifier=identifier,
                        description=goal.description,
                    )
                )
            elif ratio >= 0.6:
                delta = 1.0 - ratio
                tone_hint = "поддерживающим" if emotional.short_term.sentiment < 0 else "энергичным"
                triggers.append(
                    f"До цели «{goal.description}» осталось {delta:.0%}. Предлагаю шаг с {tone_hint} тоном."
                )
        if emotional.trend.get("sentiment", 0.0) < -0.1:
            triggers.append(
                "Замечаю снижение настроения. Сделаем паузу и отметим прогресс за последнюю неделю."
            )
        if emotional.trend.get("arousal", 0.0) > 0.2:
            triggers.append(
                "Темп высокий — предлагаю короткую растяжку или дыхательное упражнение перед продолжением."
            )
        return unlocked, triggers

    def recommend(
        self, profile: UserProfile, emotional: EmotionalProfile | None = None
    ) -> Sequence[str]:
        emotional = emotional or self._infer_emotional(profile)
        _, triggers = self.evaluate_progress(profile, emotional, {})
        if triggers:
            return triggers
        return (
            "Продолжайте, прогресс фиксируется — при необходимости предложу новую цель.",
        )

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


class WellnessAdvisor:
    """Produces health-oriented load and time management suggestions."""

    def recommend(
        self,
        profile: UserProfile,
        emotional: EmotionalProfile,
        metrics: Mapping[str, float] | None = None,
    ) -> Sequence[str]:
        metrics = dict(metrics or {})
        workload = metrics.get("workload_minutes", 0.0)
        focus_ratio = metrics.get("focus_ratio", 0.5)
        breaks = metrics.get("breaks_taken", 0.0)
        sleep_hours = metrics.get("sleep_hours", 7.0)
        suggestions: list[str] = []
        if workload > 90 or emotional.short_term.arousal > 0.7:
            suggestions.append("Сделайте паузу на 5 минут и переключитесь на дыхание 4-7-8.")
        if breaks < max(workload / 60.0, 1.0):
            suggestions.append(
                "Запланируйте микропаузу каждые 45 минут, я напомню уведомлением."
            )
        if focus_ratio < 0.4:
            suggestions.append(
                "Попробуйте сессию Pomodoro: 25 минут работы и 5 минут отдыха."
            )
        if sleep_hours < 7:
            suggestions.append(
                "Рекомендую завершить активные задачи за 60 минут до сна и перейти в режим «тихий вечер»."
            )
        if not suggestions:
            suggestions.append(
                "Баланс нагрузки оптимальный — можно запланировать лёгкое обучение или креативную задачу."
            )
        return suggestions


__all__ = ["GoalDefinition", "MotivationEngine", "WellnessAdvisor"]
