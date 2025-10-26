"""Secure IoT command bridge aligned with Kolibri privacy policies."""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from time import time
from typing import Callable, Deque, List, Mapping, MutableMapping, Optional, Sequence, Tuple

from kolibri_x.core.encoders import SensorEvent, SensorHub
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
    max_batch_size: int = 5
    max_deferred_actions: int = 25

    def is_allowed(self, device_id: str, action: str) -> bool:
        actions = self.allowlist.get(device_id, ())
        return action in actions

    def needs_confirmation(self, device_id: str, action: str) -> bool:
        token = f"{device_id}:{action}"
        return token in self.confirmation_required


class IoTBridge:
    """Validates and journals IoT commands before dispatch."""

    def __init__(
        self,
        policy: IoTPolicy,
        journal: Optional[ActionJournal] = None,
        *,
        sensor_hub: Optional[SensorHub] = None,
        sensor_signal_prefix: str = "iot",
    ) -> None:
        self.policy = policy
        self.journal = journal
        self._session_counts: MutableMapping[str, int] = {}
        self._deferred: Deque[Tuple[float, str, IoTCommand]] = deque()
        self.sensor_hub = sensor_hub
        self.sensor_signal_prefix = sensor_signal_prefix

    def attach_sensor_hub(self, hub: SensorHub, *, signal_prefix: Optional[str] = None) -> None:
        """Attach a sensor hub that mirrors IoT command effects."""

        self.sensor_hub = hub
        if signal_prefix is not None:
            self.sensor_signal_prefix = signal_prefix

    def dispatch(
        self,
        session_id: str,
        command: IoTCommand,
        *,
        confirmer: Optional[Callable[[IoTCommand], bool]] = None,
    ) -> Mapping[str, object]:
        """Validates the command and returns a deterministic acknowledgement."""

        acknowledgement = self._execute_command(session_id, command, confirmer=confirmer)
        return acknowledgement

    def dispatch_batch(
        self,
        session_id: str,
        commands: Sequence[IoTCommand],
        *,
        confirmer: Optional[Callable[[IoTCommand], bool]] = None,
    ) -> Sequence[Mapping[str, object]]:
        """Dispatch a batch of commands with policy limits enforced."""

        if len(commands) > self.policy.max_batch_size:
            if commands:
                self._journal("iot_batch_rejected", session_id, commands[0], extra={"size": len(commands)})
            raise RuntimeError("IoT batch size exceeded")

        acknowledgements: List[Mapping[str, object]] = []
        for command in commands:
            ack = self._execute_command(session_id, command, confirmer=confirmer)
            acknowledgements.append(ack)
        return acknowledgements

    def queue_delayed(
        self,
        session_id: str,
        command: IoTCommand,
        *,
        available_at: Optional[float] = None,
    ) -> None:
        """Queue a command to be dispatched later when the bridge reconnects."""

        if len(self._deferred) >= self.policy.max_deferred_actions:
            self._journal("iot_queue_overflow", session_id, command)
            raise RuntimeError("Deferred IoT queue capacity exceeded")
        timestamp = available_at if available_at is not None else time()
        self._deferred.append((timestamp, session_id, command))
        self._journal("iot_queued", session_id, command, extra={"available_at": timestamp})

    def release_delayed(
        self,
        *,
        upto: Optional[float] = None,
        confirmer: Optional[Callable[[IoTCommand], bool]] = None,
    ) -> Sequence[Mapping[str, object]]:
        """Release delayed commands that are ready to execute."""

        if not self._deferred:
            return ()
        deadline = upto if upto is not None else time()
        ready: List[Tuple[float, str, IoTCommand]] = []
        remainder: Deque[Tuple[float, str, IoTCommand]] = deque()
        while self._deferred:
            record = self._deferred.popleft()
            if record[0] <= deadline:
                ready.append(record)
            else:
                remainder.append(record)
        self._deferred = remainder
        acknowledgements: List[Mapping[str, object]] = []
        for _, session_id, command in sorted(ready, key=lambda item: item[0]):
            acknowledgements.append(self._execute_command(session_id, command, confirmer=confirmer))
        if acknowledgements:
            self._journal(
                "iot_release",
                acknowledgements[0]["session_id"],
                ready[0][2],
                extra={"released": len(acknowledgements)},
            )
        return acknowledgements

    def merge_after_offline(
        self,
        session_id: str,
        offline_commands: Sequence[IoTCommand],
        *,
        confirmer: Optional[Callable[[IoTCommand], bool]] = None,
    ) -> Sequence[Mapping[str, object]]:
        """Merge queued and offline commands while deduplicating conflicts."""

        carried: List[Tuple[float, str, IoTCommand]] = []
        remaining: Deque[Tuple[float, str, IoTCommand]] = deque()
        while self._deferred:
            timestamp, deferred_session, deferred_command = self._deferred.popleft()
            if deferred_session == session_id:
                carried.append((timestamp, deferred_session, deferred_command))
            else:
                remaining.append((timestamp, deferred_session, deferred_command))
        self._deferred = remaining

        combined: List[Tuple[float, str, IoTCommand]] = carried + [
            (time(), session_id, command) for command in offline_commands
        ]

        deduped: List[IoTCommand] = []
        seen: set[Tuple[str, str, Tuple[Tuple[str, object], ...]]] = set()
        for _, _, command in sorted(combined, key=lambda item: item[0]):
            signature = (
                command.device_id,
                command.action,
                tuple(sorted((str(key), value) for key, value in command.parameters.items())),
            )
            if signature in seen:
                continue
            seen.add(signature)
            deduped.append(command)

        acknowledgements: List[Mapping[str, object]] = []
        for command in deduped:
            acknowledgements.append(self._execute_command(session_id, command, confirmer=confirmer))

        if carried or offline_commands or deduped:
            reference: IoTCommand
            if deduped:
                reference = deduped[0]
            elif offline_commands:
                reference = offline_commands[0]
            else:
                reference = carried[0][2]
            self._journal(
                "iot_offline_merge",
                session_id,
                reference,
                extra={"merged": len(deduped), "carried": len(carried), "incoming": len(offline_commands)},
            )
        return acknowledgements

    def reset_session(self, session_id: str) -> None:
        self._session_counts.pop(session_id, None)
        if self._deferred:
            self._deferred = deque(record for record in self._deferred if record[1] != session_id)

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

    def _execute_command(
        self,
        session_id: str,
        command: IoTCommand,
        *,
        confirmer: Optional[Callable[[IoTCommand], bool]] = None,
    ) -> Mapping[str, object]:
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
        self._mirror_to_sensor_hub(session_id, command)
        return acknowledgement

    def _mirror_to_sensor_hub(self, session_id: str, command: IoTCommand) -> None:
        if not self.sensor_hub:
            return
        parameters = dict(command.parameters)
        value_raw = parameters.get("value", 1.0)
        try:
            value = float(value_raw)
        except (TypeError, ValueError):  # pragma: no cover - defensive
            value = 1.0
        timestamp_raw = parameters.get("timestamp")
        try:
            timestamp = float(timestamp_raw) if timestamp_raw is not None else time()
        except (TypeError, ValueError):
            timestamp = time()
        base_signal = parameters.get("signal_type") or f"{command.device_id}::{command.action}"
        signal_type = f"{self.sensor_signal_prefix}::{base_signal}" if self.sensor_signal_prefix else str(base_signal)
        event = SensorEvent(
            source=session_id,
            signal_type=str(signal_type),
            value=value,
            timestamp=timestamp,
        )
        self.sensor_hub.ingest(event)
        self._journal(
            "iot_sensor_sync",
            session_id,
            command,
            extra={"signal_type": event.signal_type, "value": event.value, "timestamp": event.timestamp},
        )


__all__ = [
    "IoTBridge",
    "IoTCommand",
    "IoTPolicy",
]
