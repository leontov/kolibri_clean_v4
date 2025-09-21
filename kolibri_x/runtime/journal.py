"""Merkle-like action journal for Kolibri runtime events."""
from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
import hashlib
import json
from typing import List, Mapping, MutableMapping, Sequence


def _canonical_payload(payload: Mapping[str, object]) -> Mapping[str, object]:
    """Recursively convert payloads into JSON-friendly primitives."""

    def normalise(value: object) -> object:
        if isinstance(value, Mapping):
            return {str(key): normalise(val) for key, val in sorted(value.items())}
        if isinstance(value, (list, tuple, set)):
            return [normalise(item) for item in value]
        if isinstance(value, datetime):
            return value.isoformat()
        return value

    return normalise(payload)  # type: ignore[return-value]


@dataclass
class JournalEntry:
    """Single signed event inside the action journal."""

    index: int
    event: str
    payload: Mapping[str, object]
    timestamp: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    prev_hash: str = "0" * 64
    hash: str = field(init=False)

    def __post_init__(self) -> None:  # pragma: no cover - trivial wiring
        self.hash = self.compute_hash()

    def compute_hash(self) -> str:
        canonical: MutableMapping[str, object] = {
            "index": self.index,
            "event": self.event,
            "payload": _canonical_payload(self.payload),
            "timestamp": self.timestamp.isoformat(),
            "prev_hash": self.prev_hash,
        }
        digest = json.dumps(canonical, sort_keys=True, ensure_ascii=False)
        return hashlib.sha256(digest.encode("utf-8")).hexdigest()

    def to_dict(self) -> Mapping[str, object]:
        return {
            "index": self.index,
            "event": self.event,
            "payload": _canonical_payload(self.payload),
            "timestamp": self.timestamp.isoformat(),
            "prev_hash": self.prev_hash,
            "hash": self.hash,
        }


class ActionJournal:
    """Maintains a hash-chained log of runtime decisions."""

    def __init__(self) -> None:
        self._entries: List[JournalEntry] = []

    def append(self, event: str, payload: Mapping[str, object]) -> JournalEntry:
        prev_hash = self._entries[-1].hash if self._entries else "0" * 64
        entry = JournalEntry(index=len(self._entries), event=event, payload=payload, prev_hash=prev_hash)
        self._entries.append(entry)
        return entry

    def entries(self) -> Sequence[JournalEntry]:
        return tuple(self._entries)

    def tail(self, limit: int = 5) -> Sequence[JournalEntry]:
        if limit <= 0:
            return tuple()
        return tuple(self._entries[-limit:])

    def verify(self) -> bool:
        """Verifies the hash chain for tamper detection."""

        prev_hash = "0" * 64
        for entry in self._entries:
            if entry.prev_hash != prev_hash:
                return False
            if entry.compute_hash() != entry.hash:
                return False
            prev_hash = entry.hash
        return True

    def to_json(self) -> str:
        return json.dumps([entry.to_dict() for entry in self._entries], ensure_ascii=False, indent=2)


__all__ = ["ActionJournal", "JournalEntry"]
