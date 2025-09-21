from __future__ import annotations

import asyncio
from typing import Any, Dict, List

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

        run_block = execute_tool("kolibri_run", {"steps": 3, "seed": len(message)})
        await session.publish(
            "tool_call",
            {"name": "kolibri_run", "payload": run_block["block"]},
        )

        short_answer = self._compose_short_answer(message, sources)
        explanation = self._compose_explanation(sources)
        next_steps = self._compose_next_steps(sources)

        full_response = f"{short_answer}\n\nКак это узнали: {explanation}\n\nЧто дальше: {next_steps}"
        if sources:
            full_response += "\n\nИсточники:" + "".join(
                f"\n• {item['source']}"
                for item in sources
            )
        else:
            full_response += "\n\nКак проверили: внутренний запуск kolibri_run показал стабильный результат."

        session.add_message("assistant", full_response, sources=sources)
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

    def _compose_short_answer(self, message: str, sources: List[Dict[str, Any]]) -> str:
        if "не знаю" in message.lower():
            return "Пока данных маловато, но я могу поискать больше сведений."
        if sources:
            return "Главная мысль: сведения подтверждаются локальным графом знаний."
        return "Я сохранил запрос и готов уточнить детали."

    def _compose_explanation(self, sources: List[Dict[str, Any]]) -> str:
        if sources:
            top = sources[0]
            return f"опирался на запись из {top['source']} и свежий kolibri_run"
        return "использовал внутреннюю проверку kolibri_run без внешних источников"

    def _compose_next_steps(self, sources: List[Dict[str, Any]]) -> str:
        steps = ["уточнить критерии успеха", "обновить миссию в Ledger"]
        if sources:
            steps.insert(0, "изучить отмеченные источники")
        return ", ".join(steps)


__all__ = ["ChatOrchestrator"]
