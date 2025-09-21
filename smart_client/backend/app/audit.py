from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict

from .config import settings


class AuditLogger:
    def __init__(self, path: Path | None = None):
        self.path = path or settings.audit_log
        self.path.parent.mkdir(parents=True, exist_ok=True)

    def log(self, actor: str, action: str, detail: Dict[str, Any]) -> None:
        record = {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "actor": actor,
            "action": action,
            "detail": detail,
        }
        with self.path.open("a", encoding="utf-8") as fh:
            fh.write(json.dumps(record, ensure_ascii=False) + "\n")


audit_logger = AuditLogger()
