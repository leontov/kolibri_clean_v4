"""Knowledge ingestion helpers that turn documents into graph facts."""
from __future__ import annotations

from dataclasses import dataclass, field
import re
from typing import Dict, Iterable, List, Mapping, Optional, Sequence, Tuple

from kolibri_x.core.encoders import TextEncoder
from kolibri_x.kg.graph import Edge, KnowledgeGraph, Node


@dataclass
class KnowledgeDocument:
    """Container describing a document slated for ingestion."""

    doc_id: str
    source: str
    title: str
    content: str
    tags: Sequence[str] = field(default_factory=tuple)


@dataclass
class IngestionReport:
    """Summary of graph mutations performed by the ingestor."""

    document_id: str
    nodes_added: int
    edges_added: int
    conflicts: Sequence[Tuple[str, str]]
    warnings: Sequence[str] = field(default_factory=tuple)


class KnowledgeIngestor:
    """Heuristic document ingestor for the MVP knowledge graph."""

    def __init__(self, encoder: Optional[TextEncoder] = None, min_length: int = 12) -> None:
        self.encoder = encoder or TextEncoder(dim=32)
        self.min_length = max(1, int(min_length))

    def ingest(self, document: KnowledgeDocument, graph: KnowledgeGraph) -> IngestionReport:
        sentences = self._split(document.content)
        nodes_added = 0
        edges_added = 0
        conflicts: List[Tuple[str, str]] = []
        warnings: List[str] = []

        source_id = f"source:{document.doc_id}"
        graph.add_node(
            Node(
                id=source_id,
                type="Source",
                text=document.title or document.source,
                sources=[document.source],
                confidence=0.9,
                metadata={"tags": list(document.tags)},
            )
        )
        nodes_added += 1

        for index, sentence in enumerate(sentences, start=1):
            if len(sentence) < self.min_length:
                warnings.append(f"discarded_short_sentence:{index}")
                continue

            claim_id = f"claim:{document.doc_id}:{index:04d}"
            confidence = self._confidence(sentence)
            claim_node = Node(
                id=claim_id,
                type="Claim",
                text=sentence,
                sources=[document.source],
                confidence=confidence,
                metadata={"document_id": document.doc_id, "position": index},
            )

            existing = self._find_duplicate(graph, claim_node.text)
            if existing:
                warnings.append(f"duplicate:{index}:{existing.id}")
                continue

            graph.add_node(claim_node)
            nodes_added += 1

            graph.add_edge(
                Edge(
                    source=source_id,
                    target=claim_id,
                    relation="mentions",
                    weight=confidence,
                )
            )
            edges_added += 1

            conflicts.extend(self._link_conflicts(graph, claim_node))

        return IngestionReport(
            document_id=document.doc_id,
            nodes_added=nodes_added,
            edges_added=edges_added,
            conflicts=tuple(conflicts),
            warnings=tuple(warnings),
        )

    # ------------------------------------------------------------------
    # Domain database import helpers
    # ------------------------------------------------------------------


@dataclass
class DomainRecord:
    """Domain specific entry that should be represented in the graph."""

    identifier: str
    source: str
    payload: Mapping[str, object]
    tags: Sequence[str] = field(default_factory=tuple)


@dataclass
class DomainImportReport:
    """Summary of the domain import pipeline execution."""

    nodes_added: int
    edges_added: int
    types: Mapping[str, int]
    sources: Sequence[str]


