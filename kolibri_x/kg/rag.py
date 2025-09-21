"""Retrieval augmented generation pipeline built on the MVP knowledge graph."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, Iterable, List, Mapping, MutableMapping, Optional

from kolibri_x.core.encoders import TextEncoder
from kolibri_x.kg.graph import KnowledgeGraph, Node
from kolibri_x.xai.reasoning import ReasoningLog


@dataclass
class RetrievedFact:
    node: Node
    score: float

    def as_dict(self) -> Mapping[str, object]:
        return {
            "id": self.node.id,
            "text": self.node.text,
            "sources": list(self.node.sources),
            "confidence": self.node.confidence,
            "score": self.score,
        }


class RAGPipeline:
    def __init__(self, graph: KnowledgeGraph, encoder: Optional[TextEncoder] = None) -> None:
        self.graph = graph
        self.encoder = encoder or TextEncoder(dim=32)

    def retrieve(self, query: str, top_k: int = 5) -> List[RetrievedFact]:
        query_vector = self.encoder.encode(query)
        scored: List[RetrievedFact] = []
        for node in self.graph.nodes():
            if not node.text:
                continue
            node_vector = self.encoder.encode(node.text)
            score = self._dot(query_vector, node_vector)
            if score > 0.0:
                scored.append(RetrievedFact(node=node, score=score))
        scored.sort(key=lambda item: item.score, reverse=True)
        return scored[:top_k]

    def answer(self, query: str, top_k: int = 5, reasoning: Optional[ReasoningLog] = None) -> Mapping[str, object]:
        retrieved = self.retrieve(query, top_k=top_k)
        if reasoning is not None:
            reasoning.add_step("retrieve", f"found {len(retrieved)} supporting facts", [fact.node.id for fact in retrieved], confidence=0.6)
        summary = self._summarise(query, retrieved)
        support = [fact.as_dict() for fact in retrieved]
        verification = self.verify_sources(support)
        if reasoning is not None:
            reasoning.add_step("verify", verification["message"], [fact.node.id for fact in retrieved], confidence=verification["confidence"])
        return {
            "query": query,
            "summary": summary,
            "support": support,
            "verification": verification,
        }

    def verify_sources(self, support: Iterable[Mapping[str, object]]) -> Mapping[str, object]:
        missing = []
        for item in support:
            sources = item.get("sources", [])
            if not sources:
                missing.append(item.get("id"))
        if missing:
            return {
                "status": "partial",
                "missing": missing,
                "confidence": 0.2,
                "message": f"missing sources for {len(missing)} facts",
            }
        return {"status": "ok", "missing": [], "confidence": 0.9, "message": "all facts have sources"}

    @staticmethod
    def _dot(left: List[float], right: List[float]) -> float:
        return float(sum(l * r for l, r in zip(left, right)))

    def _summarise(self, query: str, facts: List[RetrievedFact]) -> str:
        if not facts:
            return "no supporting knowledge found"
        lines = [f"Answering: {query}"]
        for fact in facts:
            snippet = fact.node.text.strip().replace("\n", " ")
            lines.append(f"- {snippet} (confidence={fact.node.confidence:.2f})")
        return "\n".join(lines)


__all__ = ["RAGPipeline", "RetrievedFact"]
