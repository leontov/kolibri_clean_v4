"""Tests for persisting and restoring the ActionJournal hash chain."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parents[1]))

from kolibri_x.runtime.journal import ActionJournal  # noqa: E402


def test_action_journal_save_and_load(tmp_path: Path) -> None:
    journal = ActionJournal()
    journal.append("alpha", {"value": 1})
    journal.append("beta", {"value": 2})
    journal.append("gamma", {"value": 3})

    path = tmp_path / "journal.jsonl"
    journal.save(path)

    restored = ActionJournal.load(path)
    assert restored.verify()
    assert [entry.hash for entry in restored.entries()] == [entry.hash for entry in journal.entries()]

    restored.append("delta", {"value": 4})
    assert restored.verify()

