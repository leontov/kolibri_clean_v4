"""Integration and component tests for the Kolibri runtime stack."""
from __future__ import annotations

import json
from datetime import datetime, timedelta, timezone
import sys
from pathlib import Path
from typing import Mapping

import pytest

sys.path.append(str(Path(__file__).resolve().parents[1]))

from kolibri_x.core.encoders import (  # noqa: E402
    AdaptiveAudioEncoder,
    AdaptiveCrossModalTransformer,
    ContinualLearner,
    DiffusionVisionEncoder,
    ModalitySignal,
    SensorEvent,
    SensorHub,
    TemporalAlignmentEngine,
    TextEncoder,
)
from kolibri_x.core.planner import HierarchicalPlan, NeuroSemanticPlanner  # noqa: E402
from kolibri_x.eval.active_learning import ActiveLearner, CandidateExample  # noqa: E402
from kolibri_x.eval.missions import Mission, MissionPack  # noqa: E402
from kolibri_x.eval.mksi import compute_metrics, metrics_report  # noqa: E402
from kolibri_x.kg.graph import Edge, KnowledgeGraph, Node, VerificationResult  # noqa: E402
from kolibri_x.kg.ingest import (  # noqa: E402
    DomainImportPipeline,
    DomainRecord,
    KnowledgeDocument,
    KnowledgeIngestor,
)
from kolibri_x.kg.rag import RAGPipeline  # noqa: E402
from kolibri_x.personalization import (  # noqa: E402
    Achievement,
    AdaptivePromptSelector,
    EmpathyContext,
    EmpathyModulator,
    InteractionSignal,
    ModelUpdate,
    MotivationEngine,
    OnDeviceProfiler,
    SecureAggregator,
)
from kolibri_x.privacy.consent import AccessProof, PolicyLayer, PrivacyOperator  # noqa: E402
from kolibri_x.runtime.cache import OfflineCache  # noqa: E402
from kolibri_x.runtime.iot import IoTBridge, IoTCommand, IoTPolicy  # noqa: E402
from kolibri_x.runtime.journal import ActionJournal  # noqa: E402
from kolibri_x.runtime.orchestrator import KolibriRuntime, RuntimeRequest, SkillSandbox  # noqa: E402
from kolibri_x.runtime.self_learning import BackgroundSelfLearner  # noqa: E402
from kolibri_x.runtime.workflow import ReminderRule, WorkflowManager  # noqa: E402
from kolibri_x.skills.store import SkillManifest, SkillPolicyViolation, SkillStore  # noqa: E402
from kolibri_x.xai.panel import ExplanationPanel  # noqa: E402
from kolibri_x.xai.reasoning import ReasoningLog  # noqa: E402


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


def _bootstrap_runtime(
    knowledge_graph: KnowledgeGraph,
    skill_store: SkillStore,
    *,
    iot_bridge: IoTBridge | None = None,
    ingestor: KnowledgeIngestor | None = None,
    workflow_manager: WorkflowManager | None = None,
) -> KolibriRuntime:
    sandbox = SkillSandbox()

    def writer_executor(payload: Mapping[str, object]) -> Mapping[str, object]:
        return {"draft": f"Outline for {payload['goal']}", "modalities": payload.get("modalities", [])}

    sandbox.register("writer", writer_executor)

    cache = OfflineCache(ttl=timedelta(minutes=30))
    journal = ActionJournal()
    self_learner = BackgroundSelfLearner()
    runtime = KolibriRuntime(
        graph=knowledge_graph,
        skill_store=skill_store,
        sandbox=sandbox,
        cache=cache,
        journal=journal,
        iot_bridge=iot_bridge,
        knowledge_ingestor=ingestor,
        workflow_manager=workflow_manager,
        self_learner=self_learner,
    )
    runtime.privacy.grant("user-1", ["text"])
    runtime.privacy.grant("eval", ["text"])
    return runtime


def test_skill_store_permission_checks(skill_store: SkillStore) -> None:
    with pytest.raises(SkillPolicyViolation):
        skill_store.authorize_execution("writer", [], actor="user-1")
    granted = skill_store.authorize_execution("writer", ["net.read:whitelist"], actor="user-1")
    assert granted == ["net.read:whitelist"]

    skill_store.enforce_policy("writer", [], actor="user-1")
    with pytest.raises(SkillPolicyViolation):
        skill_store.enforce_policy("writer", ["pii"], actor="user-1")

    audit = list(skill_store.audit_log())
    assert audit and any(entry["decision"] == "deny" for entry in audit)


