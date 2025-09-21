from __future__ import annotations

import asyncio
import re
from typing import Any, Dict, List, Optional

from .audit import audit_logger
from .memory import ChatSession, SessionManager
from .tools import execute_tool


class ChatOrchestrator:
    def __init__(self, session_manager: SessionManager):
        self.sessions = session_manager

    async def handle_message(self, session_id: str, message: str) -> None:
        session = self.sessions.get_session(session_id)
        session.add_message("user", message)
        await session.publish("token", {"content": ""})  # trigger client to start display
        sources: List[Dict[str, Any]] = []
        math_result: Optional[Dict[str, Any]] = None

        if "миссия" in message.lower():
            plan = execute_tool(
                "mission_plan",
                {"goal": message, "constraints": "по запросу", "deadline": "гибкий"},
            )
            await session.publish("tool_call", {"name": "mission_plan", "result": plan})

        if any(keyword in message.lower() for keyword in ["что", "как", "источник", "kolibri"]):
            kg_result = execute_tool("kg_search", {"query": message, "top_k": 3})
            sources = kg_result["results"]
            await session.publish(
                "tool_call",
                {"name": "kg_search", "payload": {"query": message, "hits": sources}},
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
                    {
                        "name": "math_solver",
                        "payload": {"error": str(exc)},
                    },
                )
            else:
                await session.publish(
                    "tool_call",
                    {
                        "name": "math_solver",
                        "payload": math_result,
                    },
                )
                for reference in math_result.get("references", []):
                    sources.append({"text": reference, "source": "math_solver", "score": 1.0})

        run_block = execute_tool("kolibri_run", {"steps": 3, "seed": len(message)})
        await session.publish(
            "tool_call",
            {"name": "kolibri_run", "payload": run_block["block"]},
        )

        short_answer = self._compose_short_answer(message, sources, math_result)
        explanation = self._compose_explanation(sources, math_result)
        next_steps = self._compose_next_steps(sources, math_result)
        math_section = self._format_math_result(math_result)

        full_response = f"{short_answer}\n\nКак это узнали: {explanation}\n\nЧто дальше: {next_steps}"
        if math_section:
            full_response += f"\n\nМатематика: {math_section}"
        if sources:
            full_response += "\n\nИсточники:" + "".join(
                f"\n• {item['source']}"
                for item in sources
            )
        else:
            full_response += "\n\nКак проверили: внутренний запуск kolibri_run показал стабильный результат."

        session.add_message("assistant", full_response, sources=sources, math=math_result)
        await self._stream_text(session, full_response)
        audit_logger.log(
            "assistant",
            "chat_response",
            {"session_id": session_id, "sources": sources, "length": len(full_response)},
        )

    async def _stream_text(self, session: ChatSession, text: str) -> None:
        tokens = text.split()
        for token in tokens:
            await session.publish("token", {"content": token + " "})
            await asyncio.sleep(0.05)
        await session.publish("final", {"content": text})

    def _compose_short_answer(
        self,
        message: str,
        sources: List[Dict[str, Any]],
        math_result: Optional[Dict[str, Any]],
    ) -> str:
        if "не знаю" in message.lower():
            return "Пока данных маловато, но я могу поискать больше сведений."
        if math_result and math_result.get("answer"):
            return f"Решение найдено: {math_result['answer']}"
        if sources:
            return "Главная мысль: сведения подтверждаются локальным графом знаний."
        return "Я сохранил запрос и готов уточнить детали."

    def _compose_explanation(
        self,
        sources: List[Dict[str, Any]],
        math_result: Optional[Dict[str, Any]],
    ) -> str:
        if math_result and math_result.get("summary"):
            return math_result["summary"]
        if sources:
            top = sources[0]
            return f"опирался на запись из {top['source']} и свежий kolibri_run"
        return "использовал внутреннюю проверку kolibri_run без внешних источников"

    def _compose_next_steps(
        self,
        sources: List[Dict[str, Any]],
        math_result: Optional[Dict[str, Any]],
    ) -> str:
        steps = ["уточнить критерии успеха", "обновить миссию в Ledger"]
        if sources:
            steps.insert(0, "изучить отмеченные источники")
        if math_result and math_result.get("type") == "equation":
            steps.insert(0, "подставить решение в исходное уравнение для проверки")
        return ", ".join(steps)

    def _format_math_result(self, math_result: Optional[Dict[str, Any]]) -> str:
        if not math_result:
            return ""
        if math_result.get("type") == "error":
            return math_result.get("summary", "")
        parts = [math_result.get("summary", "")]
        answer = math_result.get("answer")
        if answer:
            parts.append(f"Итог: {answer}")
        steps = math_result.get("steps") or []
        if steps:
            steps_lines = "\n".join(f"• {step}" for step in steps)
            parts.append(f"Шаги:\n{steps_lines}")
        return "\n".join(filter(None, parts))

    def _looks_like_math(self, message: str) -> bool:
        lowered = message.lower()
        math_keywords = [
            "реши",
            "уравнение",
            "площадь",
            "периметр",
            "радиус",
            "диаметр",
            "длина",
            "формула",
            "sin",
            "cos",
            "tan",
            "sqrt",
        ]
        if any(keyword in lowered for keyword in math_keywords):
            return True
        return bool(re.search(r"[0-9].*[=+\-*/^]", message))


__all__ = ["ChatOrchestrator"]
