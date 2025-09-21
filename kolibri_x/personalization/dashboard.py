"""Personalisation dashboard exporter for transparency and control."""
from __future__ import annotations

from typing import Mapping, MutableMapping, Sequence

from .empathy import AdaptiveCommunicator, EmpathyContext
from .motivations import MotivationEngine, WellnessAdvisor
from .profile import OnDeviceProfiler
from .prompting import AdaptivePromptSelector
from .style import WritingStyleLearner
from .family import FamilyModeManager


class PersonalisationDashboard:
    """Builds a transparent snapshot of the personalisation state."""

    def __init__(
        self,
        profiler: OnDeviceProfiler,
        communicator: AdaptiveCommunicator,
        prompt_selector: AdaptivePromptSelector,
        motivation_engine: MotivationEngine,
        wellness_advisor: WellnessAdvisor,
        style_learner: WritingStyleLearner,
        *,
        family_manager: FamilyModeManager | None = None,
    ) -> None:
        self.profiler = profiler
        self.communicator = communicator
        self.prompt_selector = prompt_selector
        self.motivation_engine = motivation_engine
        self.wellness_advisor = wellness_advisor
        self.style_learner = style_learner
        self.family_manager = family_manager

    def snapshot(
        self,
        user_id: str,
        *,
        goal_progress: Mapping[str, float] | None = None,
        session_metrics: Mapping[str, float] | None = None,
        prompt_candidates: Sequence[Mapping[str, object]] | None = None,
        empathy_context: EmpathyContext | None = None,
        family_id: str | None = None,
    ) -> Mapping[str, object]:
        profile = self.profiler.profile(user_id)
        emotional = self.profiler.build_emotional_profile(user_id)
        empathy_context = empathy_context or EmpathyContext()
        response_style = self.communicator.derive_style(profile, emotional, empathy_context)
        goal_progress = goal_progress or {}
        # Evaluate achievements and triggers.
        unlocked, triggers = self.motivation_engine.evaluate_progress(
            profile, emotional, goal_progress
        )
        for achievement in unlocked:
            self.profiler.unlock(user_id, achievement)
        wellbeing = self.wellness_advisor.recommend(profile, emotional, session_metrics)
        style_guidelines = self.style_learner.generate_guidelines(user_id)
        examples = self.prompt_selector.select_examples(
            profile,
            emotional,
            empathy_context.topic or "",
            prompt_candidates or (),
            limit=3,
        )
        dashboard: MutableMapping[str, object] = {
            "profile": profile.as_dict(),
            "emotional_profile": emotional.as_dict(),
            "response_style": response_style.as_metadata(),
            "achievements": [achievement.as_dict() for achievement in profile.achievements.values()],
            "motivational_triggers": list(triggers),
            "wellness": wellbeing,
            "writing_style": style_guidelines,
            "adaptive_examples": list(examples),
        }
        if self.family_manager and family_id:
            dashboard["family_mode"] = self.family_manager.export_family_state(family_id)
        return dashboard


__all__ = ["PersonalisationDashboard"]
