import sys
from pathlib import Path

import pytest

sys.path.append(str(Path(__file__).resolve().parents[1]))

from kolibri_x.kg.graph import Edge, KnowledgeGraph, Node  # noqa: E402
from kolibri_x.runtime.journal import ActionJournal  # noqa: E402
from kolibri_x.runtime.orchestrator import KolibriRuntime  # noqa: E402


def test_graph_persistence_roundtrip(tmp_path: Path) -> None:
    graph = KnowledgeGraph()
    graph.add_node(
        Node(
            id="node:primary",
            type="Claim",
            text="Primary node",
            sources=("seed",),
            confidence=0.4,
        )
    )
    graph.add_node(
        Node(
            id="node:secondary",
            type="Entity",
            text="Secondary node",
            confidence=0.6,
        )
    )
    graph.add_edge(
        Edge(
            source="node:primary",
            target="node:secondary",
            relation="supports",
            weight=0.7,
        )
    )
    graph.lazy_update("node:primary", confidence=0.9, metadata={"note": "pending"})

    store_path = tmp_path / "session_graph.jsonl"
    runtime = KolibriRuntime(graph=graph, journal=ActionJournal())
    runtime.start_session("demo-session", graph_path=store_path)
    runtime.end_session()

    assert store_path.exists(), "Graph snapshot should be written on session end"

    fresh_runtime = KolibriRuntime(journal=ActionJournal())
    fresh_runtime.start_session("demo-session", graph_path=store_path)

    loaded_primary = fresh_runtime.graph.get_node("node:primary")
    assert loaded_primary is not None
    assert loaded_primary.text == "Primary node"
    assert loaded_primary.confidence == pytest.approx(0.4)

    edges = fresh_runtime.graph.edges()
    assert any(
        edge.source == "node:primary" and edge.target == "node:secondary" for edge in edges
    )

    assert "node:primary" in fresh_runtime.graph._pending_updates  # type: ignore[attr-defined]
    pending = fresh_runtime.graph._pending_updates["node:primary"]  # type: ignore[attr-defined]
    assert pending["confidence"] == pytest.approx(0.9)
    assert pending["metadata"]["note"] == "pending"

    processed = fresh_runtime.graph.propagate_pending()
    assert "node:primary" in processed

    updated_primary = fresh_runtime.graph.get_node("node:primary")
    assert updated_primary is not None
    assert updated_primary.confidence == pytest.approx(0.9)
    assert updated_primary.metadata.get("note") == "pending"

    fresh_runtime.end_session()
