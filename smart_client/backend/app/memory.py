from __future__ import annotations

import asyncio
from collections import deque
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any, Deque, Dict, List, Optional


@dataclass
class Message:
    role: str
    content: str
    timestamp: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    metadata: Dict[str, Any] = field(default_factory=dict)


class ChatSession:
    def __init__(self, session_id: str, history_limit: int = 50):
        self.session_id = session_id
        self.messages: Deque[Message] = deque(maxlen=history_limit)
        self.summary: Optional[str] = None
        self.preferences: Dict[str, Any] = {}
        self.listeners: List[asyncio.Queue] = []
        self.backlog: Deque[tuple[str, Dict[str, Any]]] = deque(maxlen=200)

    def add_message(self, role: str, content: str, **metadata: Any) -> None:
        self.messages.append(Message(role=role, content=content, metadata=metadata))
        self._update_summary()

    def _update_summary(self) -> None:
        if not self.messages:
            self.summary = None
            return
        recent = list(self.messages)[-5:]
        highlights = [m.content.strip().split("\n")[0] for m in recent if m.content.strip()]
        self.summary = " | ".join(highlights[-3:])

    def to_dict(self) -> Dict[str, Any]:
        return {
            "session_id": self.session_id,
            "summary": self.summary,
            "messages": [
                {
                    "role": m.role,
                    "content": m.content,
                    "timestamp": m.timestamp.isoformat() + "Z",
                    "metadata": m.metadata,
                }
                for m in self.messages
            ],
            "preferences": self.preferences,
        }

    def add_listener(self) -> asyncio.Queue:
        queue: asyncio.Queue = asyncio.Queue()
        self.listeners.append(queue)
        for event_type, payload in self.backlog:
            queue.put_nowait((event_type, payload))
        return queue

    def remove_listener(self, queue: asyncio.Queue) -> None:
        if queue in self.listeners:
            self.listeners.remove(queue)

    async def publish(self, event_type: str, payload: Dict[str, Any]) -> None:
        self.backlog.append((event_type, payload))
        for queue in list(self.listeners):
            await queue.put((event_type, payload))


class SessionManager:
    def __init__(self, history_limit: int = 50):
        self._sessions: Dict[str, ChatSession] = {}
        self.history_limit = history_limit

    def get_session(self, session_id: str) -> ChatSession:
        if session_id not in self._sessions:
            self._sessions[session_id] = ChatSession(session_id, self.history_limit)
        return self._sessions[session_id]

    def delete_session(self, session_id: str) -> None:
        if session_id in self._sessions:
            del self._sessions[session_id]

    def snapshot(self, session_id: str) -> Dict[str, Any]:
        return self.get_session(session_id).to_dict()


__all__ = ["SessionManager", "ChatSession", "Message"]
