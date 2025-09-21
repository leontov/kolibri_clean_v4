"""Lightweight multimodal encoders used by the Kolibri runtime."""
from __future__ import annotations

import hashlib
import math
import os
from collections import Counter, deque
from dataclasses import dataclass, field
from statistics import fmean
from typing import Deque, Dict, Iterable, List, Mapping, MutableMapping, Optional, Sequence, Tuple

_WORD_BYTES = 4


class TextEncoder:
    """Simple hashed bag-of-words encoder."""

    def __init__(self, dim: int = 32) -> None:
        if dim <= 0:
            raise ValueError("dim must be positive")
        self.dim = dim

    def encode(self, text: str) -> List[float]:
        tokens = [token.lower() for token in text.split() if token]
        counts = Counter(tokens)
        vector = [0.0] * self.dim
        for token, count in counts.items():
            digest = hashlib.blake2b(token.encode("utf-8"), digest_size=_WORD_BYTES).digest()
            index = int.from_bytes(digest, "big") % self.dim
            vector[index] += float(count)
        norm = math.sqrt(sum(value * value for value in vector)) or 1.0
        return [value / norm for value in vector]


class ASREncoder:
    """Deterministic placeholder for speech recognition."""

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
        return " ".join(f"{sample:.3f}" for sample in audio)


class ImageEncoder:
    """Hashes raw bytes to produce a pseudo-embedding."""

    def __init__(self, dim: int = 32) -> None:
        if dim <= 0:
            raise ValueError("dim must be positive")
        self.dim = dim

    def _load(self, payload: bytes | os.PathLike[str] | str) -> bytes:
        if isinstance(payload, (str, os.PathLike)):
            with open(payload, "rb") as handle:
                return handle.read()
        return payload

    def encode(self, data: bytes | os.PathLike[str] | str) -> List[float]:
        raw = self._load(data)
        if not raw:
            return [0.0] * self.dim
        digest = hashlib.sha256(raw).digest()
        return [digest[index % len(digest)] / 255.0 for index in range(self.dim)]


@dataclass
class FusionResult:
    embedding: List[float]
    modality_weights: Mapping[str, float]
    metadata: Mapping[str, object] = field(default_factory=dict)


@dataclass
class ModalitySignal:
    name: str
    embedding: Sequence[float]
    quality: float = 1.0
    latency_ms: float = 0.0
    metadata: Mapping[str, object] = field(default_factory=dict)

    def energy(self) -> float:
        if not self.embedding:
            return 0.0
        return fmean(abs(float(value)) for value in self.embedding)


class FusionTransformer:
    """Fuses modality embeddings by weighted averaging."""

    def __init__(self, dim: int = 32) -> None:
        self.dim = dim

    def fuse(self, embeddings: Mapping[str, Sequence[float]]) -> FusionResult:
        if not embeddings:
            return FusionResult(embedding=[0.0] * self.dim, modality_weights={})
        weights = {name: 1.0 for name in embeddings}
        weight_sum = sum(weights.values())
        normalised = {name: weight / weight_sum for name, weight in weights.items()}
        fused = [0.0] * self.dim
        for name, vector in embeddings.items():
            for index in range(min(len(vector), self.dim)):
                fused[index] += normalised[name] * float(vector[index])
        return FusionResult(embedding=fused, modality_weights=normalised)


class AdaptiveCrossModalTransformer:
    """Selects modality weights based on quality and latency."""

    def __init__(self, dim: int = 32) -> None:
        self.dim = dim

    def fuse(self, signals: Sequence[ModalitySignal], budget: float = 1.0) -> FusionResult:
        if not signals:
            return FusionResult(embedding=[0.0] * self.dim, modality_weights={})
        weights: Dict[str, float] = {}
        for signal in signals:
            latency_penalty = 1.0 + signal.latency_ms / max(budget, 0.1)
            weights[signal.name] = max(signal.quality, 0.0) / latency_penalty
        total = sum(weights.values()) or 1.0
        normalised = {name: weight / total for name, weight in weights.items()}
        fused = [0.0] * self.dim
        for signal in signals:
            for index in range(min(len(signal.embedding), self.dim)):
                fused[index] += normalised[signal.name] * float(signal.embedding[index])
        metadata = {
            "layers": {name: max(1, int(8 * weight)) for name, weight in normalised.items()},
        }
        return FusionResult(embedding=fused, modality_weights=normalised, metadata=metadata)


