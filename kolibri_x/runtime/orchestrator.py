"""Runtime orchestration pipeline that stitches together Kolibri subsystems."""
from __future__ import annotations

import hashlib
import json

import multiprocessing
from multiprocessing.connection import Connection
import traceback


from pathlib import Path

import time


from collections.abc import Iterable as IterableABC
from dataclasses import dataclass, field
from datetime import datetime

from pathlib import Path
from typing import Callable, Dict, Iterable, List, Mapping, MutableMapping, Optional, Sequence, Tuple

from typing import Callable, Dict, Iterable, List, Mapping, MutableMapping, Optional, Sequence, Tuple, Union


from kolibri_x.core.encoders import (
    ASREncoder,
    AdaptiveAudioEncoder,
    AdaptiveCrossModalTransformer,
    DiffusionVisionEncoder,
    FusionTransformer,
    ImageEncoder,
    ModalitySignal,
    SensorEvent,
    SensorHub,
    TemporalAlignmentEngine,
    TextEncoder,
)
from kolibri_x.core.planner import NeuroSemanticPlanner, Plan, PlanStep
from kolibri_x.kg.graph import KnowledgeGraph
from kolibri_x.kg.ingest import IngestionReport, KnowledgeDocument, KnowledgeIngestor
from kolibri_x.kg.rag import RAGPipeline
from kolibri_x.personalization import EmpathyContext, EmpathyModulator, InteractionSignal, OnDeviceProfiler
from kolibri_x.privacy.consent import PrivacyOperator
from kolibri_x.runtime.cache import OfflineCache, RAGCache
from kolibri_x.runtime.iot import IoTBridge, IoTCommand
from kolibri_x.runtime.journal import ActionJournal, JournalEntry
from kolibri_x.runtime.metrics import SLOTracker
from kolibri_x.runtime.self_learning import BackgroundSelfLearner
from kolibri_x.runtime.workflow import ReminderEvent, ReminderRule, Workflow, WorkflowManager
from kolibri_x.skills.store import (
    SkillPolicyViolation,
    SkillQuota,
    SkillQuotaExceeded,
    SkillStore,
)
from kolibri_x.xai.reasoning import ReasoningLog


SkillExecutor = Callable[[Mapping[str, object]], Mapping[str, object]]


class SkillExecutionError(RuntimeError):
    """Raised when a sandboxed skill fails to produce a valid response."""



def _invoke_skill(
    result_conn: Connection,
    executor: SkillExecutor,
    payload: Mapping[str, object],
    memory_limit_bytes: Optional[int],
) -> None:
    """Worker entry point executed inside a separate process."""

    if memory_limit_bytes is not None:
        try:  # pragma: no cover - platform specific guard
            import resource

            soft, hard = resource.getrlimit(resource.RLIMIT_AS)
            new_soft = memory_limit_bytes
            new_hard = hard
            if hard == resource.RLIM_INFINITY or hard > memory_limit_bytes:
                new_hard = memory_limit_bytes
            elif hard < memory_limit_bytes:
                new_soft = hard
                new_hard = hard
            resource.setrlimit(resource.RLIMIT_AS, (new_soft, new_hard))
        except ValueError:  # pragma: no cover - fallback attempt
            try:
                resource.setrlimit(resource.RLIMIT_AS, (memory_limit_bytes, resource.RLIM_INFINITY))
            except (ValueError, OSError):
                pass
        except (ImportError, AttributeError):  # pragma: no cover - best effort guard
            pass

    try:
        result = executor(payload)
    except BaseException as exc:  # pragma: no cover - protective path
        result_conn.send(
            (
                "error",
                {
                    "exc_type": type(exc).__name__,
                    "message": str(exc),
                    "traceback": traceback.format_exc(),
                },
            )
        )
    else:
        result_conn.send(("ok", result))
    finally:
        result_conn.close()

@dataclass
class _SkillUsage:
    invocations: int = 0
    cpu_ms: float = 0.0
    wall_ms: float = 0.0
    net_bytes: int = 0
    fs_bytes: int = 0
    fs_ops: int = 0



