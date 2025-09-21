"""Family mode orchestration with segmented access and emotions."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Mapping, MutableMapping, Sequence, Set

from .profile import EmotionalSnapshot


@dataclass
class FamilyMemberProfile:
    """Defines permissions and emotional defaults for a family member."""

    member_id: str
    role: str
    allowed_skills: Set[str] = field(default_factory=set)
    blocked_skills: Set[str] = field(default_factory=set)
    emotional_model: EmotionalSnapshot = field(default_factory=EmotionalSnapshot)
    escalation_contacts: Sequence[str] = field(default_factory=tuple)
    content_rating: str = "general"

    def as_dict(self) -> Mapping[str, object]:
        return {
            "member_id": self.member_id,
            "role": self.role,
            "allowed_skills": sorted(self.allowed_skills),
            "blocked_skills": sorted(self.blocked_skills),
            "emotional_model": self.emotional_model.as_dict(),
            "escalation_contacts": list(self.escalation_contacts),
            "content_rating": self.content_rating,
        }


class FamilyModeManager:
    """Maintains per-family access policies and emotional defaults."""

    def __init__(self) -> None:
        self._families: MutableMapping[str, MutableMapping[str, FamilyMemberProfile]] = {}

    def register_member(self, family_id: str, profile: FamilyMemberProfile) -> None:
        family = self._families.setdefault(family_id, {})
        family[profile.member_id] = profile

    def update_permissions(
        self,
        family_id: str,
        member_id: str,
        *,
        allow: Set[str] | None = None,
        block: Set[str] | None = None,
    ) -> None:
        family = self._families.setdefault(family_id, {})
        member = family.setdefault(member_id, FamilyMemberProfile(member_id=member_id, role="guest"))
        if allow is not None:
            member.allowed_skills.update(allow)
        if block is not None:
            member.blocked_skills.update(block)

    def emotional_defaults(
        self, family_id: str, member_id: str
    ) -> EmotionalSnapshot:
        family = self._families.get(family_id, {})
        member = family.get(member_id)
        if not member:
            return EmotionalSnapshot()
        return member.emotional_model

    def export_family_state(self, family_id: str) -> Mapping[str, object]:
        family = self._families.get(family_id, {})
        return {member_id: profile.as_dict() for member_id, profile in family.items()}

    def allowed_for(self, family_id: str, member_id: str, skill: str) -> bool:
        family = self._families.get(family_id, {})
        member = family.get(member_id)
        if not member:
            return False
        if skill in member.blocked_skills:
            return False
        if member.allowed_skills and skill not in member.allowed_skills:
            return False
        return True


__all__ = ["FamilyMemberProfile", "FamilyModeManager"]