def test_rag_pipeline_returns_supported_answer(knowledge_graph: KnowledgeGraph) -> None:
    pipeline = RAGPipeline(knowledge_graph, encoder=TextEncoder(dim=16))
    reasoning = ReasoningLog()
    answer = pipeline.answer("How does Kolibri-x deliver autonomy?", top_k=3, reasoning=reasoning)
    assert answer["verification"]["status"] in {"ok", "partial", "conflict"}
    assert "Answering" in answer["summary"]
    assert reasoning.steps(), "reasoning log should not be empty"


def test_graph_hybrid_memory_and_verification(knowledge_graph: KnowledgeGraph) -> None:
    knowledge_graph.register_critic("length", lambda node: min(1.0, len(node.text.split()) / 12))
    knowledge_graph.register_authority(
        "sources",
        lambda node: {"score": 1.0 if node.sources else 0.3, "source_count": len(node.sources)},
    )

    knowledge_graph.add_node(
        Node(
            id="metric:latency",
            type="Metric",
            text="Average latency stays below the adaptive threshold",
            sources=["https://kolibri.example/perf"],
            confidence=0.72,
            embedding=[0.1, 0.2, 0.3],
            memory="operational",
        )
    )
    knowledge_graph.add_node(
        Node(
            id="metric:latency:duplicate",
            type="Metric",
            text="Average latency stays below the adaptive threshold",
            sources=["https://kolibri.example/perf"],
            confidence=0.68,
            embedding=[0.1, 0.2, 0.3],
            memory="operational",
        )
    )
    knowledge_graph.add_edge(
        Edge(
            source="metric:latency",
            target="entity:skillstore",
            relation="supports",
            weight=0.4,
        )
    )
    knowledge_graph.promote_node("metric:latency")

    results = knowledge_graph.verify_with_critics({})
    assert any(result.provenance == "authority" for result in results)
    node = knowledge_graph.get_node("metric:latency")
    assert node is not None and node.metadata.get("verification_score", 0.0) > 0

    duplicates = knowledge_graph.deduplicate_embeddings()
    assert any({"metric:latency", "metric:latency:duplicate"} == set(pair) for pair in duplicates)
    assert knowledge_graph.get_node("metric:latency") is not None
    assert knowledge_graph.get_node("metric:latency:duplicate") is None


def test_graph_lazy_updates_and_conflict_clarifications(knowledge_graph: KnowledgeGraph) -> None:
    knowledge_graph.add_node(
        Node(
            id="claim:reliable",
            type="Claim",
            text="Kolibri runtime is reliable",
            sources=["https://kolibri.example/reliability"],
            confidence=0.7,
        )
    )
    knowledge_graph.add_node(
        Node(
            id="claim:reliable:not",
            type="Claim",
            text="Kolibri runtime is not reliable",
            sources=["https://kolibri.example/issues"],
            confidence=0.6,
        )
    )
    knowledge_graph.add_edge(
        Edge(
            source="claim:reliable",
            target="claim:reliable:not",
            relation="contradicts",
            weight=0.5,
        )
    )

    knowledge_graph.lazy_update("claim:reliable", confidence=0.9, metadata={"reviewed_by": "qa"})
    processed = knowledge_graph.propagate_pending()
    assert "claim:reliable" in processed
    updated = knowledge_graph.get_node("claim:reliable")
    assert updated is not None and updated.confidence == 0.9
    neighbour = knowledge_graph.get_node("claim:reliable:not")
    assert neighbour is not None and "pending_backprop" in neighbour.metadata

    conflicts = knowledge_graph.detect_conflicts()
    assert ("claim:reliable", "claim:reliable:not") in conflicts or (
        "claim:reliable:not",
        "claim:reliable",
    ) in conflicts
    requests = knowledge_graph.generate_clarification_requests()
    assert any({"claim:reliable", "claim:reliable:not"} == set(request["pair"]) for request in requests)


def test_dialogue_compression_and_causal_links(knowledge_graph: KnowledgeGraph) -> None:
    utterances = [
        "User: Outline the launch plan for Kolibri",
        "Assistant: Therefore we should assign responsibilities",
        "User: So the plan stays on schedule",
    ]
    compressed = knowledge_graph.compress_dialogue(utterances, session_id="sess-42")
    assert compressed["events"] and compressed["summary"].startswith("3 events")
    assert compressed["causal_links"], "causal links should capture shared plan topics"
    first_link = compressed["causal_links"][0]
    assert {"cause", "effect", "prediction"} <= set(first_link)


