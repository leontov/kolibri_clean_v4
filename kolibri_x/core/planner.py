"""Lightweight semantic planner used by the runtime."""
from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass, field
from itertools import pairwise
import re
from typing import Dict, Iterable, List, Mapping, MutableMapping, Optional, Sequence, Set


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
        hints = tuple(hint for hint in (hints or ()) if isinstance(hint, str))
        if not self._skills:
            step = PlanStep(id="step-1", description=goal, skill=None)
            return Plan(goal=goal, steps=[step])

        normalised_hints = tuple(self._normalise_hint(hint) for hint in hints if hint.strip())
        hint_sequences = self._extract_sequences(normalised_hints)
        resolved_sequences = [sequence for sequence in (self._resolve_sequence(seq) for seq in hint_sequences) if sequence]
        ordered_skills = self._prioritised_skills(goal, normalised_hints, resolved_sequences)
        if not ordered_skills:
            ordered_skills = list(self._skills)

        dependency_map = self._build_dependency_map(resolved_sequences)
        steps: List[PlanStep] = []
        skill_to_step: Dict[str, str] = {}
        for index, skill_name in enumerate(ordered_skills, start=1):
            step_id = f"step-{index}"
            dependencies = self._dependencies_for_skill(skill_name, dependency_map, skill_to_step)
            description = self._step_description(goal, skill_name, hints)
            steps.append(
                PlanStep(
                    id=step_id,
                    description=description,
                    skill=skill_name,
                    dependencies=dependencies,
                )
            )
            skill_to_step[skill_name] = step_id

        # ensure sequential ordering when no explicit dependency was provided
        for previous, current in pairwise(steps):
            if not current.dependencies:
                object.__setattr__(
                    current,
                    "dependencies",
                    (previous.id,),
                )

        return Plan(goal=goal, steps=steps)

    def _normalise_hint(self, hint: str) -> str:
        return hint.strip().lower()

    def _extract_sequences(self, hints: Sequence[str]) -> List[List[str]]:
        sequences: List[List[str]] = []
        for hint in hints:
            tokens = [token.strip() for token in re.split(r"->|>", hint) if token.strip()]
            if len(tokens) > 1:
                sequences.append(tokens)
        return sequences

    def _resolve_sequence(self, tokens: Sequence[str]) -> List[str]:
        resolved: List[str] = []
        for token in tokens:
            skill = self._match_skill(token)
            if skill and skill not in resolved:
                resolved.append(skill)
        return resolved

    def _prioritised_skills(
        self,
        goal: str,
        hints: Sequence[str],
        sequences: Sequence[Sequence[str]],
    ) -> List[str]:
        ordered: List[str] = []
        seen: Set[str] = set()

        def add_skill(name: Optional[str]) -> None:
            if name and name not in seen:
                ordered.append(name)
                seen.add(name)

        for sequence in sequences:
            for name in sequence:
                add_skill(name)

        for hint in hints:
            add_skill(self._match_skill(hint))

        goal_text = goal.lower()
        for name in self._skills:
            if name.lower() in goal_text:
                add_skill(name)

        for name in sorted(self._skills):
            add_skill(name)

        return ordered

    def _build_dependency_map(self, sequences: Sequence[Sequence[str]]) -> Dict[str, Set[str]]:
        dependency_map: Dict[str, Set[str]] = defaultdict(set)
        for sequence in sequences:
            for prev_skill, next_skill in pairwise(sequence):
                dependency_map[next_skill].add(prev_skill)
        return dependency_map

    def _dependencies_for_skill(
        self,
        skill_name: str,
        dependency_map: Mapping[str, Set[str]],
        skill_to_step: Mapping[str, str],
    ) -> Sequence[str]:
        upstream = dependency_map.get(skill_name, set())
        resolved: List[str] = []
        for dependency in upstream:
            step_id = skill_to_step.get(dependency)
            if step_id:
                resolved.append(step_id)
        resolved.sort()
        return tuple(resolved)

    def _match_skill(self, token: str) -> Optional[str]:
        token = token.strip().lower()
        for name in self._skills:
            if name.lower() == token:
                return name
        for name in self._skills:
            if token and (token in name.lower() or name.lower() in token):
                return name
        for name, skill in self._skills.items():
            inputs = getattr(skill, "inputs", None)
            if not inputs:
                continue
            for value in inputs:
                if isinstance(value, str) and token in value.lower():
                    return name
        return None

    def _step_description(self, goal: str, skill: str, hints: Sequence[str]) -> str:
        skill_lower = skill.lower()
        for hint in hints:
            lowered = hint.lower()
            if skill_lower in lowered or lowered in skill_lower:
                final_hint = hint
                if "->" in hint or ">" in hint:
                    segments = [segment.strip() for segment in re.split(r"->|>", hint) if segment.strip()]
                    for segment in segments:
                        if skill_lower in segment.lower():
                            final_hint = segment
                            break
                return f"{final_hint} ({skill})"
        return f"Use {skill} to progress '{goal}'"

    def hierarchical_plan(self, goal: str) -> HierarchicalPlan:
        if self._skills:
            primary = next(iter(self._skills))
            assignments = {"root": (primary,), "support": tuple(sorted(self._skills))}
        else:
            assignments = {"root": (), "support": ()}
        return HierarchicalPlan(goal=goal, assignments=assignments)


__all__ = ["HierarchicalPlan", "NeuroSemanticPlanner", "Plan", "PlanStep"]
