"""Console chat interface built on top of :mod:`kolibri_x.runtime`."""
from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from datetime import timedelta
from pathlib import Path
from typing import Iterable, List, Mapping, Optional, Sequence

from kolibri_x.kg.ingest import KnowledgeDocument
from kolibri_x.runtime.cache import OfflineCache
from kolibri_x.runtime.journal import JournalEntry, ActionJournal
from kolibri_x.runtime.orchestrator import (
    KolibriRuntime,
    RuntimeRequest,
    RuntimeResponse,
    SkillExecution,
    SkillSandbox,
)
from kolibri_x.skills.store import SkillManifest, SkillStore

CHAT_SKILL_NAME = "chat_responder"
DEFAULT_USER_ID = "cli-user"


def _default_chat_executor(payload: Mapping[str, object]) -> Mapping[str, object]:
    goal = str(payload.get("goal", "")).strip()
    step = str(payload.get("step", "")).strip()
    modalities = payload.get("modalities", [])
    focus = goal or step or "your request"
    return {
        "reply": f"Kolibri: I considered '{focus}'",
        "modalities": list(modalities),
    }


def build_runtime(*, user_id: str = DEFAULT_USER_ID) -> KolibriRuntime:
    """Construct a runtime preconfigured for conversational experiments."""

    skill_store = SkillStore()
    manifest = SkillManifest.from_dict(
        {
            "name": CHAT_SKILL_NAME,
            "version": "0.1.0",
            "inputs": ["text"],
            "permissions": [],
            "billing": "per_call",
            "policy": {},
            "entry": "chat.py",
        }
    )
    skill_store.register(manifest)

    sandbox = SkillSandbox()
    sandbox.register(CHAT_SKILL_NAME, _default_chat_executor)

    cache = OfflineCache(ttl=timedelta(minutes=15))
    journal = ActionJournal()
    runtime = KolibriRuntime(skill_store=skill_store, sandbox=sandbox, cache=cache, journal=journal)
    runtime.privacy.grant(user_id, ["text"])
    return runtime


@dataclass
class ChatSession:
    runtime: KolibriRuntime
    user_id: str = DEFAULT_USER_ID
    journal_tail: int = 5
    last_response: Optional[RuntimeResponse] = None

    def handle_message(self, message: str) -> str:
        text = message.strip()
        if not text:
            return "Введите сообщение для Kolibri."
        if text.startswith(":"):
            return self._handle_command(text[1:])

        request = RuntimeRequest(user_id=self.user_id, goal=text, modalities={"text": text})
        response = self.runtime.process(request)
        self.last_response = response

        parts: List[str] = []
        summary = response.answer.get("summary")
        if summary:
            parts.append(str(summary))
        else:
            parts.append("Kolibri не вернула краткое описание ответа.")

        parts.extend(self._format_executions(response.executions))
        parts.extend(self._format_journal(self.runtime.journal.tail(self.journal_tail)))
        return "\n".join(parts)

    def _handle_command(self, command: str) -> str:
        raw = command.strip()
        key = raw.lower()
        if key in {"quit", "exit"}:
            raise SystemExit(0)
        if key == "journal":
            tail = [entry.to_dict() for entry in self.runtime.journal.tail(self.journal_tail)]
            if not tail:
                return "Журнал пуст."
            return json.dumps(tail, ensure_ascii=False, indent=2)
        if key == "reason":
            if not self.last_response:
                return "Нет доступного лога рассуждений."
            return self.last_response.reasoning.to_json()
        if key.startswith("export"):
            if not self.last_response:
                return "Нет ответа для выгрузки."
            parts = raw.split(maxsplit=1)
            if len(parts) < 2:
                return "Укажите путь: :export explanations.json"
            return self._export_explanations(parts[1])
        return f"Неизвестная команда: :{command}"

    def _format_executions(self, executions: Sequence[SkillExecution]) -> List[str]:
        if not executions:
            return ["Навыковая часть плана не была выполнена."]
        lines = ["Шаги навыков:"]
        for execution in executions:
            lines.append(self._describe_execution(execution))
        return lines

    def _describe_execution(self, execution: SkillExecution) -> str:
        skill = execution.skill or "<none>"
        status = str(execution.output.get("status", "unknown"))
        if status == "ok":
            result = execution.output.get("result", {})
            if isinstance(result, Mapping):
                reply = result.get("reply") or result.get("draft") or result.get("message")
            else:
                reply = result
            detail = f" — {reply}" if reply else ""
            return f"- {skill}: выполнен{detail}"
        if status == "policy_blocked":
            policy = execution.output.get("policy")
            reason = execution.output.get("reason")
            detail = f" (политика: {policy})" if policy else ""
            if reason:
                detail += f" — {reason}"
            return f"- {skill}: заблокирован политикой{detail}"
        if status == "error":
            message = execution.output.get("message", "неизвестная ошибка")
            return f"- {skill}: ошибка — {message}"
        return f"- {skill}: статус {status}"

    def _format_journal(self, entries: Sequence[JournalEntry]) -> List[str]:
        if not entries:
            return ["Журнал пуст."]
        lines = ["Журнал (последние события):"]
        for entry in entries:
            lines.append(f"- #{entry.index} {entry.event}")
        return lines

    def _export_explanations(self, path_str: str) -> str:
        target = Path(path_str).expanduser()
        data = {
            "answer": dict(self.last_response.answer) if self.last_response else {},
            "reasoning": self.last_response.reasoning.to_dict() if self.last_response else {},
            "proofs": [proof.to_dict() for proof in (self.last_response.proofs or [])],
            "journal": [entry.to_dict() for entry in self.runtime.journal.tail(self.journal_tail)],
        }
        try:
            target.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
        except OSError as exc:
            return f"Ошибка записи файла: {exc}"
        return f"Объяснения сохранены в {target}"