def test_domain_import_pipeline_creates_typed_nodes(knowledge_graph: KnowledgeGraph) -> None:
    pipeline = DomainImportPipeline(encoder=TextEncoder(dim=8))
    records = [
        DomainRecord(
            identifier="account-1",
            source="crm",
            payload={"name": "Acme Corp", "type": "account", "status": "active"},
        ),
        DomainRecord(
            identifier="renewal-1",
            source="crm",
            payload={"title": "Renewal", "date": "2025-06-01", "value": 125000},
        ),
    ]
    report = pipeline.import_records(records, knowledge_graph)
    assert report.nodes_added >= 2
    assert "Account" in report.types
    long_term_nodes = knowledge_graph.nodes(level="long_term")
    assert any(node.id == "record:account-1" for node in long_term_nodes)
    assert any(edge.relation == "describes" for edge in knowledge_graph.edges(level="long_term"))


def test_privacy_operator_controls_access() -> None:
    operator = PrivacyOperator()
    operator.register_layer(PolicyLayer(name="baseline", scope={"text"}, default_action="allow"))
    operator.grant("user-1", ["audio", "text"])
    operator.deny("user-1", ["audio"])
    assert operator.is_allowed("user-1", "text")
    assert not operator.is_allowed("user-1", "audio")
    allowed = operator.enforce("user-1", ["audio", "text", "image"])
    assert allowed == ["text"]


def test_privacy_operator_layers_and_proofs() -> None:
    operator = PrivacyOperator()
    operator.register_layer(PolicyLayer(name="iot", scope={"sensor"}, default_action="deny"))
    operator.grant("user", ["sensor"])
    proofs = operator.record_access("workflow", "user", ["sensor"])
    assert len(proofs) == 1
    assert isinstance(proofs[0], AccessProof)
    allowed = operator.enforce("user", ["unknown"])
    assert allowed == []


def test_planner_aligns_steps_with_skills(skill_store: SkillStore) -> None:
    planner = NeuroSemanticPlanner({manifest.name: manifest for manifest in skill_store.list()})
    plan = planner.plan("Draft and refine the product pitch deck.")
    assert plan.steps[0].skill == "writer"
    hierarchical = planner.hierarchical_plan("Draft and refine the product pitch deck.")
    assert isinstance(hierarchical, HierarchicalPlan)
    assert hierarchical.assignments


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


def test_adaptive_cross_modal_transformer_decides_depth() -> None:
    transformer = AdaptiveCrossModalTransformer(dim=8)
    signals = [
        ModalitySignal(name="text", embedding=[0.5] * 8, quality=0.9, latency_ms=10),
        ModalitySignal(name="image", embedding=[0.1] * 8, quality=0.4, latency_ms=100),
    ]
    result = transformer.fuse(signals, budget=1.5)
    assert set(result.modality_weights) == {"text", "image"}
    assert result.metadata["layers"]["text"] >= result.metadata["layers"]["image"]


def test_diffusion_encoder_and_audio_calibration() -> None:
    encoder = DiffusionVisionEncoder(dim=16, frame_window=2)
    frames = [bytes([index] * 3) for index in range(4)]
    embedding = encoder.encode_video(frames)
    assert len(embedding) == 16
    audio = AdaptiveAudioEncoder(dim=8)
    audio.calibrate("user", [0.1, 0.2, 0.1, 0.2])
    vector = audio.encode([0.3, 0.4, 0.2, 0.1], user_id="user")
    assert len(vector) == 8


def test_sensor_hub_and_alignment() -> None:
    hub = SensorHub()
    hub.ingest(SensorEvent(source="cam", signal_type="gesture", value=0.5, timestamp=10.0))
    hub.ingest(SensorEvent(source="mic", signal_type="speech", value=0.2, timestamp=11.0))
    sequences = hub.to_sequences()
    engine = TemporalAlignmentEngine()
    aligned = engine.align(sequences)
    assert set(aligned)


def test_continual_learner_snapshot() -> None:
    learner = ContinualLearner(consolidation=0.5)
    update1 = learner.train("task-a", {"w1": 0.3, "w2": -0.2})
    update2 = learner.train("task-b", {"w1": 0.1, "w3": 0.4})
    snapshot = learner.snapshot()
    assert snapshot["tasks"] == ["task-a", "task-b"]
    assert update1["w1"] != update2["w1"]


