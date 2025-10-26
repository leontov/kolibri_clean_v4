"""Merkle-like action journal for Kolibri runtime events."""
from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
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

    @classmethod
    def from_dict(cls, data: Mapping[str, object]) -> "JournalEntry":
        timestamp_raw = data.get("timestamp")
        if not isinstance(timestamp_raw, str):
            raise ValueError("journal entry requires string timestamp")
        timestamp = datetime.fromisoformat(timestamp_raw)
        entry = cls(
            index=int(data.get("index", 0)),
            event=str(data.get("event", "")),
            payload=dict(data.get("payload", {})),
            timestamp=timestamp,
            prev_hash=str(data.get("prev_hash", "0" * 64)),
        )
        stored_hash = data.get("hash")
        if stored_hash is not None and str(stored_hash) != entry.hash:
            raise ValueError("journal entry hash mismatch during load")
        return entry


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

    def save(self, path: str | Path) -> None:
        destination = Path(path)
        if destination.parent:
            destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("w", encoding="utf-8") as stream:
            for entry in self._entries:
                json.dump(entry.to_dict(), stream, ensure_ascii=False, sort_keys=True)
                stream.write("\n")

    @classmethod
    def load(cls, path: str | Path) -> "ActionJournal":
        source = Path(path)
        journal = cls()
        if not source.exists():
            return journal
        with source.open("r", encoding="utf-8") as stream:
            for line in stream:
                if not line.strip():
                    continue
                payload = json.loads(line)
                entry = JournalEntry.from_dict(payload)
                journal._entries.append(entry)
        if not journal.verify():
            raise ValueError("loaded journal failed integrity verification")
        return journal


__all__ = ["ActionJournal", "JournalEntry"]
