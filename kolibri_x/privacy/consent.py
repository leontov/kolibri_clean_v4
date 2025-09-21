"""Privacy consent management for Kolibri."""
from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Dict, Iterable, List, Mapping, MutableMapping, Optional, Sequence, Set


@dataclass
class ConsentRecord:
    user_id: str
    allowed: Set[str] = field(default_factory=set)
    denied: Set[str] = field(default_factory=set)
    proofs: MutableMapping[str, str] = field(default_factory=dict)
    updated_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))

    def to_dict(self) -> Mapping[str, object]:
        return {
            "user_id": self.user_id,
            "allowed": sorted(self.allowed),
            "denied": sorted(self.denied),
            "proofs": dict(self.proofs),
            "updated_at": self.updated_at.isoformat(),
        }


@dataclass(frozen=True)
class PolicyLayer:
    name: str
    scope: Set[str]
    default_action: str = "deny"


@dataclass(frozen=True)
class AccessProof:
    user_id: str
    data_type: str
    policy_layer: str
    proof_hash: str
    issued_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))


@dataclass(frozen=True)
class SecurityIncident:
    timestamp: datetime
    skill: str
    detail: str


class PrivacyOperator:
    """Tracks consent, policy layers, and issued proofs."""

    def __init__(self) -> None:
        self._records: Dict[str, ConsentRecord] = {}
        self._layers: Dict[str, PolicyLayer] = {}
        self._audit_log: List[SecurityIncident] = []

    def register_layer(self, layer: PolicyLayer) -> None:
        self._layers[layer.name] = layer

    def grant(self, user_id: str, data_types: Iterable[str]) -> ConsentRecord:
        record = self._records.setdefault(user_id, ConsentRecord(user_id=user_id))
        for data_type in data_types:
            record.allowed.add(data_type)
            record.denied.discard(data_type)
            record.proofs[data_type] = self._zk_proof(user_id, data_type, "allow")
        record.updated_at = datetime.now(timezone.utc)
        return record

    def deny(self, user_id: str, data_types: Iterable[str]) -> ConsentRecord:
        record = self._records.setdefault(user_id, ConsentRecord(user_id=user_id))
        for data_type in data_types:
            record.denied.add(data_type)
            record.allowed.discard(data_type)
            record.proofs[data_type] = self._zk_proof(user_id, data_type, "deny")
        record.updated_at = datetime.now(timezone.utc)
        return record

    def is_allowed(self, user_id: str, data_type: str) -> bool:
        record = self._records.get(user_id)
        if not record:
            return False
        if data_type in record.denied:
            return False
        if data_type in record.allowed:
            return True
        layer = self._layer_for(data_type)
        return layer.default_action == "allow" if layer else False

    def enforce(self, user_id: str, requested: Sequence[str]) -> Sequence[str]:
        return [data_type for data_type in requested if self.is_allowed(user_id, data_type)]

    def record_access(self, skill: str, user_id: str, data_types: Sequence[str]) -> List[AccessProof]:
        proofs: List[AccessProof] = []
        for data_type in data_types:
            if not self.is_allowed(user_id, data_type):
                self._audit_log.append(
                    SecurityIncident(
                        timestamp=datetime.now(timezone.utc),
                        skill=skill,
                        detail=f"access denied for {data_type}",
                    )
                )
                continue
            layer = self._layer_for(data_type)
            proof_hash = self._zk_proof(user_id, data_type, layer.name if layer else "direct")
            proofs.append(
                AccessProof(
                    user_id=user_id,
                    data_type=data_type,
                    policy_layer=layer.name if layer else "direct",
                    proof_hash=proof_hash,
                )
            )
        return proofs

    def register_incident(self, skill: str, detail: str) -> None:
        self._audit_log.append(
            SecurityIncident(timestamp=datetime.now(timezone.utc), skill=skill, detail=detail)
        )

    def audit_log(self) -> Sequence[SecurityIncident]:
        return tuple(self._audit_log)

    def export_state(self) -> Mapping[str, Mapping[str, object]]:
        return {user_id: record.to_dict() for user_id, record in self._records.items()}

    def _layer_for(self, data_type: str) -> Optional[PolicyLayer]:
        for layer in self._layers.values():
            if data_type in layer.scope:
                return layer
        return None

    def _zk_proof(self, user_id: str, data_type: str, action: str) -> str:
        payload = f"{user_id}:{data_type}:{action}".encode("utf-8")
        return hex(abs(hash(payload)))[2:]


__all__ = [
    "AccessProof",
    "ConsentRecord",
    "PolicyLayer",
    "PrivacyOperator",
    "SecurityIncident",
]
