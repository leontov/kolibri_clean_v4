"""Mission execution helpers for Kolibri evaluation."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Mapping, Optional, Sequence

from kolibri_x.runtime.orchestrator import KolibriRuntime, RuntimeRequest, RuntimeResponse


@dataclass
class Mission:
    """Represents a single evaluation mission/task."""

    identifier: str
    description: str
    goal: str
    modalities: Mapping[str, object] = field(default_factory=dict)
    hints: Sequence[str] = field(default_factory=tuple)
    expected_skills: Sequence[str] = field(default_factory=tuple)
    skill_scopes: Sequence[str] = field(default_factory=tuple)
    data_tags: Sequence[str] = field(default_factory=tuple)
    scoring: Callable[[RuntimeResponse], float] = lambda response: 1.0 if response.answer else 0.0

    def run(self, runtime: KolibriRuntime, user_id: str = "eval") -> "MissionOutcome":
        request = RuntimeRequest(
            user_id=user_id,
            goal=self.goal,
            modalities=self.modalities,
            hints=self.hints,
            skill_scopes=self.skill_scopes,
            data_tags=self.data_tags,
        )
        response = runtime.process(request)
        score = self.scoring(response)
        return MissionOutcome(
            mission_id=self.identifier,
            description=self.description,
            score=score,
            plan_steps=len(response.plan.steps),
            used_skills=[execution.skill for execution in response.executions if execution.skill],
            expected_skills=self.expected_skills,
            reasoning_steps=len(response.reasoning.steps()),
            cached=response.cached,
        )


@dataclass
class MissionOutcome:
    mission_id: str
    description: str
    score: float
    plan_steps: int
    used_skills: Sequence[str]
    expected_skills: Sequence[str]
    reasoning_steps: int
    cached: bool = False

    def skill_coverage(self) -> float:
        if not self.expected_skills:
            return 1.0
        expected = set(self.expected_skills)
        used = set(skill for skill in self.used_skills if skill is not None)
        return len(expected & used) / len(expected)


@dataclass
class MissionPack:
    """Collection of missions used for aggregated evaluation."""

    name: str
    missions: Sequence[Mission]

    def execute(
        self,
        runtime: KolibriRuntime,
        *,
        user_id: str = "eval",
        progress: Optional[Callable[[Mission, MissionOutcome], None]] = None,
    ) -> Sequence[MissionOutcome]:
        outcomes = []
        for mission in self.missions:
            outcome = mission.run(runtime, user_id=user_id)
            outcomes.append(outcome)
            if progress:
                progress(mission, outcome)
        return tuple(outcomes)

    def as_dict(self) -> Mapping[str, object]:
        return {
            "name": self.name,
            "missions": [
                {
                    "identifier": mission.identifier,
                    "description": mission.description,
                    "goal": mission.goal,
                    "modalities": dict(mission.modalities),
                    "hints": list(mission.hints),
                    "expected_skills": list(mission.expected_skills),
                    "skill_scopes": list(mission.skill_scopes),
                    "data_tags": list(mission.data_tags),
                }
                for mission in self.missions
            ],
        }


__all__ = ["Mission", "MissionOutcome", "MissionPack"]