class SkillSandbox:
    """Very small sandbox that hosts pure Python skill callables."""

    def __init__(
        self,
        *,
        time_limit: float = 1.0,
        memory_limit_mb: Optional[int] = 128,
        journal: Optional[ActionJournal] = None,
    ) -> None:
        self._executors: Dict[str, SkillExecutor] = {}

        self._time_limit = max(float(time_limit), 0.0)
        self._memory_limit_bytes = (
            int(memory_limit_mb) * 1024 * 1024 if memory_limit_mb is not None else None
        )
        self._journal = journal
        try:
            self._ctx = multiprocessing.get_context("fork")
        except ValueError:  # pragma: no cover - fallback for platforms without fork
            self._ctx = multiprocessing.get_context()

        self._usage: Dict[str, _SkillUsage] = {}
        self._quotas: Dict[str, SkillQuota] = {}


    def register(self, name: str, executor: SkillExecutor) -> None:
        self._executors[name] = executor
        self._usage.setdefault(name, _SkillUsage())

    def execute(
        self,
        name: str,
        payload: Mapping[str, object],
        *,
        quota: Optional[SkillQuota] = None,
    ) -> Mapping[str, object]:
        try:
            executor = self._executors[name]
        except KeyError as exc:  # pragma: no cover - defensive path
            raise KeyError(f"unknown skill executor: {name}") from exc

        parent_conn, child_conn = self._ctx.Pipe(duplex=False)
        process = self._ctx.Process(
            target=_invoke_skill,
            args=(child_conn, executor, payload, self._memory_limit_bytes),
        )
        process.start()
        child_conn.close()
        process.join(self._time_limit if self._time_limit > 0 else None)
        if process.is_alive():
            process.terminate()
            process.join()
            self._log_event(
                "skill_timeout",
                {
                    "skill": name,
                    "time_limit": self._time_limit,
                    "payload_keys": sorted(map(str, payload.keys())),
                },
            )
            parent_conn.close()
            raise SkillExecutionError(f"skill {name} exceeded time limit of {self._time_limit} seconds")

        status: Optional[str]
        data: Mapping[str, object]
        try:
            if parent_conn.poll():
                status, data = parent_conn.recv()
            else:
                status = None
                data = {}
        except EOFError:
            status = None
            data = {}
        finally:
            parent_conn.close()

        if status == "ok":
            result = data
            if not isinstance(result, Mapping):
                self._log_event(
                    "skill_execution_error",
                    {
                        "skill": name,
                        "error_type": "TypeError",
                        "message": f"non-mapping result: {type(result)!r}",
                    },
                )
                raise SkillExecutionError(
                    f"skill {name} returned non-mapping result: {type(result)!r}"
                )
            return dict(result)

        if status == "error":
            error_type = str(data.get("exc_type"))
            event = "skill_memory_limit_exceeded" if error_type == "MemoryError" else "skill_execution_error"
            self._log_event(
                event,
                {
                    "skill": name,
                    "error_type": error_type,
                    "message": data.get("message", ""),
                },
            )
            raise SkillExecutionError(f"skill {name} failed: {data.get('message', 'unknown error')}")

        exitcode = process.exitcode
        self._log_event(
            "skill_process_terminated",
            {
                "skill": name,
                "exit_code": exitcode,
                "payload_keys": sorted(map(str, payload.keys())),
            },
        )
        raise SkillExecutionError(f"skill {name} terminated unexpectedly (exit code={exitcode})")

        usage = self._usage.setdefault(name, _SkillUsage())
        if quota:
            self._quotas[name] = quota
            self._enforce_quota(name, usage, quota)
        start_wall = time.perf_counter()
        start_cpu = time.process_time()
        result = None
        try:
            result = executor(payload)
        finally:
            elapsed_wall = (time.perf_counter() - start_wall) * 1000.0
            elapsed_cpu = max((time.process_time() - start_cpu) * 1000.0, 0.0)
            usage.invocations += 1
            usage.wall_ms += elapsed_wall
            usage.cpu_ms += elapsed_cpu
        if not isinstance(result, Mapping):
            raise SkillExecutionError(f"skill {name} returned non-mapping result: {type(result)!r}")
        return dict(result)


    def registered(self) -> Sequence[str]:
        return tuple(sorted(self._executors))


    def bind_journal(self, journal: ActionJournal) -> None:
        self._journal = journal

    def _log_event(self, event: str, payload: Mapping[str, object]) -> None:
        if self._journal is not None:
            self._journal.append(event, payload)

    def record_io(
        self,
        name: str,
        *,
        net_bytes: int = 0,
        fs_bytes: int = 0,
        fs_ops: int = 0,
    ) -> None:
        usage = self._usage.setdefault(name, _SkillUsage())
        if net_bytes:
            usage.net_bytes += max(0, net_bytes)
        if fs_bytes:
            usage.fs_bytes += max(0, fs_bytes)
        if fs_ops:
            usage.fs_ops += max(0, fs_ops)
        quota = self._quotas.get(name)
        if quota:
            self._enforce_quota(name, usage, quota)

    def usage_snapshot(self, name: str) -> Mapping[str, float]:
        usage = self._usage.get(name)
        if not usage:
            return {}
        return {
            "invocations": usage.invocations,
            "cpu_ms": usage.cpu_ms,
            "wall_ms": usage.wall_ms,
            "net_bytes": usage.net_bytes,
            "fs_bytes": usage.fs_bytes,
            "fs_ops": usage.fs_ops,
        }

    def _enforce_quota(self, name: str, usage: _SkillUsage, quota: SkillQuota) -> None:
        if quota.invocations is not None and usage.invocations >= quota.invocations:
            raise SkillQuotaExceeded(name, "invocations", quota.invocations, usage.invocations)
        if quota.cpu_ms is not None and usage.cpu_ms >= quota.cpu_ms:
            raise SkillQuotaExceeded(name, "cpu_ms", quota.cpu_ms, int(usage.cpu_ms))
        if quota.wall_ms is not None and usage.wall_ms >= quota.wall_ms:
            raise SkillQuotaExceeded(name, "wall_ms", quota.wall_ms, int(usage.wall_ms))
        if quota.net_bytes is not None and usage.net_bytes >= quota.net_bytes:
            raise SkillQuotaExceeded(name, "net_bytes", quota.net_bytes, usage.net_bytes)
        if quota.fs_bytes is not None and usage.fs_bytes >= quota.fs_bytes:
            raise SkillQuotaExceeded(name, "fs_bytes", quota.fs_bytes, usage.fs_bytes)
        if quota.fs_ops is not None and usage.fs_ops >= quota.fs_ops:
            raise SkillQuotaExceeded(name, "fs_ops", quota.fs_ops, usage.fs_ops)



