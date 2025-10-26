"""Knowledge graph implementation with hybrid memory and reasoning helpers."""
from __future__ import annotations

from collections import defaultdict
import json
import math
from pathlib import Path
import re
from dataclasses import dataclass, field, replace
from typing import (
    Any,
    Callable,
    Dict,
    Iterable,
    Iterator,
    List,
    Mapping,
    MutableMapping,
    Optional,
    Sequence,
    Tuple,
    Union,
)


@dataclass(frozen=True)
class Node:
    id: str
    type: str
    text: str
    sources: Sequence[str] = field(default_factory=tuple)
    confidence: float = 0.5
    embedding: Sequence[float] = field(default_factory=tuple)
    metadata: Mapping[str, object] = field(default_factory=dict)
    memory: str = "operational"


@dataclass(frozen=True)
class Edge:
    source: str
    target: str
    relation: str
    weight: float = 1.0
    memory: str = "operational"
    metadata: Mapping[str, object] = field(default_factory=dict)


@dataclass(frozen=True)
class VerificationResult:
    node_id: str
    critic: str
    score: float
    provenance: str = "critic"
    details: Mapping[str, object] = field(default_factory=dict)


class KnowledgeGraph:
    """Stores nodes, edges, and reasoning facilities for Kolibri-x."""

    def __init__(self, config: Optional[Mapping[str, object] | str | Path] = None) -> None:
        self._operational_nodes: MutableMapping[str, Node] = {}
        self._long_term_nodes: MutableMapping[str, Node] = {}
        self._operational_edges: List[Edge] = []
        self._long_term_edges: List[Edge] = []
        self._critics: Dict[str, Callable[[Node], float]] = {}
        self._authorities: Dict[str, Callable[[Node], object]] = {}
        self._pending_updates: Dict[str, Dict[str, object]] = {}

        self._revision: int = 0
        self._verification_cache: Dict[str, Tuple[int, Tuple[VerificationResult, ...]]] = {}

        config_source = config
        if config_source is None:
            default_path = Path("configs/kolibri.json")
            if default_path.exists():
                config_source = default_path
        if config_source is not None:
            self.load_critics_from_config(config_source)

        self._listeners: Dict[str, List[Callable[[Mapping[str, object]], None]]] = {}

    # ------------------------------------------------------------------
    # Persistence helpers
    # ------------------------------------------------------------------
    def save(self, path: Union[str, Path]) -> None:
        """Serialise the graph to a JSONL file."""

        destination = Path(path)
        destination.parent.mkdir(parents=True, exist_ok=True)

        with destination.open("w", encoding="utf-8") as handle:
            handle.write(json.dumps({"kind": "meta", "version": 1}, ensure_ascii=False) + "\n")
            for node in sorted(self.nodes(), key=lambda item: (item.memory, item.id)):
                payload = {"kind": "node", "data": self._serialise_node(node)}
                handle.write(json.dumps(payload, ensure_ascii=False, sort_keys=True) + "\n")
            for edge in sorted(
                self.edges(), key=lambda item: (item.memory, item.source, item.target, item.relation)
            ):
                payload = {"kind": "edge", "data": self._serialise_edge(edge)}
                handle.write(json.dumps(payload, ensure_ascii=False, sort_keys=True) + "\n")
            if self._pending_updates:
                pending_payload = {
                    node_id: self._normalise_json(changes)
                    for node_id, changes in sorted(self._pending_updates.items())
                }
                payload = {"kind": "pending", "data": pending_payload}
                handle.write(json.dumps(payload, ensure_ascii=False, sort_keys=True) + "\n")

    def load(self, path: Union[str, Path]) -> bool:
        """Load a graph snapshot from a JSONL file."""

        source = Path(path)
        if not source.exists():
            return False

        self._operational_nodes = {}
        self._long_term_nodes = {}
        self._operational_edges = []
        self._long_term_edges = []
        self._pending_updates = {}

        with source.open("r", encoding="utf-8") as handle:
            for line in handle:
                record = line.strip()
                if not record:
                    continue
                try:
                    payload = json.loads(record)
                except json.JSONDecodeError:
                    continue
                kind = payload.get("kind")
                data = payload.get("data", {})
                if kind == "node" and isinstance(data, Mapping):
                    node = self._node_from_payload(data)
                    self._node_store(node.memory)[node.id] = node
                elif kind == "edge" and isinstance(data, Mapping):
                    edge = self._edge_from_payload(data)
                    self._edge_store(edge.memory).append(edge)
                elif kind == "pending" and isinstance(data, Mapping):
                    pending: Dict[str, Dict[str, object]] = {}
                    for node_id, changes in data.items():
                        if isinstance(changes, Mapping):
                            pending[str(node_id)] = self._normalise_loaded_mapping(changes)
                    self._pending_updates.update(pending)
        return True


    # ------------------------------------------------------------------
    # Memory management
    # ------------------------------------------------------------------
    def add_node(self, node: Node, *, memory: Optional[str] = None) -> None:
        """Insert or replace a node in the configured memory tier."""

        tier = self._normalise_memory(memory or node.memory)
        stored = replace(node, memory=tier)
        event = "node_updated" if self.get_node(stored.id) is not None else "node_added"
        self._node_store(tier)[stored.id] = stored

        self._bump_revision()

        self._notify(event, {"node": stored})


    def promote_node(self, node_id: str) -> bool:
        """Move a node from operational memory into the long-term store."""

        node = self._operational_nodes.pop(node_id, None)
        if node is None:
            return False
        promoted = replace(node, memory="long_term")
        self._long_term_nodes[node_id] = promoted

        self._bump_revision()

        self._notify("node_updated", {"node": promoted})

        return True

    def add_edge(self, edge: Edge, *, memory: Optional[str] = None) -> None:
        tier = self._normalise_memory(memory or edge.memory)
        stored = replace(edge, memory=tier)
        self._edge_store(tier).append(stored)
        self._bump_revision()

    def nodes(self, level: Optional[str] = None) -> Sequence[Node]:
        tier = self._normalise_memory(level) if level else None
        if tier == "long_term":
            return tuple(self._long_term_nodes.values())
        if tier == "operational":
            return tuple(self._operational_nodes.values())
        return tuple(self._operational_nodes.values()) + tuple(self._long_term_nodes.values())

    def edges(self, level: Optional[str] = None) -> Sequence[Edge]:
        tier = self._normalise_memory(level) if level else None
        if tier == "long_term":
            return tuple(self._long_term_edges)
        if tier == "operational":
            return tuple(self._operational_edges)
        return tuple(self._operational_edges) + tuple(self._long_term_edges)

    def get_node(self, node_id: str) -> Optional[Node]:
        if node_id in self._operational_nodes:
            return self._operational_nodes[node_id]
        return self._long_term_nodes.get(node_id)

    def lazy_update(self, node_id: str, **changes: object) -> None:
        """Register a deferred update to the node and linked structures."""

        if self.get_node(node_id) is None:
            raise KeyError(f"unknown node: {node_id}")
        pending = self._pending_updates.setdefault(node_id, {})
        for key, value in changes.items():
            if key == "metadata" and isinstance(value, Mapping):
                metadata_changes = pending.setdefault("metadata", {})
                metadata_changes.update(dict(value))
            else:
                pending[key] = value

    def propagate_pending(self) -> Sequence[str]:
        """Apply deferred updates and back-propagate revision markers."""

        processed: List[str] = []
        pending = self._pending_updates
        self._pending_updates = {}
        for node_id, change in pending.items():
            node = self.get_node(node_id)
            if node is None:
                continue
            metadata_patch = dict(change.pop("metadata", {}))
            metadata = dict(node.metadata)
            if metadata_patch:
                metadata.setdefault("revisions", []).append(dict(metadata_patch))
                metadata.update(metadata_patch)
            valid_change: Dict[str, object] = {}
            ignored: List[str] = []
            for key, value in change.items():
                if hasattr(node, key):
                    valid_change[key] = value
                else:
                    ignored.append(key)
            if ignored:
                existing = set(metadata.get("ignored_updates", []))
                metadata["ignored_updates"] = sorted(existing | set(ignored))
            updated = replace(node, metadata=metadata, **valid_change)
            self._node_store(updated.memory)[node_id] = updated
            self._backpropagate(node_id)
            self._notify("node_updated", {"node": updated})
            processed.append(node_id)
        if processed:
            self._bump_revision()
        return tuple(processed)

    # ------------------------------------------------------------------
    # Verification
    # ------------------------------------------------------------------
    def register_critic(self, name: str, critic: Callable[[Node], float]) -> None:
        self._critics[name] = critic
        self._invalidate_verification_cache()

    def register_authority(self, name: str, authority: Callable[[Node], object]) -> None:
        self._authorities[name] = authority
        self._invalidate_verification_cache()

    def verify_with_critics(
        self,
        critics: Optional[Mapping[str, Callable[[Node], float]]] = None,
        *,
        authorities: Optional[Mapping[str, Callable[[Node], object]]] = None,
    ) -> List[VerificationResult]:
        """Run automated verification via critics and external authorities."""

        critic_pool: Dict[str, Callable[[Node], float]] = {}
        critic_pool.update(self._critics)
        if critics:
            critic_pool.update(dict(critics))

        authority_pool: Dict[str, Callable[[Node], object]] = {}
        authority_pool.update(self._authorities)
        if authorities:
            authority_pool.update(dict(authorities))

        use_cache = not critics and not authorities
        cache_key = "default"
        if use_cache:
            cached = self._verification_cache.get(cache_key)
            if cached and cached[0] == self._revision:
                return [result for result in cached[1]]

        results = self._execute_verification(critic_pool, authority_pool)

        if use_cache:
            self._verification_cache[cache_key] = (self._revision, tuple(results))

        self._record_verification(results)
        return list(results)

    def load_critics_from_config(self, config: Mapping[str, object] | str | Path) -> None:
        """Initialise critic registry using structured configuration."""

        data: Mapping[str, Any]
        if isinstance(config, (str, Path)):
            path = Path(config)
            try:
                text = path.read_text(encoding="utf-8")
            except OSError as exc:
                raise RuntimeError(f"failed to read critic config: {path}") from exc
            try:
                parsed = json.loads(text)
            except json.JSONDecodeError as exc:  # pragma: no cover - defensive
                raise ValueError(f"invalid critic configuration JSON: {path}") from exc
            data = parsed
        else:
            data = config

        kg_config = data.get("knowledge_graph") if isinstance(data, Mapping) else None
        critic_specs: Mapping[str, Any]
        if isinstance(kg_config, Mapping) and isinstance(kg_config.get("critics"), Mapping):
            critic_specs = kg_config["critics"]  # type: ignore[assignment]
        elif isinstance(data.get("critics"), Mapping):
            critic_specs = data["critics"]  # type: ignore[assignment]
        else:
            critic_specs = {}

        for name, spec in critic_specs.items():
            critic = self._build_configured_critic(name, spec)
            if critic is not None:
                self.register_critic(name, critic)

    # ------------------------------------------------------------------
    # Internal verification helpers
    # ------------------------------------------------------------------
    def _execute_verification(
        self,
        critics: Mapping[str, Callable[[Node], float]],
        authorities: Mapping[str, Callable[[Node], object]],
    ) -> List[VerificationResult]:
        results: List[VerificationResult] = []

        for name, critic in critics.items():
            for node in self.nodes():
                results.append(
                    VerificationResult(
                        node_id=node.id,
                        critic=name,
                        score=float(critic(node)),
                        provenance="critic",
                    )
                )

        for name, authority in authorities.items():
            for node in self.nodes():
                payload = authority(node)
                score, details = self._normalise_authority_payload(payload)
                results.append(
                    VerificationResult(
                        node_id=node.id,
                        critic=name,
                        score=score,
                        provenance="authority",
                        details=details,
                    )
                )

        return results

    def _build_configured_critic(self, name: str, spec: object) -> Optional[Callable[[Node], float]]:
        if callable(spec):
            return spec  # pragma: no cover - direct function injection

        if isinstance(spec, (int, float)):
            return self._confidence_threshold_critic(float(spec))

        if isinstance(spec, str):
            return self._keyword_presence_critic([spec])

        if isinstance(spec, Sequence) and not isinstance(spec, (bytes, bytearray, str)):
            return self._keyword_presence_critic([str(item) for item in spec])

        if isinstance(spec, Mapping):
            critic_type = str(spec.get("type", "confidence_threshold")).lower()
            if critic_type == "confidence_threshold":
                threshold = float(spec.get("threshold", 0.6))
                window = float(spec.get("window", 0.15))
                return self._confidence_threshold_critic(threshold, window=window)
            if critic_type == "keyword_presence":
                keywords = spec.get("keywords")
                if isinstance(keywords, Mapping):
                    keywords = keywords.values()
                if isinstance(keywords, Sequence) and not isinstance(keywords, (bytes, bytearray, str)):
                    return self._keyword_presence_critic([str(item) for item in keywords])
                if isinstance(spec.get("pattern"), str):
                    return self._keyword_presence_critic([str(spec["pattern"])])
            if critic_type == "regex":
                pattern = str(spec.get("pattern", name))
                return self._regex_critic(pattern)

        return None

    def _confidence_threshold_critic(self, threshold: float, *, window: float = 0.15) -> Callable[[Node], float]:
        upper = max(0.0, min(1.0, threshold))
        ramp = max(1e-6, float(window))

        def critic(node: Node) -> float:
            confidence = float(node.confidence)
            if confidence >= upper:
                return 1.0
            lower = max(0.0, upper - ramp)
            if confidence <= lower:
                return max(0.0, confidence / max(upper, 1e-6))
            span = max(upper - lower, 1e-6)
            return max(0.0, min(1.0, (confidence - lower) / span))

        return critic

    def _keyword_presence_critic(self, keywords: Sequence[str]) -> Callable[[Node], float]:
        terms = [keyword.lower() for keyword in keywords if keyword]
        if not terms:
            return lambda node: 1.0

        def critic(node: Node) -> float:
            text = node.text.lower()
            matches = sum(1 for term in terms if term in text)
            return matches / len(terms)

        return critic

    def _regex_critic(self, pattern: str) -> Callable[[Node], float]:
        compiled = re.compile(pattern, flags=re.IGNORECASE)

        def critic(node: Node) -> float:
            return 1.0 if compiled.search(node.text) else 0.0

        return critic

    # ------------------------------------------------------------------
    # Graph maintenance helpers
    # ------------------------------------------------------------------
    def deduplicate_embeddings(self, threshold: float = 0.995) -> List[Tuple[str, str]]:
        """Merge nodes with nearly identical embeddings to reduce noise."""

        seen: List[Node] = []
        duplicates: List[Tuple[str, str]] = []
        for node in self.nodes():
            vector = tuple(float(value) for value in node.embedding)
            if not vector:
                continue
            match = self._find_matching_node(seen, vector, threshold)
            if match is None:
                seen.append(node)
                continue
            canonical, duplicate = self._select_canonical(match, node)
            duplicates.append((canonical.id, duplicate.id))
            self._redirect_edges(duplicate.id, canonical.id)
            self._remove_node(duplicate.id)
            seen = [canonical if candidate.id == match.id else candidate for candidate in seen]
        return duplicates

    def compress_dialogue(self, utterances: Sequence[str], session_id: str) -> Mapping[str, object]:
        """Compress a dialogue into abstract events with causal links."""

        events: List[Dict[str, object]] = []
        keyword_counter: Dict[str, int] = defaultdict(int)
        for index, utterance in enumerate(utterances, start=1):
            actor, content = self._split_actor_content(utterance)
            keywords = self._extract_keywords(content)
            for keyword in keywords:
                keyword_counter[keyword] += 1
            event = {
                "id": f"{session_id}:{index:03d}",
                "session": session_id,
                "actors": [actor] if actor else [],
                "summary": self._summarise_text(content),
                "keywords": keywords,
                "importance": round(min(1.0, 0.25 + len(content.split()) / 40.0), 3),
            }
            events.append(event)
        causal_links = self._infer_causal_links(events)
        summary = self._compose_summary(events, keyword_counter)
        return {
            "session": session_id,
            "events": events,
            "summary": summary,
            "causal_links": causal_links,
        }

    def conflict_queries(self) -> List[Tuple[str, str]]:
        edges = [edge for edge in self.edges() if edge.relation in {"contradicts", "conflicts_with"}]
        return [(edge.source, edge.target) for edge in edges]

    def detect_conflicts(self) -> List[Tuple[str, str]]:
        """Detect contradictions via edges and semantic heuristics."""

        conflicts = {tuple(sorted(pair)) for pair in self.conflict_queries()}
        text_index: Dict[str, List[Node]] = defaultdict(list)
        for node in self.nodes():
            normalised = self._normalise_text(node.text, drop_negation=True)
            if normalised:
                text_index[normalised].append(node)
        for candidates in text_index.values():
            polarities = defaultdict(list)
            for candidate in candidates:
                polarities[self._polarity(candidate.text)].append(candidate.id)
            if len(polarities) > 1:
                negative = polarities.get("negative", [])
                positive = polarities.get("positive", [])
                for neg in negative:
                    for pos in positive:
                        conflicts.add(tuple(sorted((neg, pos))))
        return sorted(conflicts)

    def generate_clarification_requests(self) -> List[Mapping[str, object]]:
        """Build clarification prompts for detected knowledge conflicts."""

        requests: List[Mapping[str, object]] = []
        for left_id, right_id in self.detect_conflicts():
            left = self.get_node(left_id)
            right = self.get_node(right_id)
            if left is None or right is None:
                continue
            prompt = (
                f"Clarify whether '{left.text}' or '{right.text}' should be treated as authoritative."
            )
            requests.append(
                {
                    "pair": (left_id, right_id),
                    "prompt": prompt,
                    "sources": sorted(set(left.sources) | set(right.sources)),
                }
            )
        return requests

    # ------------------------------------------------------------------
    # Internal utilities
    # ------------------------------------------------------------------
    def _serialise_node(self, node: Node) -> Mapping[str, object]:
        return {
            "id": node.id,
            "type": node.type,
            "text": node.text,
            "sources": list(node.sources),
            "confidence": float(node.confidence),
            "embedding": [float(value) for value in node.embedding],
            "metadata": self._normalise_json(node.metadata),
            "memory": node.memory,
        }

    def _serialise_edge(self, edge: Edge) -> Mapping[str, object]:
        return {
            "source": edge.source,
            "target": edge.target,
            "relation": edge.relation,
            "weight": float(edge.weight),
            "memory": edge.memory,
            "metadata": self._normalise_json(edge.metadata),
        }

    def _node_from_payload(self, payload: Mapping[str, object]) -> Node:
        metadata_raw = payload.get("metadata", {})
        metadata = (
            self._normalise_loaded_mapping(metadata_raw)
            if isinstance(metadata_raw, Mapping)
            else {}
        )
        sources = payload.get("sources", [])
        embedding = payload.get("embedding", [])
        sources_iterable = isinstance(sources, Iterable) and not isinstance(sources, (str, bytes))
        embedding_iterable = isinstance(embedding, Iterable) and not isinstance(embedding, (str, bytes))
        memory_raw = payload.get("memory")
        memory = self._normalise_memory(memory_raw if isinstance(memory_raw, str) else None)
        return Node(
            id=str(payload.get("id")),
            type=str(payload.get("type", "")),
            text=str(payload.get("text", "")),
            sources=tuple(str(item) for item in sources) if sources_iterable else tuple(),
            confidence=float(payload.get("confidence", 0.5)),
            embedding=tuple(float(item) for item in embedding) if embedding_iterable else tuple(),
            metadata=metadata,
            memory=memory,
        )

    def _edge_from_payload(self, payload: Mapping[str, object]) -> Edge:
        metadata_raw = payload.get("metadata", {})
        metadata = (
            self._normalise_loaded_mapping(metadata_raw)
            if isinstance(metadata_raw, Mapping)
            else {}
        )
        memory_raw = payload.get("memory")
        memory = self._normalise_memory(memory_raw if isinstance(memory_raw, str) else None)
        return Edge(
            source=str(payload.get("source")),
            target=str(payload.get("target")),
            relation=str(payload.get("relation", "")),
            weight=float(payload.get("weight", 1.0)),
            memory=memory,
            metadata=metadata,
        )

    @staticmethod
    def _normalise_json(value: object) -> object:
        if isinstance(value, Mapping):
            return {str(key): KnowledgeGraph._normalise_json(val) for key, val in value.items()}
        if isinstance(value, (list, tuple)):
            return [KnowledgeGraph._normalise_json(item) for item in value]
        if isinstance(value, set):
            return sorted(KnowledgeGraph._normalise_json(item) for item in value)
        return value

    def _normalise_loaded_mapping(self, payload: Mapping[str, object]) -> Dict[str, object]:
        return {str(key): self._normalise_loaded_value(value) for key, value in payload.items()}

    def _normalise_loaded_value(self, value: object) -> object:
        if isinstance(value, Mapping):
            return {str(key): self._normalise_loaded_value(val) for key, val in value.items()}
        if isinstance(value, list):
            return [self._normalise_loaded_value(item) for item in value]
        return value

    def _normalise_memory(self, memory: Optional[str]) -> str:
        if not memory:
            return "operational"
        memory_lower = memory.lower()
        if memory_lower in {"long", "long_term", "archive"}:
            return "long_term"
        return "operational"

    def _node_store(self, tier: str) -> MutableMapping[str, Node]:
        return self._long_term_nodes if tier == "long_term" else self._operational_nodes

    def _edge_store(self, tier: str) -> List[Edge]:
        return self._long_term_edges if tier == "long_term" else self._operational_edges

    def _edge_iter(self) -> Iterator[List[Edge]]:
        yield self._operational_edges
        yield self._long_term_edges

    def _remove_node(self, node_id: str) -> None:
        removed = False
        if node_id in self._operational_nodes:
            del self._operational_nodes[node_id]
            removed = True
        elif node_id in self._long_term_nodes:
            del self._long_term_nodes[node_id]

            removed = True
        if removed:
            self._bump_revision()

        self._notify("node_removed", {"node_id": node_id})


    def _redirect_edges(self, old: str, new: str) -> None:
        changed = False
        for store in self._edge_iter():
            for index, edge in enumerate(list(store)):
                if edge.source == old or edge.target == old:
                    metadata = dict(edge.metadata)
                    metadata.setdefault("redirects", []).append({"from": old, "to": new})
                    updated = edge
                    if edge.source == old:
                        updated = replace(updated, source=new)
                    if edge.target == old:
                        updated = replace(updated, target=new)
                    updated = replace(updated, metadata=metadata)
                    store[index] = updated
                    changed = True
        if changed:
            self._bump_revision()

    def _find_matching_node(
        self, candidates: Sequence[Node], vector: Sequence[float], threshold: float
    ) -> Optional[Node]:
        for candidate in candidates:
            candidate_vector = tuple(float(value) for value in candidate.embedding)
            if not candidate_vector:
                continue
            if self._cosine_similarity(candidate_vector, vector) >= threshold:
                return candidate
        return None

    def _select_canonical(self, first: Node, second: Node) -> Tuple[Node, Node]:
        priority_first = (1 if first.memory == "long_term" else 0, first.confidence)
        priority_second = (1 if second.memory == "long_term" else 0, second.confidence)
        if priority_second > priority_first:
            return second, first
        return first, second

    @staticmethod
    def _cosine_similarity(left: Sequence[float], right: Sequence[float]) -> float:
        dot = sum(l * r for l, r in zip(left, right))
        left_norm = math.sqrt(sum(l * l for l in left))
        right_norm = math.sqrt(sum(r * r for r in right))
        if left_norm == 0 or right_norm == 0:
            return 0.0
        return dot / (left_norm * right_norm)

    def _split_actor_content(self, utterance: str) -> Tuple[str, str]:
        if ":" in utterance:
            actor, content = utterance.split(":", 1)
            return actor.strip(), content.strip()
        return "", utterance.strip()

    def _extract_keywords(self, content: str) -> List[str]:
        tokens = re.findall(r"[\w']+", content.lower())
        keywords: List[str] = []
        for token in tokens:
            if len(token) <= 3:
                continue
            if token in {"this", "that", "have", "with"}:
                continue
            if token not in keywords:
                keywords.append(token)
        return keywords[:8]

    def _summarise_text(self, content: str) -> str:
        words = content.split()
        summary = " ".join(words[:12])
        if len(words) > 12:
            summary += " ..."
        return summary or content

    def _infer_causal_links(self, events: Sequence[Mapping[str, object]]) -> List[Mapping[str, object]]:
        links: List[Mapping[str, object]] = []
        for index in range(1, len(events)):
            previous = events[index - 1]
            current = events[index]
            prev_keywords = set(previous.get("keywords", []))
            current_keywords = set(current.get("keywords", []))
            shared = sorted(prev_keywords & current_keywords)
            trigger_terms = {"because", "therefore", "so", "hence"}
            signal = any(term in str(current.get("summary", "")).lower() for term in trigger_terms)
            if shared or signal:
                links.append(
                    {
                        "cause": previous.get("id"),
                        "effect": current.get("id"),
                        "reason": "shared_topic" if shared else "temporal_cue",
                        "prediction": self._forecast_consequence(current, shared),
                    }
                )
        return links

    def _forecast_consequence(
        self, event: Mapping[str, object], shared_keywords: Sequence[str]
    ) -> str:
        if shared_keywords:
            keywords = ", ".join(shared_keywords)
            return f"Follow-up actions likely required around: {keywords}."
        return f"Monitor downstream impact of event {event.get('id')}"

    def _compose_summary(self, events: Sequence[Mapping[str, object]], keywords: Mapping[str, int]) -> str:
        if not events:
            return "dialogue empty"
        top_keywords = sorted(keywords.items(), key=lambda item: item[1], reverse=True)[:3]
        keyword_text = ", ".join(keyword for keyword, _ in top_keywords)
        return f"{len(events)} events captured. Key topics: {keyword_text}" if keyword_text else f"{len(events)} events captured."

    def _normalise_authority_payload(self, payload: object) -> Tuple[float, Mapping[str, object]]:
        if isinstance(payload, tuple) and len(payload) == 2:
            score = float(payload[0])
            details = payload[1]
            if isinstance(details, Mapping):
                return score, dict(details)
            return score, {"details": details}
        if isinstance(payload, Mapping):
            score = float(payload.get("score", 0.0))
            details = dict(payload)
            details.pop("score", None)
            return score, details
        return float(payload), {}

    def _record_verification(self, results: Iterable[VerificationResult]) -> None:
        scores: Dict[str, List[float]] = defaultdict(list)
        provenance: Dict[str, List[str]] = defaultdict(list)
        for result in results:
            scores[result.node_id].append(result.score)
            provenance[result.node_id].append(result.provenance)
        for node_id, values in scores.items():
            node = self.get_node(node_id)
            if node is None:
                continue
            metadata = dict(node.metadata)
            metadata["verification_score"] = sum(values) / len(values)
            metadata["verification_sources"] = provenance[node_id]
            updated = replace(node, metadata=metadata)
            self._node_store(updated.memory)[node_id] = updated
            self._notify("node_updated", {"node": updated})

    # ------------------------------------------------------------------
    # Event subscription helpers
    # ------------------------------------------------------------------
    def register_listener(
        self, event: str, listener: Callable[[Mapping[str, object]], None]
    ) -> None:
        listeners = self._listeners.setdefault(event, [])
        if listener not in listeners:
            listeners.append(listener)

    def unregister_listener(
        self, event: str, listener: Callable[[Mapping[str, object]], None]
    ) -> None:
        listeners = self._listeners.get(event)
        if not listeners:
            return
        if listener in listeners:
            listeners.remove(listener)
        if not listeners:
            self._listeners.pop(event, None)

    def _notify(self, event: str, payload: Mapping[str, object]) -> None:
        for listener in self._listeners.get(event, []):
            try:
                listener(payload)
            except Exception:
                continue

    def _backpropagate(self, node_id: str) -> None:
        changed = False
        for store in self._edge_iter():
            for index, edge in enumerate(list(store)):
                if edge.source != node_id and edge.target != node_id:
                    continue
                degraded = max(0.0, edge.weight * 0.95)
                metadata = dict(edge.metadata)
                metadata.setdefault("pending_review", True)
                store[index] = replace(edge, weight=degraded, metadata=metadata)
                neighbour_id = edge.target if edge.source == node_id else edge.source
                neighbour = self.get_node(neighbour_id)
                if neighbour is None:
                    continue
                metadata_n = dict(neighbour.metadata)
                pending = set(metadata_n.get("pending_backprop", []))
                pending.add(node_id)
                metadata_n["pending_backprop"] = sorted(pending)
                updated = replace(neighbour, metadata=metadata_n)
                self._node_store(updated.memory)[neighbour_id] = updated
                changed = True
        if changed:
            self._bump_revision()

    def _bump_revision(self) -> None:
        self._revision += 1
        self._invalidate_verification_cache()

    def _invalidate_verification_cache(self) -> None:
        self._verification_cache.clear()

    def _normalise_text(self, text: str, *, drop_negation: bool = False) -> str:
        tokens = [token.lower() for token in re.findall(r"[\w']+", text)]
        filtered: List[str] = []
        for token in tokens:
            if drop_negation and token in {"not", "never", "no"}:
                continue
            filtered.append(token)
        return " ".join(filtered)

    def _polarity(self, text: str) -> str:
        tokens = {token.lower() for token in re.findall(r"[\w']+", text)}
        return "negative" if tokens & {"not", "never", "no"} else "positive"


__all__ = [
    "Edge",
    "KnowledgeGraph",
    "Node",
    "VerificationResult",
]
