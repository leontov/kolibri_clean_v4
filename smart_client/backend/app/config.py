from __future__ import annotations

import os
from pathlib import Path
from pydantic import BaseModel


class Settings(BaseModel):
    app_name: str = "Kolibri Smart Client"
    log_dir: Path = Path(os.getenv("KOLIBRI_LOG_DIR", "logs"))
    chain_path: Path = Path(os.getenv("KOLIBRI_CHAIN_PATH", "logs/chain.jsonl"))
    audit_log: Path = Path(os.getenv("KOLIBRI_AUDIT_LOG", "logs/audit.log"))
    history_limit: int = 50
    summary_window: int = 10
    sse_heartbeat: float = 20.0

    class Config:
        arbitrary_types_allowed = True


def get_settings() -> Settings:
    settings = Settings()
    settings.log_dir.mkdir(parents=True, exist_ok=True)
    settings.chain_path.parent.mkdir(parents=True, exist_ok=True)
    settings.audit_log.parent.mkdir(parents=True, exist_ok=True)
    return settings


settings = get_settings()