class DomainImportPipeline:
    """Convert structured domain records into typed graph nodes and edges."""

    def __init__(self, encoder: Optional[TextEncoder] = None) -> None:
        self.encoder = encoder or TextEncoder(dim=24)

    def import_records(self, records: Iterable[DomainRecord], graph: KnowledgeGraph) -> DomainImportReport:
        nodes_added = 0
        edges_added = 0
        type_counter: Dict[str, int] = {}
        linked_sources: List[str] = []
        for record in records:
            node_type = self._infer_type(record)
            text = self._format_payload(record)
            embedding = self.encoder.encode(text)
            node_id = f"record:{record.identifier}"
            node = Node(
                id=node_id,
                type=node_type,
                text=text,
                sources=[record.source],
                confidence=0.75,
                embedding=embedding,
                metadata={"payload": dict(record.payload), "tags": list(record.tags)},
                memory="long_term",
            )
            graph.add_node(node, memory="long_term")
            nodes_added += 1
            type_counter[node_type] = type_counter.get(node_type, 0) + 1
            if record.source not in linked_sources:
                linked_sources.append(record.source)

            source_id = f"source:{record.source}"
            if graph.get_node(source_id) is None:
                graph.add_node(
                    Node(
                        id=source_id,
                        type="Source",
                        text=record.source,
                        sources=[record.source],
                        confidence=0.8,
                        metadata={"auto_created": True},
                        memory="long_term",
                    ),
                    memory="long_term",
                )
                nodes_added += 1

            graph.add_edge(
                Edge(
                    source=source_id,
                    target=node_id,
                    relation="describes",
                    weight=0.8,
                    memory="long_term",
                    metadata={"origin": "domain_import"},
                ),
                memory="long_term",
            )
            edges_added += 1

        return DomainImportReport(
            nodes_added=nodes_added,
            edges_added=edges_added,
            types=dict(type_counter),
            sources=tuple(sorted(linked_sources)),
        )

    def _infer_type(self, record: DomainRecord) -> str:
        payload = record.payload
        if "type" in payload:
            return str(payload["type"]).title()
        numeric_fields = [key for key, value in payload.items() if isinstance(value, (int, float))]
        if numeric_fields and len(payload) <= 3:
            return "Metric"
        if any("date" in key.lower() for key in payload):
            return "Event"
        if any(isinstance(value, (list, tuple)) for value in payload.values()):
            return "Collection"
        return "Fact"

    def _format_payload(self, record: DomainRecord) -> str:
        payload = record.payload
        title = str(payload.get("name") or payload.get("title") or record.identifier)
        important_fields = [
            f"{key}={value}" for key, value in payload.items() if key not in {"name", "title", "type"}
        ]
        context = ", ".join(important_fields[:5])
        return f"{title}: {context}" if context else title

    def _split(self, content: str) -> Sequence[str]:
        separators = {".", "!", "?"}
        current: List[str] = []
        sentence = []
        for char in content:
            sentence.append(char)
            if char in separators:
                joined = "".join(sentence).strip()
                if joined:
                    current.append(joined)
                sentence = []
        trailing = "".join(sentence).strip()
        if trailing:
            current.append(trailing)
        return tuple(current)

    def _confidence(self, sentence: str) -> float:
        vector = self.encoder.encode(sentence)
        energy = sum(abs(value) for value in vector) / max(len(vector), 1)
        return max(0.2, min(0.95, 0.5 + energy))

    def _find_duplicate(self, graph: KnowledgeGraph, text: str) -> Optional[Node]:
        normalised = self._normalise(text)
        for node in graph.nodes():
            if node.type != "Claim":
                continue
            if self._normalise(node.text) == normalised:
                return node
        return None

    def _link_conflicts(self, graph: KnowledgeGraph, node: Node) -> Sequence[Tuple[str, str]]:
        conflicts: List[Tuple[str, str]] = []
        normalised = self._normalise(node.text, drop_negation=True)
        is_negative = self._is_negative(node.text)
        for existing in graph.nodes():
            if existing.id == node.id or existing.type != "Claim":
                continue
            existing_negative = self._is_negative(existing.text)
            if existing_negative == is_negative:
                continue
            if self._normalise(existing.text, drop_negation=True) == normalised:
                graph.add_edge(
                    Edge(
                        source=existing.id,
                        target=node.id,
                        relation="contradicts",
                        weight=0.5,
                    )
                )
                conflicts.append((existing.id, node.id))
        return conflicts

    def _normalise(self, text: str, drop_negation: bool = False) -> str:
        tokens = [token.lower() for token in re.findall(r"[\w']+", text)]
        filtered: List[str] = []
        for token in tokens:
            if drop_negation and token in {"not", "never", "no"}:
                continue
            if drop_negation and token in {"does", "do", "did"}:
                continue
            candidate = token[:-1] if drop_negation and token.endswith("s") and len(token) > 3 else token
            filtered.append(candidate)
        if drop_negation:
            return " ".join(sorted(filtered))
        return " ".join(filtered)

    def _is_negative(self, text: str) -> bool:
        tokens = {token.lower() for token in re.findall(r"[\w']+", text)}
        return bool(tokens & {"not", "never", "no"})


__all__ = [
    "IngestionReport",
    "KnowledgeDocument",
    "KnowledgeIngestor",
    "DomainImportPipeline",
    "DomainImportReport",
    "DomainRecord",
]
