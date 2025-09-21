from __future__ import annotations

import asyncio
import json
from typing import Any, Dict

from fastapi import BackgroundTasks, FastAPI, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
from sse_starlette.sse import EventSourceResponse

from .audit import audit_logger
from .chat import ChatOrchestrator
from .config import settings
from .kprl import kprl_manager
from .memory import SessionManager
from .tools import TOOL_HANDLERS, execute_tool

app = FastAPI(title=settings.app_name)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
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


@app.get("/api/v1/status")
async def status() -> Dict[str, Any]:
    return {"app": settings.app_name, "tools": list(TOOL_HANDLERS.keys())}


@app.post("/api/v1/chat")
async def chat_endpoint(request: ChatRequest, background: BackgroundTasks) -> Dict[str, Any]:
    background.add_task(orchestrator.handle_message, request.session_id, request.message)
    audit_logger.log("user", "chat", {"session_id": request.session_id})
    return {"status": "accepted"}


@app.get("/api/v1/chat/stream")
async def chat_stream(session_id: str):
    session = session_manager.get_session(session_id)
    queue = session.add_listener()

    async def event_generator():
        try:
            while True:
                event_type, payload = await queue.get()
                yield {
                    "event": event_type,
                    "data": json.dumps(payload, ensure_ascii=False),
                }
        except asyncio.CancelledError:
            raise
        finally:
            session.remove_listener(queue)

    return EventSourceResponse(event_generator(), ping=settings.sse_heartbeat)


@app.get("/api/v1/history")
async def history(session_id: str):
    return session_manager.snapshot(session_id)


@app.post("/api/v1/tools/{name}")
async def call_tool(name: str, request: ToolRequest):
    if name not in TOOL_HANDLERS:
        raise HTTPException(status_code=404, detail="Unknown tool")
    try:
        result = execute_tool(name, request.payload)
    except Exception as exc:  # noqa: BLE001
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    return {"tool": name, "result": result}


@app.get("/api/v1/chain")
async def chain_tail(tail: int = 10):
    return {"blocks": kprl_manager.tail(tail)}


@app.get("/api/v1/chain/stream")
async def chain_stream():
    queue = kprl_manager.subscribe()

    async def event_generator():
        try:
            while True:
                block = await queue.get()
                yield {
                    "event": "block",
                    "data": block.to_json(),
                }
        except asyncio.CancelledError:
            raise
        finally:
            kprl_manager.unsubscribe(queue)

    return EventSourceResponse(event_generator(), ping=settings.sse_heartbeat)


@app.post("/api/v1/chain/verify")
async def chain_verify():
    result = execute_tool("kolibri_verify", {})
    return result


@app.middleware("http")
async def security_headers(request: Request, call_next):
    response = await call_next(request)
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["X-Frame-Options"] = "DENY"
    return response


__all__ = ["app"]
