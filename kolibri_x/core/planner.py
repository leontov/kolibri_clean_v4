"""Neuro-semantic planner for decomposing user goals into executable steps."""
from __future__ import annotations

from dataclasses import dataclass, field
import itertools
import uuid
from typing import Iterable, List, Mapping, Optional, Sequence

from kolibri_x.skills.store import SkillManifest


@dataclass
class PlanStep:
    """Single action in a decomposed goal."""

    id: str
    description: str
    skill: Optional[str] = None
    dependencies: Sequence[str] = field(default_factory=tuple)


@dataclass
class Plan:
    goal: str
    steps: List[PlanStep]

    def as_dict(self) -> Mapping[str, object]:
        return {
            "goal": self.goal,
            "steps": [
                {
                    "id": step.id,
                    "description": step.description,
                    "skill": step.skill,
                    "dependencies": list(step.dependencies),
                }
                for step in self.steps
            ],
        }


class NeuroSemanticPlanner:
    """Minimal viable planner that aligns goals with available skills."""

    def __init__(self, skill_catalogue: Mapping[str, SkillManifest] | None = None) -> None:
        self._skills = dict(skill_catalogue or {})

    def register_skills(self, manifests: Iterable[SkillManifest]) -> None:
        for manifest in manifests:
            self._skills[manifest.name] = manifest

    def plan(self, goal: str, hints: Optional[Sequence[str]] = None) -> Plan:
        tokens = [token.strip() for token in goal.replace("\n", " ").split(".") if token.strip()]
        if not tokens:
            tokens = [goal.strip()]
        steps = []
        for index, sentence in enumerate(tokens):
            skill = self._match_skill(sentence, hints)
            step_id = f"step-{index+1}-{uuid.uuid4().hex[:6]}"
            dependency = steps[-1].id if steps else None
            dependencies = (dependency,) if dependency else tuple()
            steps.append(PlanStep(id=step_id, description=sentence, skill=skill, dependencies=dependencies))
        return Plan(goal=goal, steps=steps)

    def _match_skill(self, sentence: str, hints: Optional[Sequence[str]]) -> Optional[str]:
        candidates = list(self._skills.values())
        if hints:
            hint_set = {hint.lower() for hint in hints}
            candidates = [skill for skill in candidates if skill.name.lower() in hint_set]
            if candidates:
                return candidates[0].name
        sentence_l = sentence.lower()
        score = -1
        best: Optional[str] = None
        for skill in candidates:
            keywords = itertools.chain([skill.name], skill.inputs, skill.permissions)
            local_score = sum(1 for kw in keywords if kw and kw.lower() in sentence_l)
            if local_score > score:
                score = local_score
                best = skill.name
        return best


__all__ = ["NeuroSemanticPlanner", "Plan", "PlanStep"]
