"""Advanced neuro-semantic planning with hierarchical coordination."""
from __future__ import annotations

from dataclasses import dataclass, field
import itertools
import uuid
from statistics import fmean
from typing import Dict, Iterable, List, Mapping, MutableMapping, Optional, Sequence, Tuple

from kolibri_x.skills.store import SkillManifest


@dataclass
class PlanStep:
    """Single action in a decomposed goal."""

    id: str
    description: str
    skill: Optional[str] = None
    dependencies: Sequence[str] = field(default_factory=tuple)
    risk: float = 0.0
    agent: Optional[str] = None
    metadata: MutableMapping[str, object] = field(default_factory=dict)

    def as_dict(self) -> Mapping[str, object]:
        return {
            "id": self.id,
            "description": self.description,
            "skill": self.skill,
            "dependencies": list(self.dependencies),
            "risk": self.risk,
            "agent": self.agent,
            "metadata": dict(self.metadata),
        }


@dataclass
class Plan:
    goal: str
    steps: List[PlanStep]
    risk_score: float = 0.0
    horizon: Optional[str] = None
    versions: List[str] = field(default_factory=list)

    def as_dict(self) -> Mapping[str, object]:
        return {
            "goal": self.goal,
            "steps": [step.as_dict() for step in self.steps],
            "risk_score": self.risk_score,
            "horizon": self.horizon,
            "versions": list(self.versions),
        }


@dataclass
class RiskAssessment:
    likelihood: float
    impact: float
    mitigation: str

    @property
    def score(self) -> float:
        return max(0.0, min(1.0, self.likelihood * self.impact))


@dataclass
class PlanNode:
    step: PlanStep
    children: List["PlanNode"] = field(default_factory=list)
    risk: Optional[RiskAssessment] = None
    preferred_agents: Sequence[str] = field(default_factory=tuple)

    def depth_first(self) -> Iterable["PlanNode"]:
        yield self
        for child in self.children:
            yield from child.depth_first()


@dataclass
class HierarchicalPlan:
    root: PlanNode
    risk_score: float
    assignments: Mapping[str, str]

    def all_steps(self) -> List[PlanStep]:
        return [node.step for node in self.root.depth_first()]


class RiskAssessor:
    """Derives light-touch risk estimates from the plan semantics."""

    def assess(self, description: str, dependencies: Sequence[str]) -> RiskAssessment:
        tokens = description.lower().split()
        likelihood = 0.25 + 0.1 * len(dependencies)
        if any(keyword in tokens for keyword in {"integrate", "deploy", "compliance", "legal"}):
            likelihood += 0.3
        impact = 0.3 + 0.02 * len(tokens)
        if "critical" in tokens or "deadline" in tokens:
            impact += 0.4
        mitigation = "peer_review" if "code" in tokens else "checkpoint_review"
        return RiskAssessment(likelihood=min(likelihood, 1.0), impact=min(impact, 1.0), mitigation=mitigation)


class AgentCoordinator:
    """Allocates subagents while keeping track of utilisation."""

    def __init__(self, registry: Optional[Mapping[str, Mapping[str, object]]] = None) -> None:
        self.registry: Dict[str, Mapping[str, object]] = dict(registry or {})
        self._load: Dict[str, float] = {name: 0.0 for name in self.registry}

    def register(self, name: str, profile: Mapping[str, object]) -> None:
        self.registry[name] = profile
        self._load.setdefault(name, 0.0)

    def choose_agent(self, skill: Optional[str]) -> Optional[str]:
        if not skill or not self.registry:
            return None
        candidates = [
            (name, profile)
            for name, profile in self.registry.items()
            if skill in profile.get("skills", ())
        ]
        if not candidates:
            return None
        best_agent = min(candidates, key=lambda item: self._load.get(item[0], 0.0))[0]
        self._load[best_agent] = self._load.get(best_agent, 0.0) + 1.0
        return best_agent


class MissionLibrary:
    """Stores parametric mission templates and metrics."""

    def __init__(self) -> None:
        self._templates: Dict[str, Mapping[str, object]] = {}

    def register(self, name: str, template: Mapping[str, object]) -> None:
        self._templates[name] = template

    def get(self, name: str) -> Mapping[str, object]:
        return dict(self._templates[name])

    def match(self, goal: str) -> Optional[str]:
        goal_l = goal.lower()
        best: Tuple[int, Optional[str]] = (0, None)
        for name, template in self._templates.items():
            keywords = template.get("keywords", [])
            score = sum(1 for kw in keywords if kw in goal_l)
            if score > best[0]:
                best = (score, name)
        return best[1]


class NeuroSemanticPlanner:
    """Planner that decomposes goals and coordinates specialised agents."""

    def __init__(self, skill_catalogue: Mapping[str, SkillManifest] | None = None) -> None:
        self._skills = dict(skill_catalogue or {})
        self._risk = RiskAssessor()
        self._coordinator = AgentCoordinator(
            {
                "writer_agent": {"skills": {"writer"}, "capacity": 3},
                "analyst_agent": {"skills": {"analyst", "research"}, "capacity": 2},
            }
        )
        self.missions = MissionLibrary()

    def register_skills(self, manifests: Iterable[SkillManifest]) -> None:
        for manifest in manifests:
            self._skills[manifest.name] = manifest

    def plan(self, goal: str, hints: Optional[Sequence[str]] = None) -> Plan:
        tokens = [token.strip() for token in goal.replace("\n", " ").split(".") if token.strip()]
        if not tokens:
            tokens = [goal.strip()]
        steps: List[PlanStep] = []
        for index, sentence in enumerate(tokens):
            skill = self._match_skill(sentence, hints)
            step_id = f"step-{index+1}-{uuid.uuid4().hex[:6]}"
            dependency = steps[-1].id if steps else None
            dependencies = (dependency,) if dependency else tuple()
            risk = self._risk.assess(sentence, dependencies)
            agent = self._coordinator.choose_agent(skill)
            steps.append(
                PlanStep(
                    id=step_id,
                    description=sentence,
                    skill=skill,
                    dependencies=dependencies,
                    risk=risk.score,
                    agent=agent,
                    metadata={"mitigation": risk.mitigation},
                )
            )
        risk_score = fmean(step.risk for step in steps) if steps else 0.0
        mission = self.missions.match(goal)
        versions = [mission] if mission else []
        return Plan(goal=goal, steps=steps, risk_score=risk_score, versions=versions)

    def hierarchical_plan(self, goal: str, hints: Optional[Sequence[str]] = None) -> HierarchicalPlan:
        base = self.plan(goal, hints=hints)
        root_step = PlanStep(id="root", description=goal, skill=None, dependencies=tuple())
        root_node = PlanNode(step=root_step)
        for step in base.steps:
            risk = RiskAssessment(
                likelihood=step.risk,
                impact=min(step.risk + 0.2, 1.0),
                mitigation=step.metadata.get("mitigation", ""),
            )
            node = PlanNode(step=step, risk=risk, preferred_agents=(step.agent,) if step.agent else tuple())
            root_node.children.append(node)
        assignments = {step.id: step.agent for step in base.steps if step.agent}
        return HierarchicalPlan(root=root_node, risk_score=base.risk_score, assignments=assignments)

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


__all__ = [
    "AgentCoordinator",
    "HierarchicalPlan",
    "MissionLibrary",
    "NeuroSemanticPlanner",
    "Plan",
    "PlanNode",
    "PlanStep",
    "RiskAssessment",
    "RiskAssessor",
]
