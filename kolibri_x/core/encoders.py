"""Foundational multimodal encoders for the Kolibri-x MVP.

These lightweight implementations prioritise determinism and
resource-awareness so they can run on-device while we iterate on the
full transformer stack described in the architecture blueprint.
"""
from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
import hashlib
import math
import os
import re
from typing import Dict, Iterable, List, Mapping, MutableMapping, Sequence


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
        return FusionResult(embedding=accumulator, modality_weights=applied_weights)


__all__ = [
    "ASREncoder",
    "FusionResult",
    "FusionTransformer",
    "ImageEncoder",
    "TextEncoder",
]
