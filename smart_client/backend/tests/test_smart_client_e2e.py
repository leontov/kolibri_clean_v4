from __future__ import annotations

import time

from fastapi.testclient import TestClient

from app.main import app


def test_chat_tool_chain_flow():
    client = TestClient(app)
    session_id = "e2e"

    response = client.post(
        "/api/v1/chat",
        json={"session_id": session_id, "message": "Что нового в Kolibri?"},
    )
    assert response.status_code == 200
    assert response.json()["status"] == "accepted"

    assistant_message = None
    for _ in range(30):
        history = client.get("/api/v1/history", params={"session_id": session_id}).json()
        assistants = [m for m in history["messages"] if m["role"] == "assistant"]
        if assistants:
            assistant_message = assistants[-1]
            break
        time.sleep(0.2)
    assert assistant_message, "не дождались ответа ассистента"
    assert "Источники" in assistant_message["content"] or "Как проверили" in assistant_message["content"]

    verify = client.post("/api/v1/chain/verify").json()
    assert verify["ok"] is True

    chain = client.get("/api/v1/chain", params={"tail": 1}).json()
    assert chain["blocks"], "ожидался хотя бы один блок"

    tool = client.post(
        "/api/v1/tools/iot_safe_action",
        json={"payload": {"device_id": "lamp-1", "action": "on"}},
    ).json()
    assert tool["result"]["allowed"] is False
