"""Runtime orchestration pipeline that stitches together Kolibri subsystems."""
from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import json
from typing import Callable, Dict, Iterable, List, Mapping, MutableMapping, Optional, Sequence

from kolibri_x.core.encoders import ASREncoder, FusionTransformer, ImageEncoder, TextEncoder
from kolibri_x.core.planner import NeuroSemanticPlanner, Plan, PlanStep
from kolibri_x.kg.graph import KnowledgeGraph
from kolibri_x.kg.rag import RAGPipeline
from kolibri_x.personalization import EmpathyContext, EmpathyModulator, InteractionSignal, OnDeviceProfiler
from kolibri_x.privacy.consent import PrivacyOperator
from kolibri_x.runtime.cache import OfflineCache
from kolibri_x.runtime.journal import ActionJournal, JournalEntry
from kolibri_x.skills.store import SkillStore
from kolibri_x.xai.reasoning import ReasoningLog


SkillExecutor = Callable[[Mapping[str, object]], Mapping[str, object]]


class SkillExecutionError(RuntimeError):
    """Raised when a sandboxed skill fails to produce a valid response."""


class SkillSandbox:
    """Very small sandbox that hosts pure Python skill callables."""

    def __init__(self) -> None:
        self._executors: Dict[str, SkillExecutor] = {}

    def register(self, name: str, executor: SkillExecutor) -> None:
        self._executors[name] = executor

    def execute(self, name: str, payload: Mapping[str, object]) -> Mapping[str, object]:
        try:
            executor = self._executors[name]
        except KeyError as exc:
            raise KeyError(f"unknown skill executor: {name}") from exc
        result = executor(payload)
        if not isinstance(result, Mapping):
            raise SkillExecutionError(f"skill {name} returned non-mapping result: {type(result)!r}")
        return dict(result)

    def registered(self) -> Sequence[str]:
        return tuple(sorted(self._executors))


@dataclass
class RuntimeRequest:
    user_id: str
    goal: str
    modalities: Mapping[str, object] = field(default_factory=dict)
    hints: Sequence[str] = field(default_factory=tuple)
    signals: Sequence[InteractionSignal] = field(default_factory=tuple)
    empathy: EmpathyContext = field(default_factory=EmpathyContext)
    top_k: int = 5


@dataclass
class SkillExecution:
    step_id: str
    skill: Optional[str]
    output: Mapping[str, object]

    def to_dict(self) -> Mapping[str, object]:
        return {"step_id": self.step_id, "skill": self.skill, "output": dict(self.output)}

    @classmethod
    def from_dict(cls, data: Mapping[str, object]) -> "SkillExecution":
        return cls(
            step_id=str(data.get("step_id")),
            skill=data.get("skill") if data.get("skill") is not None else None,
            output=dict(data.get("output", {})),
        )


@dataclass
class RuntimeResponse:
    plan: Plan
    answer: Mapping[str, object]
    adjustments: Mapping[str, float]
    executions: Sequence[SkillExecution]
    reasoning: ReasoningLog
    journal_tail: Sequence[JournalEntry]
    cached: bool = False


