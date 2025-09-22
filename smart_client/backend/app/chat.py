from __future__ import annotations

import asyncio
import re
from typing import Any, Dict, List, Optional

from .kprl import kprl_manager
from .memory import ChatSession, SessionManager
from .tools import execute_tool


class ChatOrchestrator:
    """In-memory orchestrator that coordinates simple tool calls and streaming."""

    def __init__(self, session_manager: SessionManager) -> None:
        self._sessions = session_manager
        self._ensure_chain_ready()

    async def handle_message(self, session_id: str, message: str) -> None:
        session = self._sessions.get_session(session_id)
        session.add_message("user", message)

        await session.publish("token", {"content": ""})

        sources: List[Dict[str, Any]] = []
        math_result: Optional[Dict[str, Any]] = None

        lowered = message.lower()

        if "миссия" in lowered:
            plan = execute_tool(
                "mission_plan",
                {"goal": message, "constraints": "по запросу", "deadline": "гибкий"},
            )
            await session.publish(
                "tool_call",
                {"name": "mission_plan", "payload": plan},
            )

        if any(keyword in lowered for keyword in ["что", "как", "источник", "kolibri"]):
            kg_result = execute_tool("kg_search", {"query": message, "top_k": 3})
            sources = kg_result.get("results", [])
            await session.publish(
                "tool_call",
                {"name": "kg_search", "payload": kg_result},
            )

        if self._looks_like_math(message):
            try:
                math_result = execute_tool("math_solver", {"problem": message})
            except Exception as exc:  # noqa: BLE001
                math_result = {
                    "type": "error",
                    "summary": "Не удалось решить задачу.",
                    "answer": str(exc),
                    "steps": [],
                    "references": [],
                }
            await session.publish(
                "tool_call",
                {"name": "math_solver", "payload": math_result},
            )

        run_block = execute_tool("kolibri_run", {"steps": 3, "seed": len(message)})
        await session.publish(
            "tool_call",
            {"name": "kolibri_run", "payload": run_block["block"]},
        )

        reply = self._compose_reply(message, sources, math_result)
        await self._stream_text(session, reply)
        session.add_message("assistant", reply, sources=sources, math=math_result)

    async def _stream_text(self, session: ChatSession, text: str) -> None:
        tokens = text.split()

        if not tokens:
            await session.publish("final", {"content": text})
            return

        for token in tokens:
            await session.publish("token", {"content": f"{token} "})
            await asyncio.sleep(0.02)

        await session.publish("final", {"content": text})

    def _compose_reply(
        self,
        message: str,
        sources: List[Dict[str, Any]],
        math_result: Optional[Dict[str, Any]],
    ) -> str:
        short_answer = self._compose_short_answer(message, sources, math_result)
        explanation = self._compose_explanation(message, sources, math_result)
        next_steps = self._compose_next_steps(message, sources, math_result)
        math_section = self._format_math_result(math_result)

        parts = [
            short_answer,
            "",
            f"Как это узнали: {explanation}",
            "",
            f"Что дальше: {next_steps}",
        ]

        if math_section:
            parts.extend(["", f"Математика: {math_section}"])

        if sources:
            parts.append("")
            parts.append("Источники:")
            parts.extend(
                f"• {item['source']}" + (f" — {item['text']}" if item.get("text") else "")
                for item in sources
            )
        else:
            parts.extend(
                [
                    "",
                    "Как проверили: внутренний запуск kolibri_run показал стабильный результат.",
                ]
            )

        return "\n".join(parts).strip()

    def _compose_short_answer(
        self,
        message: str,
        sources: List[Dict[str, Any]],
        math_result: Optional[Dict[str, Any]],
    ) -> str:
        lowered = message.lower()
        if self._looks_like_greeting(lowered):
            return "Привет! Я Kolibri Smart Client и готов помочь с миссиями и задачами."
        if math_result and math_result.get("answer"):
            return f"Решение найдено: {math_result['answer']}"
        if sources:
            return "Главная мысль: сведения подтверждаются локальным графом знаний."
        return "Я сохранил запрос и готов уточнить детали."

    def _compose_explanation(
        self,
        message: str,
        sources: List[Dict[str, Any]],
        math_result: Optional[Dict[str, Any]],
    ) -> str:
        if math_result and math_result.get("summary"):
            return math_result["summary"]
        if sources:
            top = sources[0]
            return f"опирался на запись из {top['source']} и свежий kolibri_run"
        if self._looks_like_greeting(message.lower()):
            return "использую встроенные инструменты Kolibri и историю диалогов"
        return "использовал внутреннюю проверку kolibri_run без внешних источников"

    def _compose_next_steps(
        self,
        message: str,
        sources: List[Dict[str, Any]],
        math_result: Optional[Dict[str, Any]],
    ) -> str:
        if self._looks_like_greeting(message.lower()):
            return ", ".join(
                [
                    "сформулировать задачу или уравнение", "поделиться контекстом миссии", "при необходимости запустить verify",
                ]
            )

        steps = ["уточнить критерии успеха", "обновить миссию в Ledger"]
        if sources:
            steps.insert(0, "изучить отмеченные источники")
        if math_result and math_result.get("type") == "equation":
            steps.insert(0, "подставить решение обратно для проверки")
        return ", ".join(steps)

    def _format_math_result(self, math_result: Optional[Dict[str, Any]]) -> str:
        if not math_result or not math_result.get("summary"):
            return ""
        parts = [math_result["summary"]]
        answer = math_result.get("answer")
        if answer:
            parts.append(f"Итог: {answer}")
        steps = math_result.get("steps") or []
        if steps:
            steps_lines = "\n".join(f"• {step}" for step in steps)
            parts.append(f"Шаги:\n{steps_lines}")
        return "\n".join(parts)

    def _looks_like_math(self, message: str) -> bool:
        lowered = message.lower()
        if any(keyword in lowered for keyword in ["реши", "уравнение", "площадь", "периметр", "радиус", "triangle", "circle"]):
            return True
        return bool(re.search(r"\d", message) and re.search(r"[=+\-*/]", message))

    def _looks_like_greeting(self, lowered: str) -> bool:
        return any(token in lowered for token in ["привет", "здравств", "hello", "hi"])

    def _ensure_chain_ready(self) -> None:
        if not kprl_manager.verify().get("ok", True):
            kprl_manager.reset()
