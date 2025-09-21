"""Offline cache with deterministic eviction for the Kolibri-x MVP."""
from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Callable, Dict, Optional


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


__all__ = ["OfflineCache"]
