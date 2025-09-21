"""Privacy operator handling consents and data policies."""
from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Dict, Iterable, Mapping, MutableMapping, Optional, Sequence, Set


@dataclass
class ConsentRecord:
    user_id: str
    allowed: Set[str] = field(default_factory=set)
    denied: Set[str] = field(default_factory=set)
    updated_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))

    def to_dict(self) -> Mapping[str, object]:
        return {
            "user_id": self.user_id,
            "allowed": sorted(self.allowed),
            "denied": sorted(self.denied),
            "updated_at": self.updated_at.isoformat(),
        }


class PrivacyOperator:
    def __init__(self) -> None:
        self._records: Dict[str, ConsentRecord] = {}

    def grant(self, user_id: str, data_types: Iterable[str]) -> ConsentRecord:
        record = self._records.setdefault(user_id, ConsentRecord(user_id=user_id))
        for item in data_types:
            record.allowed.add(item)
            record.denied.discard(item)
        record.updated_at = datetime.now(timezone.utc)
        return record

    def deny(self, user_id: str, data_types: Iterable[str]) -> ConsentRecord:
        record = self._records.setdefault(user_id, ConsentRecord(user_id=user_id))
        for item in data_types:
            record.denied.add(item)
            record.allowed.discard(item)
        record.updated_at = datetime.now(timezone.utc)
        return record

    def is_allowed(self, user_id: str, data_type: str) -> bool:
        record = self._records.get(user_id)
        if not record:
            return False
        if data_type in record.denied:
            return False
        return data_type in record.allowed

    def enforce(self, user_id: str, requested: Sequence[str]) -> Sequence[str]:
        return [data_type for data_type in requested if self.is_allowed(user_id, data_type)]

    def export_state(self) -> Mapping[str, Mapping[str, object]]:
        return {user_id: record.to_dict() for user_id, record in self._records.items()}


__all__ = ["ConsentRecord", "PrivacyOperator"]
