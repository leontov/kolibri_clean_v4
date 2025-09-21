"""Minimal knowledge graph implementation for Kolibri."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Dict, Iterable, List, Mapping, MutableMapping, Sequence, Tuple


@dataclass(frozen=True)
class Node:
    id: str
    type: str
    text: str
    sources: Sequence[str] = field(default_factory=tuple)
    confidence: float = 0.5
    embedding: Sequence[float] = field(default_factory=tuple)
    metadata: Mapping[str, object] = field(default_factory=dict)


@dataclass(frozen=True)
class Edge:
    source: str
    target: str
    relation: str
    weight: float = 1.0


@dataclass(frozen=True)
class VerificationResult:
    node_id: str
    critic: str
    score: float


class KnowledgeGraph:
    """Stores nodes, edges, and simple conflict heuristics."""

    def __init__(self) -> None:
        self._nodes: MutableMapping[str, Node] = {}
        self._edges: List[Edge] = []

    def add_node(self, node: Node) -> None:
        self._nodes[node.id] = node

    def add_edge(self, edge: Edge) -> None:
        self._edges.append(edge)

    def nodes(self) -> Sequence[Node]:
        return tuple(self._nodes.values())

    def edges(self) -> Sequence[Edge]:
        return tuple(self._edges)

    def deduplicate_embeddings(self) -> List[Tuple[str, str]]:
        seen: Dict[Tuple[float, ...], str] = {}
        duplicates: List[Tuple[str, str]] = []
        for node in self._nodes.values():
            key = tuple(float(value) for value in node.embedding)
            if key and key in seen:
                duplicates.append((seen[key], node.id))
            elif key:
                seen[key] = node.id
        return duplicates

    def verify_with_critics(self, critics: Mapping[str, Callable[[Node], float]]) -> List[VerificationResult]:
        results: List[VerificationResult] = []
        for name, critic in critics.items():
            for node in self._nodes.values():
                results.append(VerificationResult(node_id=node.id, critic=name, score=float(critic(node))))
        return results

    def compress_dialogue(self, utterances: Sequence[str], session_id: str) -> Mapping[str, object]:
        events = [
            {
                "session": session_id,
                "utterance": utterance,
                "token_count": len(utterance.split()),
            }
            for utterance in utterances
        ]
        return {"events": events, "summary": " ".join(utterances[:2])[:120]}

    def conflict_queries(self) -> List[Tuple[str, str]]:
        return [
            (edge.source, edge.target)
            for edge in self._edges
            if edge.relation in {"contradicts", "conflicts_with"}
        ]

    def detect_conflicts(self) -> List[Tuple[str, str]]:
        conflicts = self.conflict_queries()
        return conflicts


__all__ = ["Edge", "KnowledgeGraph", "Node", "VerificationResult"]
