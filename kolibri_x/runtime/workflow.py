"""Workflow planner supporting long-running tasks and reminders."""
from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
import itertools
from typing import Dict, Iterable, List, Mapping, MutableMapping, Optional, Sequence


@dataclass
class TaskStepState:
    """State for a single step inside a workflow."""

    description: str
    tool: Optional[str] = None
    completed: bool = False
    completed_at: Optional[datetime] = None

    def mark_completed(self, timestamp: datetime) -> None:
        self.completed = True
        self.completed_at = timestamp


@dataclass
class ReminderRule:
    """Reminder relative to the workflow deadline."""

    offset: timedelta
    message: str = "Reminder"


@dataclass
class Workflow:
    """Represents a long-running task tracked by the runtime."""

    id: str
    goal: str
    steps: List[TaskStepState]
    deadline: Optional[datetime]
    reminders: Sequence[ReminderRule] = field(default_factory=tuple)
    created_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    metadata: MutableMapping[str, str] = field(default_factory=dict)

    def progress(self) -> float:
        if not self.steps:
            return 1.0
        completed = sum(1 for step in self.steps if step.completed)
        return completed / len(self.steps)

    def is_overdue(self, timestamp: datetime) -> bool:
        return bool(self.deadline and timestamp > self.deadline)

    def pending_steps(self) -> List[TaskStepState]:
        return [step for step in self.steps if not step.completed]


@dataclass
class ReminderEvent:
    workflow_id: str
    message: str
    scheduled_for: datetime


class WorkflowManager:
    """Manages workflows, tracks progress, and emits reminders."""

    def __init__(self, time_provider: Optional[callable] = None) -> None:
        self._time_provider = time_provider or (lambda: datetime.now(timezone.utc))
        self._workflows: Dict[str, Workflow] = {}
        self._id_counter = itertools.count(1)

    def create_workflow(
        self,
        goal: str,
        steps: Iterable[Mapping[str, str | None]],
        deadline: Optional[datetime],
        reminders: Sequence[ReminderRule] | None = None,
        metadata: Optional[Mapping[str, str]] = None,
    ) -> Workflow:
        workflow_id = f"wf-{next(self._id_counter):04d}"
        step_states = [
            TaskStepState(description=step.get("description", ""), tool=step.get("tool"))
            for step in steps
        ]
        workflow = Workflow(
            id=workflow_id,
            goal=goal,
            steps=step_states,
            deadline=deadline,
            reminders=tuple(reminders or ()),
        )
        if metadata:
            workflow.metadata.update(metadata)
        self._workflows[workflow_id] = workflow
        return workflow

    def list_workflows(self) -> Sequence[Workflow]:
        return tuple(self._workflows.values())

    def workflow(self, workflow_id: str) -> Workflow:
        return self._workflows[workflow_id]

    def mark_step_completed(self, workflow_id: str, step_index: int) -> None:
        workflow = self.workflow(workflow_id)
        try:
            step = workflow.steps[step_index]
        except IndexError as exc:
            raise IndexError("step index out of range") from exc
        step.mark_completed(self._time_provider())

    def emit_reminders(self, timestamp: Optional[datetime] = None) -> List[ReminderEvent]:
        now = timestamp or self._time_provider()
        events: List[ReminderEvent] = []
        for workflow in self._workflows.values():
            if not workflow.deadline:
                continue
            for rule in workflow.reminders:
                scheduled = workflow.deadline - rule.offset
                if scheduled <= now:
                    events.append(
                        ReminderEvent(
                            workflow_id=workflow.id,
                            message=rule.message,
                            scheduled_for=scheduled,
                        )
                    )
        # Sort for deterministic order
        events.sort(key=lambda event: (event.scheduled_for, event.workflow_id))
        return events

    def overdue_workflows(self, timestamp: Optional[datetime] = None) -> List[Workflow]:
        now = timestamp or self._time_provider()
        return [wf for wf in self._workflows.values() if wf.is_overdue(now)]


__all__ = [
    "ReminderEvent",
    "ReminderRule",
    "TaskStepState",
    "Workflow",
    "WorkflowManager",
]
