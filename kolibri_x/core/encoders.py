"""Foundational and advanced multimodal encoders for the Kolibri-x MVP.

The original bootstrap implementation focused on determinism and
resource-awareness so we could exercise the runtime quickly.  This
module now evolves that baseline into a richer experimentation space by
bringing in adaptive cross-modal fusion, diffusion-inspired video
tokenisation, temporal alignment, continual learning and contextual
resolution control.  The components intentionally remain dependency-free
so they can execute in this constrained evaluation environment while
still modelling the behaviours described in the roadmap.
"""
from __future__ import annotations

from collections import Counter, deque
from dataclasses import dataclass, field
import hashlib
import math
import os
import re
from statistics import fmean
from typing import Dict, Iterable, Iterator, List, Mapping, MutableMapping, Optional, Sequence, Tuple


_WORD_REGEX = re.compile(r"[\w\-']+")


class TextEncoder:
    """A tiny hashed bag-of-words encoder.

    It intentionally avoids heavyweight ML dependencies but still
    produces dense, normalised vectors that we can feed into fusion
    layers or similarity modules.
    """

    def __init__(self, dim: int = 32) -> None:
        if dim <= 0:
            raise ValueError("dim must be positive")
        self.dim = dim

    def encode(self, text: str) -> List[float]:
        tokens = [token.lower() for token in _WORD_REGEX.findall(text)]
        counts = Counter(tokens)
        vector = [0.0] * self.dim
        for token, count in counts.items():
            digest = hashlib.blake2b(token.encode("utf-8"), digest_size=4).digest()
            index = int.from_bytes(digest, "big") % self.dim
            vector[index] += float(count)
        norm = math.sqrt(sum(value * value for value in vector)) or 1.0
        return [value / norm for value in vector]


class ASREncoder:
    """Deterministic placeholder for ASR integration.

    For the MVP the encoder simply normalises provided transcripts. In
    later iterations the method will stream audio frames through a
    conformer-based recogniser, but the interface is already in place.
    """

    def transcribe(self, audio: Sequence[float] | bytes | str) -> str:
        if isinstance(audio, str):
            return audio.strip()
        if isinstance(audio, bytes):
            try:
                return audio.decode("utf-8").strip()
            except UnicodeDecodeError:
                return hashlib.sha1(audio).hexdigest()
        if not audio:
            return ""
        rounded = (f"{sample:.3f}" for sample in audio)
        return " ".join(rounded)


class ImageEncoder:
    """Compact perceptual encoder that hashes pixel bytes.

    This is *not* a replacement for a vision transformer, but it
    provides a stable embedding so we can start exercising the fusion
    and retrieval code paths.
    """

    def __init__(self, dim: int = 32) -> None:
        if dim <= 0:
            raise ValueError("dim must be positive")
        self.dim = dim

    def _load(self, data: bytes | os.PathLike[str] | str) -> bytes:
        if isinstance(data, (str, os.PathLike)):
            with open(data, "rb") as handle:
                return handle.read()
        return data

    def encode(self, data: bytes | os.PathLike[str] | str) -> List[float]:
        payload = self._load(data)
        if not payload:
            return [0.0] * self.dim
        chunk = hashlib.sha256(payload).digest()
        vector = [0.0] * self.dim
        for index in range(self.dim):
            vector[index] = chunk[index % len(chunk)] / 255.0
        return vector


@dataclass
class FusionResult:
    embedding: List[float]
    modality_weights: Mapping[str, float]
    metadata: Mapping[str, object] = field(default_factory=dict)


@dataclass
class ModalitySignal:
    """Represents a modality stream entering the fusion transformer."""

    name: str
    embedding: Sequence[float]
    quality: float = 1.0
    latency_ms: float = 0.0
    resolution: str = "high"
    timestamp: float | None = None

    def energy(self) -> float:
        if not self.embedding:
            return 0.0
        return fmean(abs(float(value)) for value in self.embedding)


@dataclass
class ResolutionDecision:
    """Decision emitted by the resolution controller for a modality."""

    modality: str
    resolution: str
    expected_gain: float


class ResolutionController:
    """Chooses between low/high resolution based on predicted utility."""

    def __init__(self, high_cost: float = 1.0, low_cost: float = 0.2) -> None:
        self.high_cost = max(high_cost, 0.01)
        self.low_cost = max(low_cost, 0.01)

    def decide(self, modality: str, utility: float, budget: float) -> ResolutionDecision:
        """Returns a deterministic resolution choice honouring the budget."""

        high_score = utility / self.high_cost
        low_score = (utility * 0.6) / self.low_cost
        if budget <= 0:
            return ResolutionDecision(modality=modality, resolution="low", expected_gain=low_score)
        if high_score >= low_score:
            return ResolutionDecision(modality=modality, resolution="high", expected_gain=high_score)
        return ResolutionDecision(modality=modality, resolution="low", expected_gain=low_score)


