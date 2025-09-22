from __future__ import annotations

import asyncio
import json
from typing import Any, Dict

from fastapi import BackgroundTasks, FastAPI, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import RedirectResponse
from pydantic import BaseModel, Field
from sse_starlette.sse import EventSourceResponse

from .audit import audit_logger
from .chat import ChatOrchestrator
from .config import settings
from .kprl import kprl_manager
from .memory import SessionManager
from .tools import TOOL_HANDLERS, execute_tool

app = FastAPI(title=settings.app_name)

# CORS: локальный фронт + разрешение всего (можно сузить позже)
app.add_middleware(
    CORSMiddleware,
    allow_origins=[
        "http://localhost:5173",
        "http://127.0.0.1:5173",
        "*",
    ],
    allow_methods=["*"],
    allow_headers=["*"],
    allow_credentials=False,
)

session_manager = SessionManager(history_limit=settings.history_limit)
orchestrator = ChatOrchestrator(session_manager)


class ChatRequest(BaseModel):
    message: str
    session_id: str = Field(..., min_length=1)


class ToolRequest(BaseModel):
    payload: Dict[str, Any] = Field(default_factory=dict)


# --- UX/ops удобства ---
@app.get("/", include_in_schema=False)
def root():
    # Быстрый вход в Swagger
    return RedirectResponse("/docs")


@app.get("/health", include_in_schema=False)
async def health():
    return {"status": "ok"}


# --- API v1 ---
@app.get("/api/v1/status", summary="Status")
async def status() -> Dict[str, Any]:
    return {"app": settings.app_name, "tools": list(TOOL_HANDLERS.keys())}


@app.post("/api/v1/chat", summary="Chat Endpoint")
async def chat_endpoint(request: ChatRequest, background: BackgroundTasks) -> Dict[str, Any]:
    background.add_task(orchestrator.handle_message, request.session_id, request.message)
    audit_logger.log("user", "chat", {"session_id": request.session_id})
    return {"status": "accepted"}


@app.get("/api/v1/chat/stream", summary="Chat Stream")
async def chat_stream(session_id: str, request: Request):
    """
    SSE стрим: события чата в реальном времени.
    - держим heartbeat
    - корректно выходим при разрыве соединения
    """
    session = session_manager.get_session(session_id)
    queue = session.add_listener()

    async def event_generator():
        try:
            while True:
                # Периодически проверяем, что клиент на линии
                if await request.is_disconnected():
                    break
                try:
                    # ждём событие из очереди не дольше heartbeat; по таймауту шлём ping
                    event_type, payload = await asyncio.wait_for(queue.get(), timeout=settings.sse_heartbeat)
                    yield {"event": event_type, "data": json.dumps(payload, ensure_ascii=False)}
                except asyncio.TimeoutError:
                    # Тихий keep-alive (можно оставить только ping=..., но явное событие тоже ок)
                    yield {"event": "ping", "data": "{}"}
        except asyncio.CancelledError:
            raise
        finally:
            session.remove_listener(queue)

    return EventSourceResponse(
        event_generator(),
        ping=int(settings.sse_heartbeat) if settings.sse_heartbeat is not None else None,
    )


@app.get("/api/v1/history", summary="History")
async def history(session_id: str):
    return session_manager.snapshot(session_id)


@app.post("/api/v1/tools/{name}", summary="Call Tool")
async def call_tool(name: str, request: ToolRequest):
    if name not in TOOL_HANDLERS:
        raise HTTPException(status_code=404, detail="Unknown tool")
    try:
        result = execute_tool(name, request.payload)
    except Exception as exc:  # noqa: BLE001
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    return {"tool": name, "result": result}


@app.get("/api/v1/chain", summary="Chain Tail")
async def chain_tail(tail: int = 10):
    return {"blocks": kprl_manager.tail(tail)}


@app.get("/api/v1/chain/stream", summary="Chain Stream")
async def chain_stream(request: Request):
    queue = kprl_manager.subscribe()

    async def event_generator():
        try:
            while True:
                if await request.is_disconnected():
                    break
                try:
                    block = await asyncio.wait_for(queue.get(), timeout=settings.sse_heartbeat)
                    yield {"event": "block", "data": block.to_json()}
                except asyncio.TimeoutError:
                    yield {"event": "ping", "data": "{}"}
        except asyncio.CancelledError:
            raise
        finally:
            kprl_manager.unsubscribe(queue)

    return EventSourceResponse(
        event_generator(),
        ping=int(settings.sse_heartbeat) if settings.sse_heartbeat is not None else None,
    )


@app.post("/api/v1/chain/verify", summary="Chain Verify")
async def chain_verify():
    result = execute_tool("kolibri_verify", {})
    return result


# Безопасные заголовки
@app.middleware("http")
async def security_headers(request: Request, call_next):
    response = await call_next(request)
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["X-Frame-Options"] = "DENY"
    return response


__all__ = ["app"]