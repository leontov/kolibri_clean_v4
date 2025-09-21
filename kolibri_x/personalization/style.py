"""Self-adapting writing style learner for authentic text generation."""
from __future__ import annotations

import re
from collections import Counter, defaultdict
from statistics import fmean
from typing import Mapping, MutableMapping

SENTENCE_SPLIT = re.compile(r"[.!?]+\s+")
TOKEN_SPLIT = re.compile(r"[^a-zA-Zа-яА-Я0-9]+", re.UNICODE)


class WritingStyleLearner:
    """Learns lightweight statistical fingerprints of the user's writing."""

    def __init__(self, *, ngram_size: int = 2) -> None:
        if ngram_size < 1:
            raise ValueError("ngram_size must be positive")
        self.ngram_size = ngram_size
        self._sentence_lengths: MutableMapping[str, list[int]] = defaultdict(list)
        self._token_histogram: MutableMapping[str, Counter[str]] = defaultdict(Counter)
        self._ngram_histogram: MutableMapping[str, Counter[tuple[str, ...]]] = defaultdict(
            Counter
        )

    def ingest(self, user_id: str, text: str) -> None:
        tokens = [token for token in TOKEN_SPLIT.split(text.lower()) if token]
        if not tokens:
            return
        self._token_histogram[user_id].update(tokens)
        ngrams = [tuple(tokens[i : i + self.ngram_size]) for i in range(len(tokens) - self.ngram_size + 1)]
        if ngrams:
            self._ngram_histogram[user_id].update(ngrams)
        sentences = [segment.strip() for segment in SENTENCE_SPLIT.split(text) if segment.strip()]
        if sentences:
            for sentence in sentences:
                words = [token for token in TOKEN_SPLIT.split(sentence) if token]
                if words:
                    self._sentence_lengths[user_id].append(len(words))

    def signature(self, user_id: str) -> Mapping[str, object]:
        tokens = self._token_histogram.get(user_id)
        if not tokens:
            return {
                "average_sentence_length": 0.0,
                "lexical_diversity": 0.0,
                "top_phrases": [],
            }
        lengths = self._sentence_lengths.get(user_id, [len(tokens)])
        diversity = len(tokens) / max(sum(tokens.values()), 1)
        top_phrases = [" ".join(ngram) for ngram, _ in self._ngram_histogram[user_id].most_common(3)]
        return {
            "average_sentence_length": fmean(lengths),
            "lexical_diversity": diversity,
            "top_phrases": top_phrases,
        }

    def generate_guidelines(
        self, user_id: str, *, include_examples: bool = True
    ) -> Mapping[str, object]:
        signature = self.signature(user_id)
        guidelines: MutableMapping[str, object] = {
            "target_sentence_length": signature.get("average_sentence_length", 0.0),
            "lexical_diversity": signature.get("lexical_diversity", 0.0),
            "mirroring_level": min(1.0, signature.get("lexical_diversity", 0.0) * 1.5),
        }
        if include_examples:
            guidelines["preferred_phrases"] = signature.get("top_phrases", [])
        return guidelines


__all__ = ["WritingStyleLearner"]
