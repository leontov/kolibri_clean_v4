from datetime import datetime, timedelta
import sys
from pathlib import Path

import pytest

sys.path.append(str(Path(__file__).resolve().parents[1]))

from kolibri_x.core.encoders import TextEncoder
from kolibri_x.core.planner import NeuroSemanticPlanner
from kolibri_x.kg.graph import Edge, KnowledgeGraph, Node
from kolibri_x.kg.rag import RAGPipeline
from kolibri_x.eval.active_learning import ActiveLearner, CandidateExample
from kolibri_x.personalization import (
    EmpathyContext,
    EmpathyModulator,
    InteractionSignal,
    ModelUpdate,
    OnDeviceProfiler,
    SecureAggregator,
)
from kolibri_x.privacy.consent import PrivacyOperator
from kolibri_x.runtime.cache import OfflineCache
from kolibri_x.runtime.workflow import ReminderRule, WorkflowManager
from kolibri_x.skills.store import SkillManifest, SkillStore
from kolibri_x.xai.reasoning import ReasoningLog


@pytest.fixture()
def knowledge_graph() -> KnowledgeGraph:
    graph = KnowledgeGraph()
    graph.add_node(
        Node(
            id="claim:collaboration",
            type="Claim",
            text="Kolibri-x orchestrates skills to deliver autonomous project support",
            sources=["https://kolibri.example/whitepaper"],
            confidence=0.82,
        )
    )
    graph.add_node(
        Node(
            id="entity:skillstore",
            type="Entity",
            text="SkillStore provides sandboxed execution with declarative manifests",
            sources=["https://kolibri.example/docs/skillstore"],
            confidence=0.77,
        )
    )
    graph.add_edge(Edge(source="claim:collaboration", target="entity:skillstore", relation="supports", weight=0.6))
    return graph


@pytest.fixture()
def skill_store() -> SkillStore:
    store = SkillStore()
    manifest = SkillManifest.from_dict(
        {
            "name": "writer",
            "version": "0.1.0",
            "inputs": ["text"],
            "permissions": ["net.read:whitelist"],
            "billing": "per_call",
            "policy": {"pii": "deny"},
            "entry": "writer.py",
        }
    )
    store.register(manifest)
    return store


def test_rag_pipeline_returns_supported_answer(knowledge_graph: KnowledgeGraph) -> None:
    pipeline = RAGPipeline(knowledge_graph, encoder=TextEncoder(dim=16))
    reasoning = ReasoningLog()
    answer = pipeline.answer("How does Kolibri-x deliver autonomy?", top_k=3, reasoning=reasoning)
    assert answer["verification"]["status"] == "ok"
    assert "autonomous project support" in answer["summary"]
    assert reasoning.steps(), "reasoning log should not be empty"


def test_privacy_operator_controls_access() -> None:
    operator = PrivacyOperator()
    operator.grant("user-1", ["audio", "text"])
    operator.deny("user-1", ["audio"])
    assert operator.is_allowed("user-1", "text")
    assert not operator.is_allowed("user-1", "audio")
    assert operator.enforce("user-1", ["audio", "text", "image"]) == ["text"]


def test_skill_store_permission_checks(skill_store: SkillStore) -> None:
    assert skill_store.require_permissions("writer", ["net.read:whitelist"])
    with pytest.raises(KeyError):
        skill_store.require_permissions("unknown", [])


def test_planner_aligns_steps_with_skills(skill_store: SkillStore) -> None:
    planner = NeuroSemanticPlanner({manifest.name: manifest for manifest in skill_store.list()})
    plan = planner.plan("Draft and refine the product pitch deck.")
    assert plan.steps[0].skill == "writer"


def test_offline_cache_eviction() -> None:
    clock = datetime(2025, 1, 1, 12, 0, 0)

    def time_provider() -> datetime:
        return clock

    cache = OfflineCache(ttl=timedelta(minutes=5), time_provider=time_provider)
    cache.put("answer", {"text": "cached"})
    assert cache.get("answer") == {"text": "cached"}
    clock += timedelta(minutes=10)  # type: ignore[operator]
    assert cache.get("answer") is None
    assert cache.size() == 0


def test_profiler_and_empathy_modulation() -> None:
    profiler = OnDeviceProfiler(decay=0.9)
    signals = [
        InteractionSignal(type="tone", value=0.3, weight=2.0),
        InteractionSignal(type="tempo", value=1.2, weight=1.5),
        InteractionSignal(type="formality", value=0.6, weight=1.0),
    ]
    profile = profiler.bulk_record("user-42", signals)
    aggregator = SecureAggregator()
    aggregator.submit(ModelUpdate(user_id="user-42", values={"tone": profile.tone_preference}, clipping=0.5))
    aggregator.submit(ModelUpdate(user_id="user-99", values={"tone": -0.1}, clipping=1.0))
    aggregated = aggregator.aggregate()
    assert pytest.approx(aggregated["tone"], rel=1e-3) == 0.05

    context = EmpathyContext(sentiment=-0.2, urgency=0.8, energy=0.1)
    modulator = EmpathyModulator()
    adjustments = modulator.modulation(profile, context)
    assert -1.0 <= adjustments["tone"] <= 1.0
    assert 0.2 <= adjustments["tempo"] <= 3.0
    assert "style::formality" in adjustments


def test_active_learning_prioritises_low_confidence_domains() -> None:
    learner = ActiveLearner(budget=2)
    candidates = [
        CandidateExample(uid="a", confidence=0.95, metadata={"domain": "code"}),
        CandidateExample(uid="b", confidence=0.55, metadata={"domain": "legal"}),
        CandidateExample(uid="c", confidence=0.40, metadata={"domain": "legal"}),
    ]
    coverage = {"code": 0.6, "legal": 0.2}
    requests = learner.propose_annotations(candidates, coverage)
    assert [request.uid for request in requests] == ["c", "b"]
    assert requests[0].priority >= requests[1].priority


def test_workflow_manager_tracks_progress_and_reminders() -> None:
    now = datetime(2025, 1, 1, 9, 0, 0, tzinfo=None)

    def time_provider() -> datetime:
        return now

    manager = WorkflowManager(time_provider=time_provider)
    deadline = datetime(2025, 1, 3, 9, 0, 0)
    reminders = [ReminderRule(offset=timedelta(days=1), message="24h remaining")]
    workflow = manager.create_workflow(
        goal="Prepare investor update",
        steps=[{"description": "Collect metrics", "tool": "analytics"}, {"description": "Draft narrative"}],
        deadline=deadline,
        reminders=reminders,
    )
    assert workflow.progress() == 0.0
    manager.mark_step_completed(workflow.id, 0)
    assert workflow.steps[0].completed
    assert 0.0 < workflow.progress() < 1.0

    reminder_events = manager.emit_reminders(timestamp=datetime(2025, 1, 3, 8, 0, 0))
    assert reminder_events
    assert reminder_events[0].workflow_id == workflow.id
    assert reminder_events[0].message == "24h remaining"

    overdue = manager.overdue_workflows(timestamp=datetime(2025, 1, 4, 9, 0, 0))
    assert workflow in overdue