class AdaptiveCrossModalTransformer:
    """Implements adaptive cross-attention with modality-specific depths.

    The transformer works with lightweight primitives: each modality
    signal is normalised and passed through a stack of pseudo-attention
    layers.  The number of layers per modality is chosen dynamically
    using heuristics based on quality, latency and signal energy.  The
    design mirrors the behaviour we expect from a production transformer
    without introducing external ML dependencies.
    """

    def __init__(
        self,
        dim: int = 64,
        min_depth: int = 1,
        max_depth: int = 6,
        resolution_controller: Optional[ResolutionController] = None,
    ) -> None:
        if min_depth <= 0 or max_depth < min_depth:
            raise ValueError("invalid depth bounds")
        self.dim = dim
        self.min_depth = min_depth
        self.max_depth = max_depth
        self.resolution_controller = resolution_controller or ResolutionController()
        self._history: deque[Mapping[str, object]] = deque(maxlen=100)

    def _depth_for_signal(self, signal: ModalitySignal) -> int:
        quality = max(signal.quality, 0.01)
        latency_penalty = 1.0 + signal.latency_ms / 1000.0
        energy = signal.energy() or 0.01
        score = quality * (1.0 + math.log1p(energy)) / latency_penalty
        scaled = self.min_depth + int(score * (self.max_depth - self.min_depth))
        return max(self.min_depth, min(self.max_depth, scaled))

    def _attention(self, query: Sequence[float], key: Sequence[float]) -> float:
        dot = sum(float(q) * float(k) for q, k in zip(query, key))
        denom = math.sqrt(len(query) or 1)
        return dot / denom

    def _normalise(self, values: Sequence[float]) -> List[float]:
        norm = math.sqrt(sum(float(v) ** 2 for v in values)) or 1.0
        return [float(v) / norm for v in values]

    def fuse(
        self,
        signals: Sequence[ModalitySignal],
        *,
        budget: float = 1.0,
        context: Optional[Mapping[str, object]] = None,
    ) -> FusionResult:
        if not signals:
            return FusionResult(embedding=[0.0] * self.dim, modality_weights={}, metadata={})
        modality_weights: MutableMapping[str, float] = {}
        accumulator = [0.0] * self.dim
        metadata: Dict[str, object] = {"layers": {}, "resolutions": {}, "history_len": len(self._history)}
        remaining_budget = float(budget)
        for signal in signals:
            utility = signal.quality * (1.0 + signal.energy())
            decision = self.resolution_controller.decide(signal.name, utility, remaining_budget)
            metadata["resolutions"][signal.name] = decision.resolution
            if decision.resolution == "high":
                remaining_budget -= 1.0
            else:
                remaining_budget -= 0.3
            depth = self._depth_for_signal(signal)
            metadata["layers"][signal.name] = depth
            vector = self._normalise(signal.embedding)[: self.dim]
            attention_weight = 0.0
            for layer in range(depth):
                rotated = vector[layer:] + vector[:layer]
                attention_weight += self._attention(vector, rotated)
                vector = [0.5 * (v + r) for v, r in zip(vector, rotated)]
            attention_weight = max(attention_weight, 0.01)
            modality_weights[signal.name] = attention_weight
            for index in range(min(self.dim, len(vector))):
                accumulator[index] += attention_weight * vector[index]
        total_weight = sum(modality_weights.values()) or 1.0
        embedding = [value / total_weight for value in accumulator]
        context_snapshot = dict(context or {})
        metadata["context"] = context_snapshot
        self._history.append({"signals": [signal.name for signal in signals], "metadata": metadata})
        return FusionResult(embedding=embedding, modality_weights=dict(modality_weights), metadata=metadata)

    def history(self) -> Sequence[Mapping[str, object]]:
        return tuple(self._history)