class AdaptiveAudioEncoder:
    """Maintains per-user calibration baselines for audio features."""

    def __init__(self, dim: int = 16) -> None:
        self.dim = dim
        self._profiles: Dict[str, List[float]] = {}

    def calibrate(self, user_id: str, samples: Sequence[float]) -> None:
        if not samples:
            self._profiles[user_id] = [0.0] * self.dim
            return
        baseline = sum(float(value) for value in samples) / len(samples)
        self._profiles[user_id] = [baseline] * self.dim

    def encode(self, samples: Sequence[float], *, user_id: str = "default") -> List[float]:
        profile = self._profiles.get(user_id, [0.0] * self.dim)
        buffer = [0.0] * self.dim
        for index, value in enumerate(samples):
            buffer[index % self.dim] += float(value) - profile[index % len(profile)]
        norm = math.sqrt(sum(entry * entry for entry in buffer)) or 1.0
        return [entry / norm for entry in buffer]


class DiffusionVisionEncoder:
    """Aggregates frame hashes with a sliding window."""

    def __init__(self, dim: int = 32, frame_window: int = 4) -> None:
        self.dim = dim
        self.frame_window = max(1, frame_window)

    def encode_video(self, frames: Iterable[bytes]) -> List[float]:
        window: Deque[bytes] = deque(maxlen=self.frame_window)
        for frame in frames:
            window.append(frame)
        if not window:
            return [0.0] * self.dim
        digest = hashlib.sha256(b"".join(window)).digest()
        return [digest[index % len(digest)] / 255.0 for index in range(self.dim)]


@dataclass
class SensorEvent:
    source: str
    signal_type: str
    value: float
    timestamp: float


class SensorHub:
    """Collects sensor events and exposes them as time series."""

    def __init__(self) -> None:
        self._streams: Dict[str, List[SensorEvent]] = {}

    def ingest(self, event: SensorEvent) -> None:
        self._streams.setdefault(event.signal_type, []).append(event)
        self._streams[event.signal_type].sort(key=lambda item: item.timestamp)

    def to_sequences(self) -> Mapping[str, List[Tuple[float, float]]]:
        return {
            signal_type: [(event.timestamp, event.value) for event in events]
            for signal_type, events in self._streams.items()
        }


class TemporalAlignmentEngine:
    """Aligns heterogeneous sensor streams using relative offsets."""

    def align(
        self, sequences: Mapping[str, Sequence[Tuple[float, float]]]
    ) -> Mapping[str, List[Tuple[float, float]]]:
        if not sequences:
            return {}
        earliest = min((points[0][0] for points in sequences.values() if points), default=0.0)
        aligned: Dict[str, List[Tuple[float, float]]] = {}
        for signal_type, points in sequences.items():
            aligned[signal_type] = [(timestamp - earliest, value) for timestamp, value in points]
        return aligned


class ContinualLearner:
    """Tracks task-specific weight updates with consolidation."""

    def __init__(self, consolidation: float = 0.5) -> None:
        self.consolidation = consolidation
        self._weights: Dict[str, Dict[str, float]] = {}

    def train(self, task_id: str, gradients: Mapping[str, float]) -> Mapping[str, float]:
        task_state = self._weights.setdefault(task_id, {})
        updated: Dict[str, float] = {}
        for name, gradient in gradients.items():
            previous = task_state.get(name, 0.0)
            new_value = (1.0 - self.consolidation) * previous + self.consolidation * float(gradient)
            task_state[name] = new_value
            updated[name] = new_value
        return updated

    def snapshot(self) -> Mapping[str, object]:
        return {
            "tasks": sorted(self._weights),
            "weights": {task: dict(weights) for task, weights in self._weights.items()},
        }


__all__ = [
    "ASREncoder",
    "AdaptiveAudioEncoder",
    "AdaptiveCrossModalTransformer",
    "ContinualLearner",
    "DiffusionVisionEncoder",
    "FusionResult",
    "FusionTransformer",
    "ImageEncoder",
    "ModalitySignal",
    "SensorEvent",
    "SensorHub",
    "TemporalAlignmentEngine",
    "TextEncoder",
]
