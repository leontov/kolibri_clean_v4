"""Lightweight knowledge graph implementation for the Kolibri-x MVP."""
from __future__ import annotations

from dataclasses import dataclass, field
import json
from pathlib import Path
from typing import Dict, Iterable, Iterator, List, Mapping, MutableMapping, Optional, Sequence, Tuple


@dataclass
class Node:
    id: str
    type: str
    text: str
    sources: List[str] = field(default_factory=list)
    confidence: float = 1.0
    metadata: MutableMapping[str, object] = field(default_factory=dict)

    def to_dict(self) -> Mapping[str, object]:
        return {
            "id": self.id,
            "type": self.type,
            "text": self.text,
            "sources": list(self.sources),
            "confidence": self.confidence,
            "metadata": dict(self.metadata),
        }


@dataclass
class Edge:
    source: str
    target: str
    relation: str
    weight: float = 1.0
    evidence: Optional[str] = None

    def to_dict(self) -> Mapping[str, object]:
        return {
            "source": self.source,
            "target": self.target,
            "relation": self.relation,
            "weight": self.weight,
            "evidence": self.evidence,
        }


class KnowledgeGraph:
    def __init__(self) -> None:
        self._nodes: Dict[str, Node] = {}
        self._edges: List[Edge] = []
        self._adjacency: Dict[str, List[Edge]] = {}

    def add_node(self, node: Node) -> None:
        if node.id in self._nodes:
            existing = self._nodes[node.id]
            existing.text = node.text or existing.text
            existing.confidence = max(existing.confidence, node.confidence)
            existing.metadata.update(node.metadata)
            for source in node.sources:
                if source not in existing.sources:
                    existing.sources.append(source)
        else:
            self._nodes[node.id] = node
        self._adjacency.setdefault(node.id, [])

    def add_edge(self, edge: Edge) -> None:
        if edge.source not in self._nodes or edge.target not in self._nodes:
            raise KeyError("both endpoints must exist before adding an edge")
        self._edges.append(edge)
        self._adjacency.setdefault(edge.source, []).append(edge)

    def get_node(self, node_id: str) -> Optional[Node]:
        return self._nodes.get(node_id)

    def neighbours(self, node_id: str, relation: Optional[str] = None) -> List[Edge]:
        edges = self._adjacency.get(node_id, [])
        if relation is None:
            return list(edges)
        return [edge for edge in edges if edge.relation == relation]

    def nodes(self) -> Iterator[Node]:
        return iter(self._nodes.values())

    def edges(self) -> Iterator[Edge]:
        return iter(self._edges)

    def detect_conflicts(self) -> List[Tuple[str, str]]:
        conflicts: List[Tuple[str, str]] = []
        for edge in self._edges:
            if edge.relation == "contradicts":
                conflicts.append((edge.source, edge.target))
        return conflicts

    def validate_sources(self) -> Dict[str, List[str]]:
        missing: Dict[str, List[str]] = {}
        for node in self._nodes.values():
            if not node.sources:
                missing[node.id] = ["missing_sources"]
        return missing

    def query(self, text: str, limit: int = 5) -> List[Node]:
        text_lower = text.lower()
        scored: List[Tuple[float, Node]] = []
        for node in self._nodes.values():
            if not node.text:
                continue
            score = self._score(text_lower, node.text.lower())
            if score:
                scored.append((score, node))
        scored.sort(key=lambda item: item[0], reverse=True)
        return [node for _, node in scored[:limit]]

    @staticmethod
    def _score(query: str, candidate: str) -> float:
        if query in candidate:
            return float(len(query)) / (len(candidate) + 1)
        overlap = len(set(query.split()) & set(candidate.split()))
        return float(overlap)

    def export_jsonl(self, path: str | Path) -> None:
        with open(path, "w", encoding="utf-8") as handle:
            for node in self._nodes.values():
                handle.write(json.dumps({"type": "node", **node.to_dict()}, ensure_ascii=False) + "\n")
            for edge in self._edges:
                handle.write(json.dumps({"type": "edge", **edge.to_dict()}, ensure_ascii=False) + "\n")

    @classmethod
    def import_jsonl(cls, path: str | Path) -> "KnowledgeGraph":
        graph = cls()
        with open(path, "r", encoding="utf-8") as handle:
            for line in handle:
                record = json.loads(line)
                if record.get("type") == "node":
                    graph.add_node(
                        Node(
                            id=record["id"],
                            type=record.get("type_label", record.get("type", "Entity")),
                            text=record.get("text", ""),
                            sources=record.get("sources", []),
                            confidence=record.get("confidence", 1.0),
                            metadata=record.get("metadata", {}),
                        )
                    )
                elif record.get("type") == "edge":
                    graph.add_edge(
                        Edge(
                            source=record["source"],
                            target=record["target"],
                            relation=record.get("relation", "relates"),
                            weight=record.get("weight", 1.0),
                            evidence=record.get("evidence"),
                        )
                    )
        return graph


__all__ = ["Edge", "KnowledgeGraph", "Node"]