@dataclass
class RuntimeRequest:
    user_id: str
    goal: str
    modalities: Mapping[str, object] = field(default_factory=dict)
    hints: Sequence[str] = field(default_factory=tuple)
    signals: Sequence[InteractionSignal] = field(default_factory=tuple)
    empathy: EmpathyContext = field(default_factory=EmpathyContext)
    data_tags: Sequence[str] = field(default_factory=tuple)
    skill_scopes: Sequence[str] = field(default_factory=tuple)
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
    metrics: Mapping[str, Mapping[str, float]] = field(default_factory=dict)


class KolibriRuntime:
    """Coordinates encoding, planning, retrieval, skills, and empathy."""

    def __init__(
        self,
        *,
        graph: Optional[KnowledgeGraph] = None,
        text_encoder: Optional[TextEncoder] = None,
        asr: Optional[ASREncoder] = None,
        image_encoder: Optional[ImageEncoder] = None,
        audio_encoder: Optional[AdaptiveAudioEncoder] = None,
        vision_encoder: Optional[DiffusionVisionEncoder] = None,
        fusion: Optional[FusionTransformer] = None,
        cross_fusion: Optional[AdaptiveCrossModalTransformer] = None,
        planner: Optional[NeuroSemanticPlanner] = None,
        rag: Optional[RAGPipeline] = None,
        rag_cache: Optional[RAGCache] = None,
        skill_store: Optional[SkillStore] = None,
        sandbox: Optional[SkillSandbox] = None,
        privacy: Optional[PrivacyOperator] = None,
        profiler: Optional[OnDeviceProfiler] = None,
        empathy: Optional[EmpathyModulator] = None,
        cache: Optional[OfflineCache] = None,
        journal: Optional[ActionJournal] = None,
        metrics: Optional[SLOTracker] = None,
        iot_bridge: Optional[IoTBridge] = None,
        workflow_manager: Optional[WorkflowManager] = None,
        self_learner: Optional[BackgroundSelfLearner] = None,
        knowledge_ingestor: Optional[KnowledgeIngestor] = None,
        sensor_hub: Optional[SensorHub] = None,
        alignment_engine: Optional[TemporalAlignmentEngine] = None,
        fusion_budget: float = 1.5,
        self_learning_storage: Optional[str | Path] = None,
    ) -> None:
        self._self_learning_path: Optional[Path] = None
        self._self_learning_dirty = False
        self._shutdown = False

        self.graph = graph or KnowledgeGraph()
        self.text_encoder = text_encoder or TextEncoder(dim=32)
        self.asr = asr or ASREncoder()
        self.image_encoder = image_encoder or ImageEncoder(dim=32)
        self.audio_encoder = audio_encoder or AdaptiveAudioEncoder(dim=16)
        self.vision_encoder = vision_encoder or DiffusionVisionEncoder(dim=32, frame_window=4)
        self.fusion = fusion or FusionTransformer(dim=32)
        cross_fusion_dim = getattr(self.fusion, "dim", 32)
        self.cross_fusion = cross_fusion or AdaptiveCrossModalTransformer(dim=cross_fusion_dim)
        self.fusion_budget = fusion_budget
        self.planner = planner or NeuroSemanticPlanner()
        self.skill_store = skill_store or SkillStore()
        self.journal = journal or ActionJournal()
        if sandbox is None:
            self.sandbox = SkillSandbox(journal=self.journal)
        else:
            self.sandbox = sandbox
            self.sandbox.bind_journal(self.journal)
        self.privacy = privacy or PrivacyOperator()
        self.profiler = profiler or OnDeviceProfiler()
        self.empathy = empathy or EmpathyModulator()
        self.cache = cache
        self.metrics = metrics or SLOTracker()
        self.iot_bridge = iot_bridge
        self.workflow_manager = workflow_manager or WorkflowManager()
        self.ingestor = knowledge_ingestor or KnowledgeIngestor()
        self.rag = rag or RAGPipeline(self.graph, encoder=self.text_encoder)
        self.rag_cache = rag_cache or RAGCache()
        self.sensor_hub = sensor_hub or SensorHub()
        self.alignment_engine = alignment_engine or TemporalAlignmentEngine()
        if self_learning_storage and self_learner is None:
            self_learner = BackgroundSelfLearner()
        self.self_learner = self_learner
        if self_learning_storage:
            self._self_learning_path = Path(self_learning_storage).expanduser()
        if self.self_learner and self._self_learning_path:
            try:
                self.self_learner.load(self._self_learning_path)
            except FileNotFoundError:
                pass

        self._active_session_id: Optional[str] = None
        self._graph_store_path: Optional[Path] = None

        skills = self.skill_store.list()
        if skills:
            self.planner.register_skills(skills)

        if self.iot_bridge:
            if self.iot_bridge.journal is None:
                self.iot_bridge.journal = self.journal
            self.iot_bridge.attach_sensor_hub(self.sensor_hub)

    # ------------------------------------------------------------------
    # Session lifecycle
    # ------------------------------------------------------------------
    def start_session(self, session_id: str, *, graph_path: Optional[Union[str, Path]] = None) -> None:
        """Initialise a runtime session and load graph state if available."""

        if self._active_session_id and self._active_session_id != session_id:
            self.end_session()

        path = Path(graph_path) if graph_path is not None else self._graph_store_path
        if path is None:
            path = Path(f"{session_id}.kg.jsonl")
        self._graph_store_path = path

        loaded = False
        if path.exists():
            loaded = self.graph.load(path)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)

        self._active_session_id = session_id
        self.journal.append(
            "session_started",
            {
                "session_id": session_id,
                "graph_path": str(path),
                "graph_loaded": bool(loaded),
                "node_count": len(self.graph.nodes()),
                "edge_count": len(self.graph.edges()),
            },
        )

    def end_session(self) -> None:
        """Persist graph state and tear down the active session."""

        if not self._active_session_id:
            return

        session_id = self._active_session_id
        path = self._graph_store_path
        saved = False
        if path is not None:
            self.graph.save(path)
            saved = True

        if self.iot_bridge:
            self.iot_bridge.reset_session(session_id)

        self.journal.append(
            "session_finished",
            {
                "session_id": session_id,
                "graph_path": str(path) if path else None,
                "graph_saved": saved,
                "node_count": len(self.graph.nodes()),
                "edge_count": len(self.graph.edges()),
            },
        )

        self._active_session_id = None

    def process(self, request: RuntimeRequest) -> RuntimeResponse:
        reasoning = ReasoningLog()
        with self.metrics.time_stage("privacy_enforce"):
            filtered_modalities = self._enforce_privacy(request.user_id, request.modalities, reasoning)
        with self.metrics.time_stage("compose_transcript"):
            transcript = self._compose_transcript(filtered_modalities)
        with self.metrics.time_stage("encode_modalities"):
            embeddings, signals = self._encode_modalities(
                request.user_id, filtered_modalities, transcript, reasoning
            )
        with self.metrics.time_stage("fusion"):
            self._fuse_modalities(embeddings, signals, reasoning)

        cache_key = self._cache_key(
            request.user_id,
            request.goal,
            filtered_modalities,
            transcript,
            request.data_tags,
        )
        with self.metrics.time_stage("offline_cache_lookup"):
            cached_payload = self.cache.get(cache_key) if self.cache else None
        if cached_payload:
            plan = self._plan_from_dict(cached_payload["plan"])
            reasoning.add_step("cache", "served response from offline cache", [], confidence=0.95)
            self.journal.append("cache_hit", {"user_id": request.user_id, "goal": request.goal})
            executions = [SkillExecution.from_dict(data) for data in cached_payload.get("executions", [])]
            metrics_snapshot = self.metrics.report()
            self.journal.append("slo_snapshot", {"stages": metrics_snapshot})
            return RuntimeResponse(
                plan=plan,
                answer=dict(cached_payload.get("answer", {})),
                adjustments=dict(cached_payload.get("adjustments", {})),
                executions=executions,
                reasoning=reasoning,
                journal_tail=self.journal.tail(),
                cached=True,
                metrics=metrics_snapshot,
            )

        with self.metrics.time_stage("planning"):
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

        rag_query = transcript or request.goal
        with self.metrics.time_stage("rag_cache_lookup"):
            cached_answer = self.rag_cache.get(
                request.user_id,
                rag_query,
                request.data_tags,
                list(filtered_modalities.keys()),
                request.top_k,
            )
        if cached_answer:
            answer = dict(cached_answer)
            reasoning.add_step("rag_cache", "served response from rag cache", [], confidence=0.85)
            self.journal.append(
                "rag_cache_hit",
                {"user_id": request.user_id, "goal": request.goal, "query": rag_query},
            )
        else:
            with self.metrics.time_stage("rag_answer"):
                answer = self.rag.answer(rag_query, top_k=request.top_k, reasoning=reasoning)
            self.rag_cache.put(
                request.user_id,
                rag_query,
                request.data_tags,
                list(filtered_modalities.keys()),
                request.top_k,
                answer,
            )
            self.journal.append(
                "rag_cache_store",
                {"user_id": request.user_id, "goal": request.goal, "query": rag_query},
            )
        self.journal.append(
            "rag_answer",
            {"query": rag_query, "support": [fact["id"] for fact in answer.get("support", [])]},
        )

        with self.metrics.time_stage("execute_plan"):
            executions = self._execute_plan(plan, request, filtered_modalities, reasoning)
        with self.metrics.time_stage("profile_signals"):
            profile = self.profiler.bulk_record(request.user_id, request.signals)
        with self.metrics.time_stage("empathy_modulation"):
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
        if self.self_learner:
            self._background_learn(request, answer, executions)
        if self.cache:
            self.cache.put(cache_key, payload)
            self.journal.append("cache_store", {"key": cache_key, "user_id": request.user_id})

        metrics_snapshot = self.metrics.report()
        self.journal.append("slo_snapshot", {"stages": metrics_snapshot})
        return RuntimeResponse(
            plan=plan,
            answer=answer,
            adjustments=dict(adjustments),
            executions=executions,
            reasoning=reasoning,
            journal_tail=self.journal.tail(),
            cached=False,
            metrics=metrics_snapshot,
        )

    def _background_learn(
        self,
        request: RuntimeRequest,
        answer: Mapping[str, object],
        executions: Sequence[SkillExecution],
    ) -> None:
        if not self.self_learner:
            return
        verification = answer.get("verification", {}) if isinstance(answer, Mapping) else {}
        confidence_obj = verification.get("confidence") if isinstance(verification, Mapping) else None
        base_confidence = 0.5
        if isinstance(confidence_obj, (int, float)):
            base_confidence = float(confidence_obj)
        base_confidence = max(0.0, min(base_confidence, 1.0))

        mutated = False
        for execution in executions:
            skill = execution.skill or execution.step_id
            if not skill:
                continue
            status = str(execution.output.get("status", "unknown"))
            gradients: Dict[str, float] = {
                "success": 1.0 if status == "ok" else 0.0,
                "penalty": 1.0 if status not in {"ok", "skipped"} else 0.0,
            }
            if status == "policy_blocked":
                gradients["policy"] = 1.0
            if status == "error":
                gradients["errors"] = 1.0
            self.self_learner.enqueue(
                skill,
                gradients,
                confidence=base_confidence,
                metadata={
                    "goal": request.goal,
                    "status": status,
                    "step_id": execution.step_id,
                },
                user_id=request.user_id,
            )
            mutated = True
        updates = self.self_learner.step()
        if updates:
            self.journal.append(
                "self_learning",
                {
                    "tasks": sorted(updates.keys()),
                    "weights": {task: dict(weights) for task, weights in updates.items()},
                },
            )
        if mutated or updates:
            if self._self_learning_path:
                self._self_learning_dirty = True

    def dispatch_iot_command(
        self,
        session_id: str,
        command: IoTCommand,
        *,
        confirmer: Optional[Callable[[IoTCommand], bool]] = None,
    ) -> Mapping[str, object]:
        """Routes IoT commands through the policy bridge with journalling."""

        if not self.iot_bridge:
            raise RuntimeError("IoT bridge not configured")
        acknowledgement = self.iot_bridge.dispatch(session_id, command, confirmer=confirmer)
        self.journal.append(
            "runtime_iot_dispatch",
            {
                "session_id": session_id,
                "device_id": command.device_id,
                "action": command.action,
                "status": acknowledgement.get("status"),
            },
        )
        return acknowledgement

    def shutdown(self) -> None:
        """Persist self-learning state if a storage path is configured."""

        if self._shutdown:
            return
        self._shutdown = True
        if self.self_learner and self._self_learning_path:
            self.self_learner.save(self._self_learning_path)
            self._self_learning_dirty = False

    def __del__(self) -> None:  # pragma: no cover - best-effort persistence
        try:
            if self._self_learning_dirty:
                self.shutdown()
        except Exception:
            pass

    def ingest_document(self, document: KnowledgeDocument) -> IngestionReport:
        """Adds a document to the knowledge graph via the ingestor."""

        report = self.ingestor.ingest(document, self.graph)
        self.journal.append(
            "knowledge_ingest",
            {
                "document_id": document.doc_id,
                "nodes_added": report.nodes_added,
                "edges_added": report.edges_added,
                "conflicts": list(report.conflicts),
                "warnings": list(report.warnings),
            },
        )
        return report

    def schedule_workflow(
        self,
        goal: str,
        steps: Iterable[Mapping[str, str | None]],
        *,
        deadline: Optional[datetime] = None,
        reminders: Optional[Sequence[ReminderRule]] = None,
        metadata: Optional[Mapping[str, str]] = None,
    ) -> Workflow:
        workflow = self.workflow_manager.create_workflow(
            goal=goal,
            steps=steps,
            deadline=deadline,
            reminders=reminders,
            metadata=metadata,
        )
        self.journal.append(
            "workflow_created",
            {
                "workflow_id": workflow.id,
                "goal": workflow.goal,
                "deadline": workflow.deadline.isoformat() if workflow.deadline else None,
                "step_count": len(workflow.steps),
            },
        )
        return workflow

    def emit_workflow_reminders(self, timestamp: Optional[datetime] = None) -> Sequence[ReminderEvent]:
        events = self.workflow_manager.emit_reminders(timestamp=timestamp)
        for event in events:
            self.journal.append(
                "workflow_reminder",
                {
                    "workflow_id": event.workflow_id,
                    "scheduled_for": event.scheduled_for.isoformat(),
                    "message": event.message,
                },
            )
        return events

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
        user_id: str,
        modalities: Mapping[str, object],
        transcript: str,
        reasoning: ReasoningLog,
    ) -> Tuple[Mapping[str, Sequence[float]], Sequence[ModalitySignal]]:
        embeddings: MutableMapping[str, Sequence[float]] = {}
        signals: List[ModalitySignal] = []
        if transcript:
            text_embedding = self.text_encoder.encode(transcript)
            embeddings["text"] = text_embedding
            signals.append(ModalitySignal(name="text", embedding=text_embedding, quality=0.9))
        audio_value = modalities.get("audio")
        if isinstance(audio_value, (list, tuple)):
            audio_embedding = self.audio_encoder.encode(audio_value, user_id=user_id)
            embeddings["audio"] = audio_embedding
            signals.append(ModalitySignal(name="audio", embedding=audio_embedding, quality=0.7))
        image_value = modalities.get("image")
        if image_value is not None:
            image_embedding = self.image_encoder.encode(image_value)
            embeddings["image"] = image_embedding
            signals.append(ModalitySignal(name="image", embedding=image_embedding, quality=0.6))
        video_value = modalities.get("video")
        if isinstance(video_value, IterableABC):
            video_embedding = self.vision_encoder.encode_video(video_value)
            embeddings["video"] = video_embedding
            signals.append(ModalitySignal(name="video", embedding=video_embedding, quality=0.8))
        sensor_payload = modalities.get("sensors")
        if isinstance(sensor_payload, IterableABC):
            for raw in sensor_payload:
                event = raw if isinstance(raw, SensorEvent) else SensorEvent(**raw)
                self.sensor_hub.ingest(event)
            sequences = self.sensor_hub.to_sequences()
            aligned = self.alignment_engine.align(sequences)
            reasoning.add_step(
                "sensor_alignment",
                f"aligned {len(aligned)} sensor streams",
                list(aligned.keys()),
                confidence=0.5,
            )
        return embeddings, signals

    def _fuse_modalities(
        self,
        embeddings: Mapping[str, Sequence[float]],
        signals: Sequence[ModalitySignal],
        reasoning: ReasoningLog,
    ) -> None:
        fusion_result = None
        if signals and self.cross_fusion:
            fusion_result = self.cross_fusion.fuse(signals, budget=self.fusion_budget)
        elif embeddings:
            fusion_result = self.fusion.fuse(embeddings)
        if fusion_result:
            metadata = dict(fusion_result.metadata or {})
            layers = metadata.get("layers") if isinstance(metadata, Mapping) else None
            if isinstance(layers, Mapping):
                for modality, depth in layers.items():
                    try:
                        self.metrics.observe(f"fusion_layers::{modality}", float(depth))
                    except Exception:  # pragma: no cover - defensive guard against bad metadata
                        continue
            self.journal.append(
                "fusion",
                {
                    "modalities": list(embeddings.keys()),
                    "weights": dict(fusion_result.modality_weights),
                    "embedding_preview": fusion_result.embedding[:4],
                    "metadata": metadata,
                },
            )
            layer_summary = ""
            if isinstance(layers, Mapping) and layers:
                layer_summary = ", ".join(f"{name}:{layers[name]}" for name in sorted(layers))
            message = f"fused {len(embeddings)} modalities"
            if layer_summary:
                message += f" with layers {layer_summary}"
            reasoning.add_step(
                "fusion",
                message,
                fusion_result.modality_weights.keys(),
                confidence=0.6,
            )

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
            granted = self.skill_store.authorize_execution(
                step.skill,
                request.skill_scopes,
                actor=request.user_id,
            )
            self.journal.append(
                "skill_permissions",
                {"step_id": step.id, "skill": step.skill, "granted": granted, "user_id": request.user_id},
            )
            self.skill_store.enforce_policy(step.skill, request.data_tags, actor=request.user_id)
            sandbox_payload = {
                "goal": request.goal,
                "step": step.description,
                "modalities": list(modalities.keys()),
            }
            quota = self.skill_store.quota(step.skill)
            with self.metrics.time_stage(f"skill::{step.skill}"):
                result = self.sandbox.execute(step.skill, sandbox_payload, quota=quota)
            payload = {"status": "ok", "result": result}
            reasoning.add_step("skill", f"executed {step.skill}", [step.id], confidence=0.75)
            self.journal.append(
                "skill_executed",
                {"step_id": step.id, "skill": step.skill, "result_keys": sorted(result.keys())},
            )
        except SkillQuotaExceeded as exc:
            payload = {
                "status": "quota_blocked",
                "reason": str(exc),
                "resource": exc.resource,
                "limit": exc.limit,
                "used": exc.used,
            }
            reasoning.add_step(
                "skill_quota",
                f"{step.skill} quota exhausted ({exc.resource})",
                [step.id],
                confidence=0.2,
            )
            self.journal.append(
                "skill_quota_blocked",
                {
                    "step_id": step.id,
                    "skill": step.skill,
                    "resource": exc.resource,
                    "limit": exc.limit,
                    "used": exc.used,
                    "user_id": request.user_id,
                },
            )
        except SkillPolicyViolation as exc:
            payload = {
                "status": "policy_blocked",
                "reason": str(exc),
                "policy": exc.policy,
                "details": getattr(exc, "details", {}),
            }
            reasoning.add_step("skill_policy", f"{step.skill} blocked by policy", [step.id], confidence=0.2)
            self.journal.append(
                "skill_policy_blocked",
                {
                    "step_id": step.id,
                    "skill": step.skill,
                    "policy": exc.policy,
                    "requirement": exc.requirement,
                    "details": getattr(exc, "details", {}),
                },
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
        tags: Sequence[str],
    ) -> str:
        canonical_modalities: MutableMapping[str, object] = {}
        for key, value in modalities.items():
            canonical_modalities[key] = self._normalise_cache_value(value)
        payload = {
            "user": user_id,
            "goal": goal,
            "modalities": canonical_modalities,
            "transcript": transcript,
            "tags": sorted(tags),
        }
        digest = json.dumps(payload, sort_keys=True, ensure_ascii=False)
        return hashlib.sha256(digest.encode("utf-8")).hexdigest()

    def _normalise_cache_value(self, value: object) -> object:
        if isinstance(value, bytes):
            return hashlib.sha1(value).hexdigest()
        if isinstance(value, (str, int, float, bool)) or value is None:
            return value
        if isinstance(value, IterableABC) and not isinstance(value, (str, bytes)):
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
