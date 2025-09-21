from pathlib import Path
import sys

sys.path.append(str(Path(__file__).resolve().parents[1]))

from kolibri_x.cli.chat import CHAT_SKILL_NAME, ChatSession, build_runtime


def test_build_runtime_registers_chat_skill() -> None:
    runtime = build_runtime(user_id="tester")
    manifest = runtime.skill_store.get(CHAT_SKILL_NAME)
    assert manifest is not None
    assert CHAT_SKILL_NAME in runtime.sandbox.registered()
    payload = {"goal": "test", "step": "respond", "modalities": ["text"]}
    result = runtime.sandbox.execute(CHAT_SKILL_NAME, payload)
    assert result["reply"].startswith("Kolibri: I considered")


def test_chat_session_handle_message_returns_summary() -> None:
    runtime = build_runtime(user_id="tester")
    session = ChatSession(runtime=runtime, user_id="tester", journal_tail=3)
    output = session.handle_message("Привет, Kolibri")
    assert "Шаги навыков:" in output
    assert CHAT_SKILL_NAME in output
    assert "Журнал (последние события):" in output
    assert "Kolibri" in output

    reasoning_log = session.handle_message(":reason")
    assert "steps" in reasoning_log
