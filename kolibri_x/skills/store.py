"""Skill manifest registry with policy enforcement."""
from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Iterable, List, Mapping, Optional, Sequence, Set

from kolibri_x.runtime.journal import ActionJournal

MANDATORY_FIELDS = {"name", "version", "inputs", "permissions", "billing", "policy", "entry"}

VERSION_PATTERN = re.compile(r"^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$")
PERMISSION_PATTERN = re.compile(r"^[a-z][a-z0-9_.-]*\.[a-z][a-z0-9_.-]*:[a-z0-9_.]+$")


def _to_int(value: object) -> Optional[int]:
    if value is None:
        return None
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, (int, float)):
        return int(value)
    if isinstance(value, str) and value.strip():
        try:
            return int(float(value))
        except ValueError:
            return None
    return None


class SkillPolicyViolation(RuntimeError):
    """Raised when a skill manifest policy blocks execution."""

    def __init__(
        self,
        skill: str,
        policy: str,
        requirement: str,
        *,
        details: Optional[Mapping[str, object]] = None,
    ) -> None:
        super().__init__(f"skill '{skill}' blocked by policy '{policy}' ({requirement})")
        self.skill = skill
        self.policy = policy
        self.requirement = requirement
        self.details = dict(details or {})


class SkillManifestValidationError(ValueError):
    """Raised when a manifest fails schema validation."""


@dataclass(frozen=True)
class SkillQuota:
    """Typed limits for sandboxed skill execution."""

    invocations: Optional[int] = None
    cpu_ms: Optional[int] = None
    wall_ms: Optional[int] = None
    ram_mb: Optional[int] = None
    net_bytes: Optional[int] = None
    fs_bytes: Optional[int] = None
    fs_ops: Optional[int] = None
    extra: Mapping[str, int] = field(default_factory=dict)

    @classmethod
    def from_dict(cls, payload: Mapping[str, object] | None) -> "SkillQuota":
        if not payload:
            return cls()
        known: Dict[str, Optional[int]] = {
            "invocations": _to_int(payload.get("invocations")),
            "cpu_ms": _to_int(payload.get("cpu_ms")),
            "wall_ms": _to_int(payload.get("wall_ms")),
            "ram_mb": _to_int(payload.get("ram_mb")),
            "net_bytes": _to_int(payload.get("net_bytes")),
            "fs_bytes": _to_int(payload.get("fs_bytes")),
            "fs_ops": _to_int(payload.get("fs_ops")),
        }
        extra: Dict[str, int] = {}
        for key, value in payload.items():
            if key in known:
                continue
            parsed = _to_int(value)
            if parsed is not None:
                extra[key] = parsed
        return cls(
            invocations=known["invocations"],
            cpu_ms=known["cpu_ms"],
            wall_ms=known["wall_ms"],
            ram_mb=known["ram_mb"],
            net_bytes=known["net_bytes"],
            fs_bytes=known["fs_bytes"],
            fs_ops=known["fs_ops"],
            extra=extra,
        )

    def to_dict(self) -> Mapping[str, int]:
        payload: Dict[str, int] = {}
        if self.invocations is not None:
            payload["invocations"] = int(self.invocations)
        if self.cpu_ms is not None:
            payload["cpu_ms"] = int(self.cpu_ms)
        if self.wall_ms is not None:
            payload["wall_ms"] = int(self.wall_ms)
        if self.ram_mb is not None:
            payload["ram_mb"] = int(self.ram_mb)
        if self.net_bytes is not None:
            payload["net_bytes"] = int(self.net_bytes)
        if self.fs_bytes is not None:
            payload["fs_bytes"] = int(self.fs_bytes)
        if self.fs_ops is not None:
            payload["fs_ops"] = int(self.fs_ops)
        payload.update(self.extra)
        return payload


class SkillQuotaExceeded(SkillPolicyViolation):
    """Raised when a skill exceeds or exhausts one of its quotas."""

    def __init__(self, skill: str, resource: str, limit: int, used: int) -> None:
        super().__init__(
            skill,
            "quota",
            f"{resource}_exceeded",
            details={"resource": resource, "limit": limit, "used": used},
        )
        self.resource = resource
        self.limit = limit
        self.used = used


