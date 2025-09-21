"""SkillStore implementation with manifest validation and policy checks."""
from __future__ import annotations

from dataclasses import dataclass, field
import json
from pathlib import Path
from typing import Dict, Iterable, List, Mapping, Optional, Sequence, Set

from typing import Dict, Iterable, List, Mapping, Optional, Sequence



MANDATORY_FIELDS = {"name", "version", "inputs", "permissions", "billing", "policy", "entry"}

class SkillPolicyViolation(RuntimeError):
    """Raised when a skill manifest policy blocks execution."""

    def __init__(self, skill: str, policy: str, requirement: str) -> None:
        super().__init__(f"skill '{skill}' blocked by policy '{policy}' ({requirement})")
        self.skill = skill
        self.policy = policy
        self.requirement = requirement



@dataclass
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
            inputs=list(data.get("inputs", [])),
            permissions=list(data.get("permissions", [])),
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


class SkillStore:
    def __init__(self) -> None:
        self._skills: Dict[str, SkillManifest] = {}

    def register(self, manifest: SkillManifest) -> None:
        self._skills[manifest.name] = manifest

    def register_many(self, manifests: Iterable[SkillManifest]) -> None:
        for manifest in manifests:
            self.register(manifest)

    def get(self, name: str) -> Optional[SkillManifest]:
        return self._skills.get(name)

    def list(self) -> List[SkillManifest]:
        return sorted(self._skills.values(), key=lambda manifest: manifest.name)

    def require_permissions(self, name: str, permissions: Sequence[str]) -> bool:
        manifest = self.get(name)
        if not manifest:
            raise KeyError(f"unknown skill: {name}")
        return all(permission in manifest.permissions for permission in permissions)


    def enforce_policy(self, name: str, context_tags: Sequence[str]) -> None:
        manifest = self.get(name)
        if not manifest:
            raise KeyError(f"unknown skill: {name}")
        tags: Set[str] = set(context_tags)
        for policy, requirement in manifest.policy.items():
            rule = requirement.lower()
            if rule in {"deny", "blocked", "forbid"} and policy in tags:
                raise SkillPolicyViolation(name, policy, rule)
            if rule in {"require", "required"} and policy not in tags:
                raise SkillPolicyViolation(name, policy, rule)


    @staticmethod
    def load_from_file(path: str | Path) -> "SkillManifest":
        with open(path, "r", encoding="utf-8") as handle:
            return SkillManifest.from_dict(json.load(handle))


__all__ = ["SkillManifest", "SkillPolicyViolation", "SkillStore"]

__all__ = ["SkillManifest", "SkillStore"]