class KolibriRuntime:
    """Coordinates encoding, planning, retrieval, skills, and empathy."""

    def __init__(
        self,
        *,
        graph: Optional[KnowledgeGraph] = None,
        text_encoder: Optional[TextEncoder] = None,
        asr: Optional[ASREncoder] = None,
        image_encoder: Optional[ImageEncoder] = None,
        fusion: Optional[FusionTransformer] = None,
        planner: Optional[NeuroSemanticPlanner] = None,
        rag: Optional[RAGPipeline] = None,
        skill_store: Optional[SkillStore] = None,
        sandbox: Optional[SkillSandbox] = None,
        privacy: Optional[PrivacyOperator] = None,
        profiler: Optional[OnDeviceProfiler] = None,
        empathy: Optional[EmpathyModulator] = None,
        cache: Optional[OfflineCache] = None,
        journal: Optional[ActionJournal] = None,
    ) -> None:
        self.graph = graph or KnowledgeGraph()
        self.text_encoder = text_encoder or TextEncoder(dim=32)
        self.asr = asr or ASREncoder()
        self.image_encoder = image_encoder or ImageEncoder(dim=32)
        self.fusion = fusion or FusionTransformer(dim=32)
        self.planner = planner or NeuroSemanticPlanner()
        self.skill_store = skill_store or SkillStore()
        self.sandbox = sandbox or SkillSandbox()
        self.privacy = privacy or PrivacyOperator()
        self.profiler = profiler or OnDeviceProfiler()
        self.empathy = empathy or EmpathyModulator()
        self.cache = cache
        self.journal = journal or ActionJournal()
        self.rag = rag or RAGPipeline(self.graph, encoder=self.text_encoder)
        skills = self.skill_store.list()
        if skills:
            self.planner.register_skills(skills)

    def process(self, request: RuntimeRequest) -> RuntimeResponse:
        reasoning = ReasoningLog()
        filtered_modalities = self._enforce_privacy(request.user_id, request.modalities, reasoning)
        transcript = self._compose_transcript(filtered_modalities)
        embeddings = self._encode_modalities(filtered_modalities, transcript, reasoning)
        fusion_result = self.fusion.fuse(embeddings) if embeddings else None
        if fusion_result:
            self.journal.append(
                "fusion",
                {
                    "modalities": list(embeddings.keys()),
                    "weights": dict(fusion_result.modality_weights),
                    "embedding_preview": fusion_result.embedding[:4],
                },
            )
            reasoning.add_step(
                "fusion",
                f"fused {len(embeddings)} modalities",
                fusion_result.modality_weights.keys(),
                confidence=0.6,
            )

        cache_key = self._cache_key(request.user_id, request.goal, filtered_modalities, transcript)
        cached_payload = self.cache.get(cache_key) if self.cache else None
        if cached_payload:
            plan = self._plan_from_dict(cached_payload["plan"])
            reasoning.add_step("cache", "served response from offline cache", [], confidence=0.95)
            self.journal.append("cache_hit", {"user_id": request.user_id, "goal": request.goal})
            executions = [SkillExecution.from_dict(data) for data in cached_payload.get("executions", [])]
            return RuntimeResponse(
                plan=plan,
                answer=dict(cached_payload.get("answer", {})),
                adjustments=dict(cached_payload.get("adjustments", {})),
                executions=executions,
                reasoning=reasoning,
                journal_tail=self.journal.tail(),
                cached=True,
            )

        plan = self.planner.plan(request.goal, hints=request.hints)
        reasoning.add_step(
            "plan",
            f"generated {len(plan.steps)} steps",
            [step.id for step in plan.steps],
            confidence=0.7,
        )
        self.journal.append(
            "plan",
            {
                "goal": request.goal,
                "step_count": len(plan.steps),
                "skills": [step.skill for step in plan.steps],
            },
        )

        answer = self.rag.answer(transcript or request.goal, top_k=request.top_k, reasoning=reasoning)
        self.journal.append(
            "rag_answer",
            {"query": transcript or request.goal, "support": [fact["id"] for fact in answer.get("support", [])]},
        )

        executions = self._execute_plan(plan, request, filtered_modalities, reasoning)
        profile = self.profiler.bulk_record(request.user_id, request.signals)
        adjustments = self.empathy.modulation(profile, request.empathy)
        reasoning.add_step(
            "empathy",
            "generated modulation vector",
            adjustments.keys(),
            confidence=0.55,
        )
        self.journal.append(
            "empathy",
            {"user_id": request.user_id, "adjustments": dict(adjustments)},
        )

        payload = {
            "plan": plan.as_dict(),
            "answer": answer,
            "executions": [execution.to_dict() for execution in executions],
            "adjustments": dict(adjustments),
        }
        if self.cache:
            self.cache.put(cache_key, payload)
            self.journal.append("cache_store", {"key": cache_key, "user_id": request.user_id})

        return RuntimeResponse(
            plan=plan,
            answer=answer,
            adjustments=dict(adjustments),
            executions=executions,
            reasoning=reasoning,
            journal_tail=self.journal.tail(),
            cached=False,
        )

    def _enforce_privacy(
        self,
        user_id: str,
        modalities: Mapping[str, object],
        reasoning: ReasoningLog,
    ) -> Mapping[str, object]:
        allowed_modalities = set(self.privacy.enforce(user_id, list(modalities.keys())))
        filtered = {modality: value for modality, value in modalities.items() if modality in allowed_modalities}
        blocked = sorted(set(modalities) - allowed_modalities)
        self.journal.append(
            "privacy",
            {
                "user_id": user_id,
                "allowed": sorted(allowed_modalities),
                "blocked": blocked,
            },
        )
        reasoning.add_step("privacy", "enforced consent policies", allowed_modalities, confidence=0.8)
        return filtered

    def _compose_transcript(self, modalities: Mapping[str, object]) -> str:
        fragments: List[str] = []
        text_value = modalities.get("text")
        if isinstance(text_value, str):
            fragments.append(text_value.strip())
        audio_value = modalities.get("audio")
        if audio_value is not None:
            fragments.append(self.asr.transcribe(audio_value))
        return "\n".join(fragment for fragment in fragments if fragment)

    def _encode_modalities(
        self,
        modalities: Mapping[str, object],
        transcript: str,
        reasoning: ReasoningLog,
    ) -> Mapping[str, Sequence[float]]:
        embeddings: MutableMapping[str, Sequence[float]] = {}
        if transcript:
            embeddings["text"] = self.text_encoder.encode(transcript)
        image_value = modalities.get("image")
        if image_value is not None:
            embeddings["image"] = self.image_encoder.encode(image_value)
        return embeddings

    def _execute_plan(
        self,
        plan: Plan,
        request: RuntimeRequest,
        modalities: Mapping[str, object],
        reasoning: ReasoningLog,
    ) -> List[SkillExecution]:
        executions: List[SkillExecution] = []
        for step in plan.steps:
            execution = self._execute_step(step, request, modalities, reasoning)
            executions.append(execution)
        return executions

    def _execute_step(
        self,
        step: PlanStep,
        request: RuntimeRequest,
        modalities: Mapping[str, object],
        reasoning: ReasoningLog,
    ) -> SkillExecution:
        if not step.skill:
            reasoning.add_step("noop", f"step {step.id} had no mapped skill", [step.id], confidence=0.4)
            payload = {"status": "skipped", "reason": "no_skill"}
            self.journal.append("skill_skipped", {"step_id": step.id})
            return SkillExecution(step_id=step.id, skill=None, output=payload)

        manifest = self.skill_store.get(step.skill)
        if not manifest:
            reasoning.add_step("missing_skill", f"skill {step.skill} unavailable", [step.id], confidence=0.3)
            payload = {"status": "missing", "skill": step.skill}
            self.journal.append("skill_missing", {"step_id": step.id, "skill": step.skill})
            return SkillExecution(step_id=step.id, skill=step.skill, output=payload)

        try:
            self.skill_store.require_permissions(step.skill, manifest.permissions)
            sandbox_payload = {
                "goal": request.goal,
                "step": step.description,
                "modalities": list(modalities.keys()),
            }
            result = self.sandbox.execute(step.skill, sandbox_payload)
            payload = {"status": "ok", "result": result}
            reasoning.add_step("skill", f"executed {step.skill}", [step.id], confidence=0.75)
            self.journal.append(
                "skill_executed",
                {"step_id": step.id, "skill": step.skill, "result_keys": sorted(result.keys())},
            )
        except Exception as exc:  # pragma: no cover - defensive path
            payload = {"status": "error", "message": str(exc)}
            reasoning.add_step("skill_error", f"{step.skill} failed", [step.id], confidence=0.1)
            self.journal.append("skill_error", {"step_id": step.id, "error": str(exc)})
        return SkillExecution(step_id=step.id, skill=step.skill, output=payload)

    def _plan_from_dict(self, payload: Mapping[str, object]) -> Plan:
        steps_payload = payload.get("steps", [])
        steps: List[PlanStep] = []
        for item in steps_payload:
            if not isinstance(item, Mapping):
                continue
            steps.append(
                PlanStep(
                    id=str(item.get("id")),
                    description=str(item.get("description", "")),
                    skill=item.get("skill"),
                    dependencies=tuple(item.get("dependencies", [])),
                )
            )
        return Plan(goal=str(payload.get("goal", "")), steps=steps)

    def _cache_key(
        self,
        user_id: str,
        goal: str,
        modalities: Mapping[str, object],
        transcript: str,
    ) -> str:
        canonical_modalities: MutableMapping[str, object] = {}
        for key, value in modalities.items():
            canonical_modalities[key] = self._normalise_cache_value(value)
        payload = {
            "user": user_id,
            "goal": goal,
            "modalities": canonical_modalities,
            "transcript": transcript,
        }
        digest = json.dumps(payload, sort_keys=True, ensure_ascii=False)
        return hashlib.sha256(digest.encode("utf-8")).hexdigest()

    def _normalise_cache_value(self, value: object) -> object:
        if isinstance(value, bytes):
            return hashlib.sha1(value).hexdigest()
        if isinstance(value, (str, int, float, bool)) or value is None:
            return value
        if isinstance(value, Iterable) and not isinstance(value, (str, bytes)):
            return [self._normalise_cache_value(item) for item in value]
        return repr(value)


__all__ = [
    "KolibriRuntime",
    "RuntimeRequest",
    "RuntimeResponse",
    "SkillExecution",
    "SkillExecutionError",
    "SkillSandbox",
]