class FusionTransformer:
    """Simple cross-modal fusion through weighted averaging."""

    def __init__(self, dim: int = 32, modality_weights: Mapping[str, float] | None = None) -> None:
        self.dim = dim
        self.modality_weights = dict(modality_weights or {})

    def fuse(self, embeddings: Mapping[str, Sequence[float]]) -> FusionResult:
        accumulator = [0.0] * self.dim
        total_weight = 0.0
        applied_weights: MutableMapping[str, float] = {}
        for modality, values in embeddings.items():
            if not values:
                continue
            weight = float(self.modality_weights.get(modality, 1.0))
            applied_weights[modality] = weight
            total_weight += weight
            for index, value in enumerate(values):
                if index >= self.dim:
                    break
                accumulator[index] += weight * float(value)
        if total_weight:
            accumulator = [value / total_weight for value in accumulator]
        return FusionResult(embedding=accumulator, modality_weights=applied_weights, metadata={})


class DiffusionVisionEncoder:
    """Approximates diffusion tokenisation for streaming video inputs."""

    def __init__(self, dim: int = 64, frame_window: int = 8) -> None:
        if dim <= 0:
            raise ValueError("dim must be positive")
        if frame_window <= 0:
            raise ValueError("frame_window must be positive")
        self.dim = dim
        self.frame_window = frame_window

    def _hash_frame(self, frame: bytes) -> List[float]:
        digest = hashlib.blake2b(frame, digest_size=32).digest()
        return [digest[i] / 255.0 for i in range(min(self.dim, len(digest)))] + [0.0] * max(0, self.dim - len(digest))

    def tokenize_stream(self, frames: Iterable[bytes]) -> Iterator[List[float]]:
        """Yields compressed frame embeddings in an online fashion."""

        buffer: deque[List[float]] = deque(maxlen=self.frame_window)
        for frame in frames:
            token = self._hash_frame(frame)
            buffer.append(token)
            if len(buffer) == self.frame_window:
                yield self._aggregate_tokens(buffer)
        if buffer:
            yield self._aggregate_tokens(buffer)

    def encode_video(self, frames: Iterable[bytes]) -> List[float]:
        tokens = list(self.tokenize_stream(frames))
        if not tokens:
            return [0.0] * self.dim
        aggregate = [0.0] * self.dim
        for token in tokens:
            for index, value in enumerate(token):
                aggregate[index] += value
        norm = math.sqrt(sum(value * value for value in aggregate)) or 1.0
        return [value / norm for value in aggregate]

    def _aggregate_tokens(self, buffer: Iterable[List[float]]) -> List[float]:
        tokens = list(buffer)
        aggregate = [0.0] * self.dim
        for token in tokens:
            for index, value in enumerate(token):
                aggregate[index] += value
        length = max(len(tokens), 1)
        return [value / length for value in aggregate]


class AdaptiveAudioEncoder:
    """Audio encoder with noise calibration and voice profiles."""

    def __init__(self, dim: int = 48) -> None:
        self.dim = dim
        self._noise_floor: MutableMapping[str, float] = {}
        self._voiceprints: MutableMapping[str, List[float]] = {}

    def calibrate(self, user_id: str, samples: Sequence[float]) -> None:
        if not samples:
            self._noise_floor[user_id] = 0.0
            return
        magnitude = fmean(abs(sample) for sample in samples)
        self._noise_floor[user_id] = magnitude
        self._voiceprints[user_id] = self._extract_voiceprint(samples)

    def encode(self, samples: Sequence[float], user_id: Optional[str] = None) -> List[float]:
        if not samples:
            return [0.0] * self.dim
        noise = self._noise_floor.get(user_id or "global", 0.0)
        adjusted = [float(sample) - noise for sample in samples]
        voiceprint = self._voiceprints.get(user_id or "global")
        embedding = [0.0] * self.dim
        for index in range(self.dim):
            window = adjusted[index : index + 4]
            if not window:
                break
            feature = sum(abs(value) for value in window) / len(window)
            embedding[index] = feature
        if voiceprint:
            for index, value in enumerate(voiceprint[: self.dim]):
                embedding[index] = (embedding[index] + value) / 2.0
        norm = math.sqrt(sum(value * value for value in embedding)) or 1.0
        return [value / norm for value in embedding]

    def _extract_voiceprint(self, samples: Sequence[float]) -> List[float]:
        span = min(len(samples), self.dim)
        return [abs(float(samples[index])) for index in range(span)] + [0.0] * (self.dim - span)


@dataclass
class SensorEvent:
    """Normalised representation of IoT signals."""

    source: str
    signal_type: str
    value: float
    timestamp: float
    metadata: Mapping[str, object] = field(default_factory=dict)