def _iter_knowledge_paths(path: Path) -> Iterable[Path]:
    if path.is_file():
        yield path
        return
    if path.is_dir():
        for candidate in sorted(p for p in path.rglob("*") if p.is_file()):
            yield candidate


def _ingest_paths(runtime: KolibriRuntime, paths: Sequence[str]) -> None:
    for raw in paths:
        current = Path(raw).expanduser().resolve()
        if not current.exists():
            print(f"[ingest] путь не найден: {raw}")
            continue
        for file_path in _iter_knowledge_paths(current):
            try:
                content = file_path.read_text(encoding="utf-8")
            except OSError as exc:
                print(f"[ingest] ошибка чтения {file_path}: {exc}")
                continue
            document = KnowledgeDocument(
                doc_id=f"cli::{file_path.stem}",
                source=str(file_path),
                title=file_path.stem,
                content=content,
                tags=("cli",),
            )
            report = runtime.ingest_document(document)
            print(
                "[ingest]", file_path,
                f"nodes={report.nodes_added}",
                f"edges={report.edges_added}",
                f"conflicts={len(report.conflicts)}",
                f"warnings={len(report.warnings)}",
            )


def main(argv: Optional[Sequence[str]] = None) -> None:
    parser = argparse.ArgumentParser(description="Chat with KolibriRuntime")
    parser.add_argument("--user-id", default=DEFAULT_USER_ID, help="identifier for the chat user")
    parser.add_argument(
        "--knowledge",
        action="append",
        default=[],
        metavar="PATH",
        help="path to a knowledge file or directory to ingest before chatting",
    )
    parser.add_argument(
        "--journal-tail",
        type=int,
        default=5,
        help="number of journal entries to display in responses",
    )
    args = parser.parse_args(argv)

    runtime = build_runtime(user_id=args.user_id)
    session = ChatSession(runtime=runtime, user_id=args.user_id, journal_tail=max(1, args.journal_tail))

    if args.knowledge:
        _ingest_paths(runtime, args.knowledge)

    print("Kolibri CLI chat. Команды: :journal, :reason, :export <файл>, :quit")
    try:
        while True:
            try:
                user_input = input("> ")
            except EOFError:
                print()
                break
            try:
                output = session.handle_message(user_input)
            except SystemExit:
                print("Выход из чата.")
                break
            if output:
                print(output)
    except KeyboardInterrupt:
        print("\nЗавершение по Ctrl+C")


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    main()
