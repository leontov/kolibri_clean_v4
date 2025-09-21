"""Lightweight semantic planner used by the runtime."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Mapping, MutableMapping, Optional, Sequence


@dataclass(frozen=True)
class PlanStep:
    id: str
    description: str
    skill: Optional[str] = None
    dependencies: Sequence[str] = field(default_factory=tuple)

    def as_dict(self) -> Mapping[str, object]:
        return {
            "id": self.id,
            "description": self.description,
            "skill": self.skill,
            "dependencies": list(self.dependencies),
        }


@dataclass
class Plan:
    goal: str
    steps: Sequence[PlanStep]

    def as_dict(self) -> Mapping[str, object]:
        return {
            "goal": self.goal,
            "steps": [step.as_dict() for step in self.steps],
        }


@dataclass
class HierarchicalPlan:
    goal: str
    assignments: Mapping[str, Sequence[str]]

    def skills_for_level(self, level: str) -> Sequence[str]:
        return self.assignments.get(level, ())


class NeuroSemanticPlanner:
    """Derives simple skill plans based on hints and registered skills."""

    def __init__(self, skills: Optional[Mapping[str, object]] = None) -> None:
        self._skills: MutableMapping[str, object] = dict(skills or {})

    def register_skills(self, skills: Iterable[object]) -> None:
        for skill in skills:
            if hasattr(skill, "name"):
                self._skills[getattr(skill, "name")] = skill

    def plan(self, goal: str, hints: Sequence[str] | None = None) -> Plan:
        hints = hints or ()
        chosen_skill = None
        for hint in hints:
            if hint in self._skills:
                chosen_skill = hint
                break
        if chosen_skill is None and self._skills:
            chosen_skill = next(iter(self._skills))
        step = PlanStep(id="step-1", description=goal, skill=chosen_skill)
        return Plan(goal=goal, steps=[step])

    def hierarchical_plan(self, goal: str) -> HierarchicalPlan:
        if self._skills:
            primary = next(iter(self._skills))
            assignments = {"root": (primary,), "support": tuple(sorted(self._skills))}
        else:
            assignments = {"root": (), "support": ()}
        return HierarchicalPlan(goal=goal, assignments=assignments)


__all__ = ["HierarchicalPlan", "NeuroSemanticPlanner", "Plan", "PlanStep"]