class SensorHub:
    """Aggregates IoT device events into a unified timeline."""

    def __init__(self) -> None:
        self._events: List[SensorEvent] = []

    def ingest(self, event: SensorEvent) -> None:
        self._events.append(event)
        self._events.sort(key=lambda item: item.timestamp)

    def batch(self, start: float, end: float) -> List[SensorEvent]:
        return [event for event in self._events if start <= event.timestamp <= end]

    def to_sequences(self) -> Mapping[str, List[Tuple[float, float]]]:
        series: Dict[str, List[Tuple[float, float]]] = {}
        for event in self._events:
            key = f"{event.source}:{event.signal_type}"
            series.setdefault(key, []).append((event.timestamp, event.value))
        return series


class TemporalAlignmentEngine:
    """Synchronises modality timelines using lightweight heuristics."""

    def __init__(self, tolerance_ms: float = 120.0) -> None:
        self.tolerance_ms = tolerance_ms

    def align(self, traces: Mapping[str, Sequence[Tuple[float, float]]]) -> Mapping[str, Sequence[Tuple[float, float]]]:
        if not traces:
            return {}
        baseline = self._baseline(traces)
        aligned: Dict[str, List[Tuple[float, float]]] = {}
        for name, points in traces.items():
            offset = self._estimate_offset(points, baseline)
            aligned[name] = [(timestamp + offset, value) for timestamp, value in points]
        return aligned

    def _baseline(self, traces: Mapping[str, Sequence[Tuple[float, float]]]) -> Sequence[Tuple[float, float]]:
        # Choose the densest trace as baseline.
        return max(traces.values(), key=len)

    def _estimate_offset(self, points: Sequence[Tuple[float, float]], baseline: Sequence[Tuple[float, float]]) -> float:
        if not points or not baseline:
            return 0.0
        best_offset = 0.0
        best_score = float("inf")
        for candidate in range(-3, 4):
            offset = candidate * self.tolerance_ms / 2.0
            score = self._alignment_score(points, baseline, offset)
            if score < best_score:
                best_score = score
                best_offset = offset
        return best_offset

    def _alignment_score(
        self,
        points: Sequence[Tuple[float, float]],
        baseline: Sequence[Tuple[float, float]],
        offset: float,
    ) -> float:
        score = 0.0
        for (timestamp, value), (base_time, base_value) in zip(points, baseline):
            dt = abs((timestamp + offset) - base_time)
            dv = abs(value - base_value)
            score += dt + dv
        return score


class ModalityCompiler:
    """Selects optimal subnet configurations for constrained devices."""

    def __init__(self, catalogue: Optional[Mapping[str, Sequence[str]]] = None) -> None:
        self.catalogue = {**(catalogue or {})}

    def compile(self, budget: float, requested: Sequence[str]) -> Mapping[str, str]:
        plan: Dict[str, str] = {}
        for modality in requested:
            options = self.catalogue.get(modality, ("full", "quantized"))
            choice = options[0]
            if budget < 0.5 and len(options) > 1:
                choice = options[-1]
            plan[modality] = choice
            budget -= 0.4 if choice == options[0] else 0.2
        return plan


class ContinualLearner:
    """Toy continual learning loop with elastic consolidation."""

    def __init__(self, consolidation: float = 0.6) -> None:
        if not 0 <= consolidation <= 1:
            raise ValueError("consolidation must be in [0, 1]")
        self.consolidation = consolidation
        self._weights: Dict[str, float] = {}
        self._importance: Dict[str, float] = {}
        self._task_history: List[str] = []

    def train(self, task_id: str, gradients: Mapping[str, float]) -> Mapping[str, float]:
        updated: Dict[str, float] = {}
        for name, gradient in gradients.items():
            previous = self._weights.get(name, 0.0)
            importance = self._importance.get(name, 0.0)
            correction = self.consolidation * importance * (previous - gradient)
            new_value = previous * (1 - self.consolidation) + gradient - correction
            self._weights[name] = new_value
            self._importance[name] = min(1.0, importance + abs(gradient))
            updated[name] = new_value
        if task_id not in self._task_history:
            self._task_history.append(task_id)
        return updated

    def snapshot(self) -> Mapping[str, object]:
        return {"weights": dict(self._weights), "importance": dict(self._importance), "tasks": list(self._task_history)}


__all__ = [
    "ASREncoder",
    "AdaptiveAudioEncoder",
    "AdaptiveCrossModalTransformer",
    "ContinualLearner",
    "DiffusionVisionEncoder",
    "FusionResult",
    "FusionTransformer",
    "ImageEncoder",
    "ModalityCompiler",
    "ModalitySignal",
    "ResolutionController",
    "ResolutionDecision",
    "SensorEvent",
    "SensorHub",
    "TemporalAlignmentEngine",
    "TextEncoder",
]


