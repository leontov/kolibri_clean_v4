"""Runtime caching primitives."""
from __future__ import annotations

import hashlib
import json
from copy import deepcopy
from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Callable, Dict, Mapping, Optional, Sequence


TimeProvider = Callable[[], datetime]


@dataclass
class CacheEntry:
    value: object
    timestamp: datetime


class OfflineCache:
    def __init__(self, ttl: timedelta = timedelta(hours=1), time_provider: Optional[TimeProvider] = None) -> None:
        self.ttl = ttl
        self._time_provider = time_provider or datetime.utcnow
        self._entries: Dict[str, CacheEntry] = {}

    def put(self, key: str, value: object) -> None:
        self._entries[key] = CacheEntry(value=value, timestamp=self._time_provider())

    def get(self, key: str) -> Optional[object]:
        self.prune()
        entry = self._entries.get(key)
        return entry.value if entry else None

    def prune(self) -> None:
        now = self._time_provider()
        expired = [key for key, entry in self._entries.items() if now - entry.timestamp > self.ttl]
        for key in expired:
            self._entries.pop(key, None)

    def size(self) -> int:
        self.prune()
        return len(self._entries)


class RAGCache:
    """Caches retrieval-augmented answers per user and context."""

    def __init__(
        self,
        ttl: timedelta = timedelta(minutes=30),
        *,
        time_provider: Optional[TimeProvider] = None,
    ) -> None:
        self._cache = OfflineCache(ttl=ttl, time_provider=time_provider)

    def _key(
        self,
        user_id: str,
        query: str,
        tags: Sequence[str],
        modalities: Sequence[str],
        top_k: int,
    ) -> str:
        payload = {
            "user": user_id,
            "query": query,
            "tags": sorted(set(tags)),
            "modalities": sorted(set(modalities)),
            "top_k": int(top_k),
        }
        raw = json.dumps(payload, sort_keys=True, ensure_ascii=False)
        return hashlib.sha256(raw.encode("utf-8")).hexdigest()

    def get(
        self,
        user_id: str,
        query: str,
        tags: Sequence[str],
        modalities: Sequence[str],
        top_k: int,
    ) -> Optional[Mapping[str, object]]:
        key = self._key(user_id, query, tags, modalities, top_k)
        cached = self._cache.get(key)
        return deepcopy(cached) if cached is not None else None

    def put(
        self,
        user_id: str,
        query: str,
        tags: Sequence[str],
        modalities: Sequence[str],
        top_k: int,
        answer: Mapping[str, object],
    ) -> None:
        key = self._key(user_id, query, tags, modalities, top_k)
        self._cache.put(key, deepcopy(answer))


__all__ = ["OfflineCache", "RAGCache"]
