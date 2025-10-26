"""Tests for loading skill manifests from directories."""
from __future__ import annotations

import json
import logging
import sys
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parents[1]))

from kolibri_x.skills.store import SkillStore  # noqa: E402


def test_load_directory_registers_valid_and_logs_errors(tmp_path, caplog) -> None:
    valid_manifest = {
        "name": "valid_skill",
        "version": "1.0.0",
        "inputs": ["text"],
        "permissions": ["net.read:whitelist"],
        "billing": "per_call",
        "policy": {"pii": "deny"},
        "entry": "valid.py",
    }
    invalid_manifest = {
        "name": "broken_skill",
        "version": "0.1",
        # missing required fields like inputs, permissions, policy, entry
    }

    (tmp_path / "valid.json").write_text(json.dumps(valid_manifest), encoding="utf-8")
    (tmp_path / "invalid.json").write_text(json.dumps(invalid_manifest), encoding="utf-8")

    store = SkillStore()
    with caplog.at_level(logging.ERROR):
        store.load_directory(tmp_path)

    assert store.get("valid_skill") is not None
    assert "broken_skill" not in {manifest.name for manifest in store.list()}

    error_messages = [record.message for record in caplog.records if record.levelno >= logging.ERROR]
    assert any("invalid skill manifest" in message for message in error_messages)
