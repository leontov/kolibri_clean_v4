"""Knowledge graph with multi-tier storage and fact verification."""
from __future__ import annotations

from dataclasses import dataclass, field
from itertools import combinations
import json
import math
from pathlib import Path
from statistics import fmean
from typing import Callable, Dict, Iterable, Iterator, List, Mapping, MutableMapping, Optional, Sequence, Tuple


@dataclass
class Node:
    id: str
    type: str
    text: str
    sources: List[str] = field(default_factory=list)
    confidence: float = 1.0
    metadata: MutableMapping[str, object] = field(default_factory=dict)
    tier: str = "hot"
    embedding: Optional[List[float]] = None

    def to_dict(self) -> Mapping[str, object]:
        return {
            "id": self.id,
            "type": self.type,
            "text": self.text,
            "sources": list(self.sources),
            "confidence": self.confidence,
            "metadata": dict(self.metadata),
            "tier": self.tier,
            "embedding": list(self.embedding) if self.embedding else None,
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


@dataclass
class VerificationResult:
    node_id: str
    critic_scores: Mapping[str, float]
    external_sources: Sequence[str]
    status: str


class KnowledgeGraph:
    """Stores facts across hot/warm/cold tiers with conflict detection."""

    def __init__(self) -> None:
        self._nodes: Dict[str, Node] = {}
        self._edges: List[Edge] = []
        self._adjacency: Dict[str, List[Edge]] = {}
        self._tiers: Dict[str, List[str]] = {"hot": [], "warm": [], "cold": []}

    def add_node(self, node: Node) -> None:
        tier = node.tier if node.tier in self._tiers else "hot"
        if node.id in self._nodes:
            existing = self._nodes[node.id]
            existing.text = node.text or existing.text
            existing.confidence = max(existing.confidence, node.confidence)
            existing.metadata.update(node.metadata)
            existing.embedding = node.embedding or existing.embedding
            existing.tier = tier
            for source in node.sources:
                if source not in existing.sources:
                    existing.sources.append(source)
        else:
            node.tier = tier
            self._nodes[node.id] = node
            self._tiers.setdefault(tier, []).append(node.id)
        self._adjacency.setdefault(node.id, [])

    def promote(self, node_id: str, target_tier: str) -> None:
        if node_id not in self._nodes:
            raise KeyError(node_id)
        if target_tier not in self._tiers:
            raise KeyError(target_tier)
        current = self._nodes[node_id].tier
        if node_id in self._tiers.get(current, []):
            self._tiers[current].remove(node_id)
        self._tiers[target_tier].append(node_id)
        self._nodes[node_id].tier = target_tier

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

    def nodes(self, tier: Optional[str] = None) -> Iterator[Node]:
        if tier is None:
            return iter(self._nodes.values())
        ids = self._tiers.get(tier, [])
        return (self._nodes[node_id] for node_id in ids)

    def edges(self) -> Iterator[Edge]:
        return iter(self._edges)

    def detect_conflicts(self) -> List[Tuple[str, str, Mapping[str, object]]]:
        conflicts: List[Tuple[str, str, Mapping[str, object]]] = []
        for edge in self._edges:
            if edge.relation == "contradicts":
                conflicts.append((edge.source, edge.target, {"evidence": edge.evidence}))
        return conflicts

    def conflict_queries(self) -> List[str]:
        queries = []
        for source, target, _ in self.detect_conflicts():
            a = self._nodes.get(source)
            b = self._nodes.get(target)
            text_a = a.text if a else source
            text_b = b.text if b else target
            queries.append(f"clarify relationship between {text_a!r} and {text_b!r}")
        return queries

    def validate_sources(self) -> Dict[str, List[str]]:
        missing: Dict[str, List[str]] = {}
        for node in self._nodes.values():
            signals = []
            if not node.sources:
                signals.append("missing_sources")
            if node.confidence < 0.5:
                signals.append("low_confidence")
            if signals:
                missing[node.id] = signals
        return missing

    def verify_with_critics(
        self,
        critics: Mapping[str, Callable[[Node], float]],
        external_lookup: Optional[Callable[[Node], Sequence[str]]] = None,
    ) -> List[VerificationResult]:
        results: List[VerificationResult] = []
        for node in self._nodes.values():
            critic_scores = {name: max(0.0, min(1.0, scorer(node))) for name, scorer in critics.items()}
            external = list(external_lookup(node)) if external_lookup else []
            avg_score = fmean(critic_scores.values()) if critic_scores else node.confidence
            status = "ok" if avg_score >= 0.6 else "needs_review"
            if external:
                status = "ok" if status == "ok" else "pending_external"
            results.append(
                VerificationResult(
                    node_id=node.id,
                    critic_scores=critic_scores,
                    external_sources=external,
                    status=status,
                )
            )
        return results

    def deduplicate_embeddings(self, threshold: float = 0.98) -> List[Tuple[str, str]]:
        duplicates: List[Tuple[str, str]] = []
        items = [node for node in self._nodes.values() if node.embedding]
        for left, right in combinations(items, 2):
            similarity = self._cosine_similarity(left.embedding, right.embedding)
            if similarity >= threshold:
                duplicates.append((left.id, right.id))
                if left.confidence >= right.confidence:
                    self._merge_nodes(left, right)
                else:
                    self._merge_nodes(right, left)
        return duplicates

    def compress_dialogue(self, turns: Sequence[str], session_id: str) -> Mapping[str, object]:
        summary = []
        for index, turn in enumerate(turns):
            key_event = {
                "event_id": f"{session_id}-{index}",
                "abstract": turn[:120],
                "sentiment": self._sentiment(turn),
            }
            summary.append(key_event)
        return {"session_id": session_id, "events": summary}

    def propagate_revision(self, node_id: str, update: Mapping[str, object]) -> None:
        node = self._nodes.get(node_id)
        if not node:
            raise KeyError(node_id)
        node.metadata.update(update)
        node.confidence = min(1.0, update.get("confidence", node.confidence))
        for edge in self._adjacency.get(node_id, []):
            target = self._nodes.get(edge.target)
            if target:
                target.metadata.setdefault("revision_traces", []).append({"source": node_id, **update})

    def import_domain_corpus(self, records: Iterable[Mapping[str, object]], source: str) -> None:
        for record in records:
            node = Node(
                id=str(record.get("id")),
                type=str(record.get("type", "Entity")),
                text=str(record.get("text", "")),
                sources=[source] + list(record.get("sources", [])),
                confidence=float(record.get("confidence", 0.8)),
                metadata=dict(record.get("metadata", {})),
                tier=str(record.get("tier", "warm")),
            )
            self.add_node(node)

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
                            tier=record.get("tier", "hot"),
                            embedding=record.get("embedding"),
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

    def _score(self, query: str, candidate: str) -> float:
        if query in candidate:
            return float(len(query)) / (len(candidate) + 1)
        overlap = len(set(query.split()) & set(candidate.split()))
        return float(overlap)

    @staticmethod
    def _cosine_similarity(left: Sequence[float], right: Sequence[float]) -> float:
        numerator = sum(float(a) * float(b) for a, b in zip(left, right))
        denom_left = math.sqrt(sum(float(a) ** 2 for a in left)) or 1.0
        denom_right = math.sqrt(sum(float(b) ** 2 for b in right)) or 1.0
        return numerator / (denom_left * denom_right)

    def _merge_nodes(self, winner: Node, loser: Node) -> None:
        for source in loser.sources:
            if source not in winner.sources:
                winner.sources.append(source)
        winner.metadata.setdefault("aliases", []).append(loser.id)
        self._nodes.pop(loser.id, None)
        for tier, ids in self._tiers.items():
            if loser.id in ids:
                ids.remove(loser.id)

    @staticmethod
    def _sentiment(text: str) -> float:
        positive = sum(word in text.lower() for word in ("good", "great", "success"))
        negative = sum(word in text.lower() for word in ("bad", "issue", "delay"))
        return max(-1.0, min(1.0, positive * 0.3 - negative * 0.3))


__all__ = [
    "Edge",
    "KnowledgeGraph",
    "Node",
    "VerificationResult",
]
