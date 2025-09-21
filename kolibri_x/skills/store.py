"""Skill manifest registry with policy enforcement."""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Iterable, List, Mapping, Optional, Sequence, Set

MANDATORY_FIELDS = {"name", "version", "inputs", "permissions", "billing", "policy", "entry"}


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


@dataclass(frozen=True)
class SkillManifest:
    name: str
    version: str
    inputs: Sequence[str]
    permissions: Sequence[str]
    billing: str
    policy: Mapping[str, str]
    entry: str

    @classmethod
    def from_dict(cls, data: Mapping[str, object]) -> "SkillManifest":
        missing = MANDATORY_FIELDS - data.keys()
        if missing:
            raise ValueError(f"missing manifest fields: {sorted(missing)}")
        return cls(
            name=str(data["name"]),
            version=str(data["version"]),
            inputs=tuple(data.get("inputs", [])),
            permissions=tuple(data.get("permissions", [])),
            billing=str(data.get("billing", "per_call")),
            policy=dict(data.get("policy", {})),
            entry=str(data["entry"]),
        )

    def to_dict(self) -> Mapping[str, object]:
        return {
            "name": self.name,
            "version": self.version,
            "inputs": list(self.inputs),
            "permissions": list(self.permissions),
            "billing": self.billing,
            "policy": dict(self.policy),
            "entry": self.entry,
        }


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
    def __init__(self) -> None:
        self._skills: Dict[str, SkillManifest] = {}
        self._audit_log: List[SkillAuditRecord] = []

    def register(self, manifest: SkillManifest) -> None:
        self._skills[manifest.name] = manifest

    def register_many(self, manifests: Iterable[SkillManifest]) -> None:
        for manifest in manifests:
            self.register(manifest)

    def get(self, name: str) -> Optional[SkillManifest]:
        return self._skills.get(name)

    def list(self) -> List[SkillManifest]:
        return sorted(self._skills.values(), key=lambda manifest: manifest.name)

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


__all__ = ["SkillManifest", "SkillPolicyViolation", "SkillStore"]
