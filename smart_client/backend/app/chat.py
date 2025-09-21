import asyncio
from typing import Any, AsyncGenerator, Dict, List
from collections import defaultdict

class ChatOrchestrator:
    """
    Минимальный рабочий оркестратор чата:
    - chat(message, session_id) -> dict с ответом
    - stream(session_id) -> AsyncGenerator для SSE
    - history(session_id) -> список сообщений
    Совместим с app.main эндпоинтами.
    """

    def __init__(self) -> None:
        self._history: Dict[str, List[Dict[str, str]]] = defaultdict(list)

    async def chat(self, message: str, session_id: str) -> Dict[str, Any]:
        reply = self._simple_reply(message)
        self._history[session_id].append({"role": "user", "content": message})
        self._history[session_id].append({"role": "assistant", "content": reply})
        return {"reply": reply}

    async def stream(self, session_id: str) -> AsyncGenerator[dict, None]:
        """
        SSE-стрим: отдаём токены последнего ответа ассистента.
        EventSourceResponse в app.main умеет потреблять такой генератор.
        """
        last_reply = ""
        hist = self._history.get(session_id, [])
        if hist and hist[-1]["role"] == "assistant":
            last_reply = hist[-1]["content"]

        if not last_reply:
            last_reply = "Нет данных для стрима — сначала отправь сообщение."

        for token in last_reply.split():
            # формат совместим со sse_starlette: словарь с ключом "data"
            yield {"event": "token", "data": token + " "}
            await asyncio.sleep(0.02)

        # финальный маркер завершения
        yield {"event": "done", "data": ""}

    def history(self, session_id: str) -> List[Dict[str, str]]:
        return list(self._history.get(session_id, []))

    def _simple_reply(self, message: str) -> str:
        # Плейсхолдер логики. Здесь можно подключить твой Kolibri-ядро/агентов.
        return f"Колибри: я услышал — {message}"
