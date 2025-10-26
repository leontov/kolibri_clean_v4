import sys
import time
from pathlib import Path

import pytest

sys.path.append(str(Path(__file__).resolve().parents[1]))

from kolibri_x.runtime.journal import ActionJournal  # noqa: E402
from kolibri_x.runtime.orchestrator import SkillExecutionError, SkillSandbox  # noqa: E402


def _hanging_skill(payload):
    interval = float(payload.get("interval", 0.05))
    while True:
        time.sleep(interval)


def _memory_hungry_skill(payload):
    size_mb = int(payload.get("size_mb", 64))
    # Attempt to allocate a single large bytearray to exceed the imposed limit.
    bytearray(size_mb * 1024 * 1024)
    return {"status": "unreachable"}


def test_skill_sandbox_timeout_logs_and_interrupts():
    journal = ActionJournal()
    sandbox = SkillSandbox(time_limit=0.1, memory_limit_mb=128, journal=journal)
    sandbox.register("hang", _hanging_skill)

    with pytest.raises(SkillExecutionError) as exc:
        sandbox.execute("hang", {"interval": 0.5})

    assert "time limit" in str(exc.value)
    entries = journal.entries()
    assert entries[-1].event == "skill_timeout"
    assert entries[-1].payload["skill"] == "hang"


def test_skill_sandbox_memory_limit_logs_error():
    journal = ActionJournal()
    sandbox = SkillSandbox(time_limit=1.0, memory_limit_mb=64, journal=journal)
    sandbox.register("memory", _memory_hungry_skill)

    with pytest.raises(SkillExecutionError):
        sandbox.execute("memory", {"size_mb": 256})
    entries = journal.entries()
    assert entries[-1].event == "skill_memory_limit_exceeded"
    assert entries[-1].payload["skill"] == "memory"
    assert entries[-1].payload["error_type"] == "MemoryError"
