from __future__ import annotations

import asyncio
import json
from collections import OrderedDict
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List

from .config import settings


def format_number(value: float) -> str:
    return f"{value:.17g}"


@dataclass
class ReasonBlock:
    block_id: str
    actor: str
    action: str
    payload: OrderedDict
    timestamp: str

    def to_json(self) -> str:
        data = OrderedDict(
            (
                ("id", self.block_id),
                ("timestamp", self.timestamp),
                ("actor", self.actor),
                ("action", self.action),
                ("payload", self.payload),
            )
        )
        return json.dumps(data, ensure_ascii=False)


class KPRLManager:
    def __init__(self, path: Path | None = None):
        self.path = path or settings.chain_path
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._subscribers: List[asyncio.Queue] = []
        if not self.path.exists():
            self.path.touch()

    def _next_id(self) -> str:
        return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")

    def append_block(self, actor: str, action: str, payload: Dict[str, Any]) -> ReasonBlock:
        ordered_payload = OrderedDict()
        for key, value in payload.items():
            if isinstance(value, float):
                ordered_payload[key] = format_number(value)
            else:
                ordered_payload[key] = value
        block = ReasonBlock(
            block_id=self._next_id(),
            actor=actor,
            action=action,
            payload=ordered_payload,
            timestamp=datetime.now(timezone.utc).isoformat(),
        )
        with self.path.open("a", encoding="utf-8") as fh:
            fh.write(block.to_json() + "\n")
        asyncio.create_task(self._broadcast(block))
        return block

    async def _broadcast(self, block: ReasonBlock) -> None:
        for queue in list(self._subscribers):
            await queue.put(block)

    def subscribe(self) -> asyncio.Queue:
        queue: asyncio.Queue = asyncio.Queue()
        self._subscribers.append(queue)
        return queue

    def unsubscribe(self, queue: asyncio.Queue) -> None:
        if queue in self._subscribers:
            self._subscribers.remove(queue)

    def tail(self, n: int = 10) -> List[Dict[str, Any]]:
        lines = self.path.read_text(encoding="utf-8").strip().splitlines()
        result: List[Dict[str, Any]] = []
        for line in lines[-n:]:
            if line:
                result.append(json.loads(line))
        return result

    def verify(self) -> Dict[str, Any]:
        issues: List[str] = []
        with self.path.open("r", encoding="utf-8") as fh:
            for idx, line in enumerate(fh, start=1):
                try:
                    block = json.loads(line)
                except json.JSONDecodeError:
                    issues.append(f"Line {idx}: invalid JSON")
                    continue
                payload = block.get("payload", {})
                required = ["step", "summary", "fa", "fa_stab", "fa_map", "r"]
                for key in required:
                    if key not in payload:
                        issues.append(f"Line {idx}: missing {key} in payload")
                for numeric_field in ["fa", "fa_stab", "fa_map", "r"]:
                    if numeric_field in payload and not isinstance(payload[numeric_field], str):
                        issues.append(
                            f"Line {idx}: field {numeric_field} must be formatted string"
                        )
        return {"ok": not issues, "issues": issues}

    def reset(self) -> None:
        self.path.write_text("", encoding="utf-8")


kprl_manager = KPRLManager()