def test_background_self_learning_accumulates_updates() -> None:
    learner = BackgroundSelfLearner()
    learner.enqueue(
        "writer",
        {"success": 1.0, "penalty": 0.0},
        confidence=0.25,
        metadata={"status": "ok"},
        user_id="user-1",
    )
    learner.enqueue(
        "writer",
        {"success": 0.0, "penalty": 1.0},
        confidence=0.9,
        metadata={"status": "error"},
        user_id="user-2",
    )
    updates = learner.step()
    assert "writer" in updates
    writer_update = updates["writer"]
    assert writer_update["success"] > 0
    assert writer_update["penalty"] >= 0
    status = learner.status()
    assert status["pending"]["writer"] == 0
    history = learner.history()
    assert history and history[-1]["updates"].get("writer")


def test_knowledge_graph_verification_and_compression() -> None:
    graph = KnowledgeGraph()
    node_a = Node(id="a", type="Claim", text="A", sources=["s1"], confidence=0.7, embedding=[1.0, 0.0])
    node_b = Node(id="b", type="Claim", text="B", sources=["s2"], confidence=0.65, embedding=[1.0, 0.0])
    graph.add_node(node_a)
    graph.add_node(node_b)
    graph.add_edge(Edge(source="a", target="b", relation="contradicts"))
    duplicates = graph.deduplicate_embeddings()
    assert duplicates
    graph.register_authority("source-check", lambda node: 1.0 if node.sources else 0.0)
    results = graph.verify_with_critics({"critic": lambda node: node.confidence})
    assert all(isinstance(result, VerificationResult) for result in results)
    summary = graph.compress_dialogue(["Discuss project", "Resolve blockers"], "sess")
    assert summary["events"] and summary["summary"].startswith("2 events")
    assert "sess" == summary["session"]
    assert isinstance(graph.generate_clarification_requests(), list)


def test_profiler_and_empathy_modulation() -> None:
    profiler = OnDeviceProfiler(decay=0.9)
    signals = [
        InteractionSignal(type="tone", value=0.3, weight=2.0),
        InteractionSignal(type="tempo", value=1.2, weight=1.5),
        InteractionSignal(type="formality", value=0.6, weight=1.0),
    ]
    profile = profiler.bulk_record("user-42", signals)
    profiler.unlock("user-42", Achievement(identifier="onboarding", description="Completed onboarding"))
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
    selector = AdaptivePromptSelector()
    examples = [
        {"id": 1, "tags": ["design", "pitch"]},
        {"id": 2, "tags": ["legal", "analysis"]},
    ]
    chosen = selector.select_examples(profile, "Design pitch deck", examples)
    assert chosen
    engine = MotivationEngine()
    suggestions = engine.recommend(profile)
    assert suggestions


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
    now = datetime(2025, 1, 1, 9, 0, 0)

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


def test_runtime_orchestrator_end_to_end(
    knowledge_graph: KnowledgeGraph, skill_store: SkillStore
) -> None:
    runtime = _bootstrap_runtime(knowledge_graph, skill_store)
    request = RuntimeRequest(
        user_id="user-1",
        goal="Draft an investor pitch deck",
        modalities={"text": "Need a compelling narrative for Kolibri-x."},
        hints=["writer"],
        signals=[InteractionSignal(type="tone", value=0.2, weight=1.5)],
        empathy=EmpathyContext(sentiment=0.1, urgency=0.3, energy=0.2),
        top_k=3,
        skill_scopes=["net.read:whitelist"],
    )
    response = runtime.process(request)
    assert response.plan.steps, "planner should produce at least one step"
    assert response.executions[0].output["status"] == "ok"
    assert response.answer["support"], "RAG pipeline should return supporting facts"
    assert "tone" in response.adjustments and "tempo" in response.adjustments
    assert runtime.self_learner is not None
    history = runtime.self_learner.history()
    assert history and any(entry["updates"] for entry in history)
    assert runtime.journal.verify(), "journal chain must remain consistent"
    assert not response.cached
    assert "privacy_enforce" in response.metrics
    cached_response = runtime.process(request)
    assert cached_response.cached, "second invocation should use offline cache"
    assert cached_response.answer == response.answer
    assert cached_response.executions[0].output == response.executions[0].output
    journal_events = [entry.event for entry in runtime.journal.tail(5)]
    assert "slo_snapshot" in journal_events
    all_events = [entry.event for entry in runtime.journal.entries()]
    assert "self_learning" in all_events