@dataclass(frozen=True)
class SkillManifest:
    name: str
    version: str
    inputs: Sequence[str]
    permissions: Sequence[str]
    billing: str
    policy: Mapping[str, str]
    entry: str
    quota: SkillQuota = field(default_factory=SkillQuota)

    def validate(self) -> None:
        """Validate the manifest fields against schema rules."""

        self._validate_name(self.name)
        self._validate_version(self.version)
        self._validate_sequence(self.inputs, "inputs")
        self._validate_permissions(self.permissions)
        self._validate_entry(self.entry)
        self._validate_policy(self.policy)

    @classmethod
    def from_dict(cls, data: Mapping[str, object]) -> "SkillManifest":
        missing = MANDATORY_FIELDS - data.keys()
        if missing:

            raise SkillManifestValidationError(f"missing manifest fields: {sorted(missing)}")
        name = str(data["name"])
        version = str(data["version"])
        inputs = cls._coerce_sequence(data.get("inputs", []), "inputs")
        permissions = cls._coerce_sequence(data.get("permissions", []), "permissions")
        billing = str(data.get("billing", "per_call"))
        policy = cls._coerce_policy(data.get("policy", {}))
        entry = str(data["entry"])

        manifest = cls(
            name=name,
            version=version,
            inputs=tuple(inputs),
            permissions=tuple(permissions),
            billing=billing,
            policy=policy,
            entry=entry,

            raise ValueError(f"missing manifest fields: {sorted(missing)}")
        return cls(
            name=str(data["name"]),
            version=str(data["version"]),
            inputs=tuple(data.get("inputs", [])),
            permissions=tuple(data.get("permissions", [])),
            billing=str(data.get("billing", "per_call")),
            policy=dict(data.get("policy", {})),
            entry=str(data["entry"]),
            quota=SkillQuota.from_dict(
                data.get("limits") if isinstance(data.get("limits"), Mapping) else None
            ),

        )
        manifest.validate()
        return manifest

    def to_dict(self) -> Mapping[str, object]:
        return {
            "name": self.name,
            "version": self.version,
            "inputs": list(self.inputs),
            "permissions": list(self.permissions),
            "billing": self.billing,
            "policy": dict(self.policy),
            "entry": self.entry,
            "limits": dict(self.quota.to_dict()),
        }

    @staticmethod
    def _validate_name(name: str) -> None:
        if not name or not name.strip():
            raise SkillManifestValidationError("manifest name must be a non-empty string")

    @staticmethod
    def _validate_version(version: str) -> None:
        if not VERSION_PATTERN.match(version):
            raise SkillManifestValidationError(f"invalid manifest version '{version}'")

    @staticmethod
    def _validate_sequence(values: Sequence[object], field: str) -> None:
        for item in values:
            if not isinstance(item, str) or not item.strip():
                raise SkillManifestValidationError(f"{field} entries must be non-empty strings")

    @classmethod
    def _validate_permissions(cls, permissions: Sequence[str]) -> None:
        cls._validate_sequence(permissions, "permissions")
        for permission in permissions:
            if not PERMISSION_PATTERN.match(permission):
                raise SkillManifestValidationError(f"invalid permission format: '{permission}'")

    @staticmethod
    def _validate_entry(entry: str) -> None:
        candidate = Path(entry)
        if not entry or not entry.strip():
            raise SkillManifestValidationError("entry must be a non-empty string")
        if candidate.is_absolute():
            raise SkillManifestValidationError("entry must be a relative path")
        if any(part == ".." for part in candidate.parts):
            raise SkillManifestValidationError("entry cannot traverse directories")
        if candidate.suffix != ".py":
            raise SkillManifestValidationError("entry must reference a Python module (.py)")

    @staticmethod
    def _validate_policy(policy: Mapping[str, str]) -> None:
        for key, value in policy.items():
            if not isinstance(key, str) or not key.strip():
                raise SkillManifestValidationError("policy keys must be non-empty strings")
            if not isinstance(value, str) or not value.strip():
                raise SkillManifestValidationError("policy values must be non-empty strings")

    @staticmethod
    def _coerce_sequence(value: object, field: str) -> List[str]:
        if isinstance(value, Sequence) and not isinstance(value, (str, bytes)):
            items: List[str] = []
            for item in value:
                if not isinstance(item, str):
                    item = str(item)
                items.append(item)
            SkillManifest._validate_sequence(items, field)
            return items
        raise SkillManifestValidationError(f"{field} must be a sequence of strings")

    @staticmethod
    def _coerce_policy(value: object) -> Mapping[str, str]:
        if isinstance(value, Mapping):
            policy: Dict[str, str] = {}
            for key, val in value.items():
                if not isinstance(key, str):
                    key = str(key)
                if not isinstance(val, str):
                    val = str(val)
                policy[key] = val
            SkillManifest._validate_policy(policy)
            return policy
        raise SkillManifestValidationError("policy must be a mapping of string keys to string values")


@dataclass(frozen=True)
class SkillAuditRecord:
    timestamp: datetime
    skill: str
    actor: Optional[str]
    decision: str
    detail: Mapping[str, object] = field(default_factory=dict)

    def to_dict(self) -> Mapping[str, object]:
        payload = dict(self.detail)
        payload.update(
            {
                "skill": self.skill,
                "actor": self.actor,
                "decision": self.decision,
                "timestamp": self.timestamp.isoformat(),
            }
        )
        return payload


class SkillStore:
    def __init__(self, *, journal: Optional[ActionJournal] = None) -> None:
        self._skills: Dict[str, SkillManifest] = {}
        self._audit_log: List[SkillAuditRecord] = []

        self.journal = journal or ActionJournal()

        self._scopes: Dict[str, Sequence[str]] = {}
        self._quotas: Dict[str, SkillQuota] = {}


    def register(self, manifest: SkillManifest) -> None:
        try:
            manifest.validate()
        except SkillManifestValidationError as exc:
            self.journal.append(
                "skill_manifest.rejected",
                {
                    "skill": getattr(manifest, "name", "<unknown>"),
                    "reason": str(exc),
                },
            )
            raise
        self._skills[manifest.name] = manifest
        self._scopes[manifest.name] = tuple(manifest.permissions)
        self._quotas[manifest.name] = manifest.quota

    def register_many(self, manifests: Iterable[SkillManifest]) -> None:
        for manifest in manifests:
            self.register(manifest)

    def get(self, name: str) -> Optional[SkillManifest]:
        return self._skills.get(name)

    def list(self) -> List[SkillManifest]:
        return sorted(self._skills.values(), key=lambda manifest: manifest.name)

    def scopes(self, name: str) -> Sequence[str]:
        return self._scopes.get(name, tuple())

    def quota(self, name: str) -> SkillQuota:
        return self._quotas.get(name, SkillQuota())

    def authorize_execution(
        self,
        name: str,
        granted_scopes: Sequence[str],
        *,
        actor: Optional[str] = None,
    ) -> Sequence[str]:
        manifest = self.get(name)
        if not manifest:
            raise KeyError(f"unknown skill: {name}")
        required = set(manifest.permissions)
        granted = set(granted_scopes)
        missing = sorted(required - granted)
        if missing:
            self._record_audit(name, "deny", actor, {"missing_permissions": missing})
            raise SkillPolicyViolation(
                name,
                "permission",
                "scope_missing",
                details={"missing": missing},
            )
        self._record_audit(name, "allow", actor, {"granted": sorted(required)})
        return sorted(required)

    def enforce_policy(
        self,
        name: str,
        context_tags: Sequence[str],
        *,
        actor: Optional[str] = None,
    ) -> None:
        manifest = self.get(name)
        if not manifest:
            raise KeyError(f"unknown skill: {name}")
        tags: Set[str] = set(context_tags)
        for policy, requirement in manifest.policy.items():
            rule = requirement.lower()
            if rule in {"deny", "blocked", "forbid"} and policy in tags:
                self._record_audit(name, "deny", actor, {"policy": policy, "rule": rule})
                raise SkillPolicyViolation(name, policy, rule, details={"tag": policy})
            if rule in {"require", "required"} and policy not in tags:
                self._record_audit(name, "deny", actor, {"policy": policy, "rule": rule})
                raise SkillPolicyViolation(name, policy, rule, details={"required": policy})
        self._record_audit(name, "allow", actor, {"policies": dict(manifest.policy)})

    def audit_log(self) -> Sequence[Mapping[str, object]]:
        return tuple(record.to_dict() for record in self._audit_log)

    def _record_audit(
        self,
        skill: str,
        decision: str,
        actor: Optional[str],
        detail: Mapping[str, object],
    ) -> None:
        record = SkillAuditRecord(
            timestamp=datetime.now(timezone.utc),
            skill=skill,
            actor=actor,
            decision=decision,
            detail=dict(detail),
        )
        self._audit_log.append(record)
        if len(self._audit_log) > 512:
            self._audit_log.pop(0)

    @staticmethod
    def load_from_file(path: str | Path) -> SkillManifest:
        with open(path, "r", encoding="utf-8") as handle:
            return SkillManifest.from_dict(json.load(handle))


__all__ = ["SkillManifest", "SkillManifestValidationError", "SkillPolicyViolation", "SkillStore"]

__all__ = [
    "SkillManifest",
    "SkillPolicyViolation",
    "SkillQuota",
    "SkillQuotaExceeded",
    "SkillStore",
]

