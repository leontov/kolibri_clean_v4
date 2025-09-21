"""Secure IoT command bridge aligned with Kolibri privacy policies."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Mapping, MutableMapping, Optional, Sequence

from kolibri_x.runtime.journal import ActionJournal


@dataclass(frozen=True)
class IoTCommand:
    """Represents a single device action requested by the runtime."""

    device_id: str
    action: str
    parameters: Mapping[str, object] = field(default_factory=dict)
    requires_confirmation: bool = False


@dataclass
class IoTPolicy:
    """Capability policy for IoT actions."""

    allowlist: Mapping[str, Sequence[str]]
    confirmation_required: Sequence[str] = field(default_factory=tuple)
    max_actions_per_session: int = 10

    def is_allowed(self, device_id: str, action: str) -> bool:
        actions = self.allowlist.get(device_id, ())
        return action in actions

    def needs_confirmation(self, device_id: str, action: str) -> bool:
        token = f"{device_id}:{action}"
        return token in self.confirmation_required


class IoTBridge:
    """Validates and journals IoT commands before dispatch."""

    def __init__(self, policy: IoTPolicy, journal: Optional[ActionJournal] = None) -> None:
        self.policy = policy
        self.journal = journal
        self._session_counts: MutableMapping[str, int] = {}

    def dispatch(
        self,
        session_id: str,
        command: IoTCommand,
        *,
        confirmer: Optional[Callable[[IoTCommand], bool]] = None,
    ) -> Mapping[str, object]:
        """Validates the command and returns a deterministic acknowledgement."""

        if not self.policy.is_allowed(command.device_id, command.action):
            self._journal("iot_denied", session_id, command)
            raise PermissionError(f"action {command.action} not allowed for device {command.device_id}")

        current = self._session_counts.get(session_id, 0)
        if current + 1 > self.policy.max_actions_per_session:
            self._journal("iot_rate_limited", session_id, command)
            raise RuntimeError("IoT command limit exceeded")

        requires_confirmation = command.requires_confirmation or self.policy.needs_confirmation(
            command.device_id, command.action
        )
        if requires_confirmation:
            if confirmer is None or not confirmer(command):
                self._journal("iot_unconfirmed", session_id, command)
                raise PermissionError("command requires confirmation")

        count = current + 1
        self._session_counts[session_id] = count

        acknowledgement = {
            "device_id": command.device_id,
            "action": command.action,
            "parameters": dict(command.parameters),
            "status": "executed",
            "session_id": session_id,
            "count": count,
        }
        self._journal("iot_executed", session_id, command, extra=acknowledgement)
        return acknowledgement

    def reset_session(self, session_id: str) -> None:
        self._session_counts.pop(session_id, None)

    def _journal(
        self,
        event: str,
        session_id: str,
        command: IoTCommand,
        *,
        extra: Optional[Mapping[str, object]] = None,
    ) -> None:
        if not self.journal:
            return
        payload = {
            "session_id": session_id,
            "device_id": command.device_id,
            "action": command.action,
            "parameters": dict(command.parameters),
        }
        if extra:
            payload.update(extra)
        self.journal.append(event, payload)


__all__ = ["IoTBridge", "IoTCommand", "IoTPolicy"]