def test_skill_policy_violation_blocks_execution(
    knowledge_graph: KnowledgeGraph, skill_store: SkillStore
) -> None:
    runtime = _bootstrap_runtime(knowledge_graph, skill_store)
    request = RuntimeRequest(
        user_id="user-1",
        goal="Draft an investor pitch deck",
        modalities={"text": "Need a compelling narrative for Kolibri-x."},
        hints=["writer"],
        data_tags=["pii"],
        skill_scopes=["net.read:whitelist"],
    )
    response = runtime.process(request)
    assert response.executions[0].output["status"] == "policy_blocked"
    assert response.answer["verification"]["status"] in {"partial", "ok", "conflict"}


def test_explanation_panel_surfaces_reasoning(
    knowledge_graph: KnowledgeGraph, skill_store: SkillStore
) -> None:
    runtime = _bootstrap_runtime(knowledge_graph, skill_store)
    request = RuntimeRequest(
        user_id="user-1",
        goal="Draft an investor pitch deck",
        modalities={"text": "Need a compelling narrative for Kolibri-x."},
        hints=["writer"],
        skill_scopes=["net.read:whitelist"],
    )
    response = runtime.process(request)
    panel = ExplanationPanel(
        plan=response.plan,
        reasoning=response.reasoning,
        answer=response.answer,
        adjustments=response.adjustments,
        journal_entries=runtime.journal.tail(10),
    )
    data = panel.to_dict()
    assert data["timeline"]["steps"], "timeline should expose reasoning steps"
    assert data["evidence"], "evidence should include supporting references"
    assert data["adjustments"], "empathy adjustments propagated to panel"


def test_runtime_ingestion_and_workflow_management(
    knowledge_graph: KnowledgeGraph, skill_store: SkillStore
) -> None:
    ingestor = KnowledgeIngestor()
    workflow_manager = WorkflowManager(time_provider=lambda: datetime(2025, 1, 1, tzinfo=timezone.utc))
    runtime = _bootstrap_runtime(
        knowledge_graph,
        skill_store,
        ingestor=ingestor,
        workflow_manager=workflow_manager,
    )
    document = KnowledgeDocument(
        doc_id="security-review",
        source="https://kolibri.example/security",
        title="Security posture",
        content="Kolibri keeps data secure. Kolibri does not keep data secure.",
    )
    report = runtime.ingest_document(document)
    assert report.nodes_added >= 3
    assert report.conflicts, "conflicting claims should be linked"
    assert knowledge_graph.detect_conflicts()
    deadline = datetime(2025, 1, 5, tzinfo=timezone.utc)
    workflow = runtime.schedule_workflow(
        goal="Close security review",
        steps=[{"description": "Draft summary", "tool": "writer"}, {"description": "Share with board"}],
        deadline=deadline,
        reminders=[ReminderRule(offset=timedelta(days=1), message="24h remaining")],
    )
    assert workflow.goal == "Close security review"
    events = runtime.emit_workflow_reminders(timestamp=datetime(2025, 1, 4, 12, tzinfo=timezone.utc))
    assert events and events[0].workflow_id == workflow.id


def test_mission_pack_and_metrics(knowledge_graph: KnowledgeGraph, skill_store: SkillStore) -> None:
    runtime = _bootstrap_runtime(knowledge_graph, skill_store)
    runtime.privacy.grant("eval", ["text"])
    mission = Mission(
        identifier="doc:writer_pitch",
        description="Draft investor pitch deck",
        goal="Draft an investor pitch deck",
        modalities={"text": "Need investor pitch"},
        expected_skills=["writer"],
        skill_scopes=["net.read:whitelist"],
    )
    pack = MissionPack(name="demo", missions=[mission])
    outcomes = pack.execute(runtime)
    assert outcomes[0].skill_coverage() == 1.0
    metrics = compute_metrics(outcomes)
    assert 0.0 <= metrics.mksi <= 1.0
    assert metrics.generalization > 0.0
    logs_dir = Path("logs")
    logs_dir.mkdir(parents=True, exist_ok=True)
    artifact_path = logs_dir / "mksi_report.json"
    report_payload = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "metrics": {
            "generalization": metrics.generalization,
            "parsimony": metrics.parsimony,
            "autonomy": "pending",
            "reliability": "pending",
            "explainability": "pending",
            "usability": "pending",
            "mksi": metrics.mksi,
        },
        "missions": [outcome.mission_id for outcome in outcomes],
    }
    artifact_path.write_text(json.dumps(report_payload, ensure_ascii=False, indent=2), encoding="utf-8")
    assert artifact_path.exists()
    snapshot = metrics_report(outcomes)
    assert snapshot["generalization"] == pytest.approx(metrics.generalization)
