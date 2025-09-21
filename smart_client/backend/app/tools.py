from __future__ import annotations

import random
from dataclasses import dataclass
from typing import Any, Dict, List, Tuple

from .audit import audit_logger
from .kprl import kprl_manager


@dataclass
class KGEntry:
    text: str
    source: str


KG_DATASET: List[KGEntry] = [
    KGEntry(
        text="Kolibri FA-10 использует модульную архитектуру планировщика с блоками оценки риска.",
        source="docs/core/fa10_overview.md",
    ),
    KGEntry(
        text="KPRL фиксирует каждое действие агента и добавляет стабилизированную оценку faithfulness.",
        source="docs/core/kprl_spec.md",
    ),
    KGEntry(
        text="Умный клиент должен поддерживать офлайн-режим через PWA и сервис-воркер.",
        source="docs/product/client_requirements.md",
    ),
]


def kg_search(query: str, top_k: int = 3) -> Dict[str, Any]:
    scores: List[Tuple[float, KGEntry]] = []
    query_lower = query.lower()
    for entry in KG_DATASET:
        overlap = len(set(query_lower.split()) & set(entry.text.lower().split()))
        score = overlap / (len(entry.text.split()) + 1)
        scores.append((score, entry))
    scores.sort(key=lambda item: item[0], reverse=True)
    results = [
        {"text": entry.text, "source": entry.source, "score": round(score, 4)}
        for score, entry in scores[:top_k]
        if score > 0
    ]
    audit_logger.log("assistant", "kg_search", {"query": query, "results": results})
    return {"query": query, "results": results}


def kolibri_run(steps: int, seed: int | None = None, lambda_: float | None = None) -> Dict[str, Any]:
    rng = random.Random(seed)
    fa = 0.75 + rng.random() * 0.1
    block = kprl_manager.append_block(
        actor="assistant",
        action="kolibri_run",
        payload={
            "step": steps,
            "summary": f"Симулированный запуск на {steps} шагов",
            "fa": fa,
            "fa_stab": fa - 0.02,
            "fa_map": fa + 0.01,
            "r": fa - 0.05,
        },
    )
    audit_logger.log(
        "assistant",
        "kolibri_run",
        {
            "steps": steps,
            "seed": seed,
            "lambda": lambda_,
            "block_id": block.block_id,
        },
    )
    return {
        "block": {
            "id": block.block_id,
            "payload": dict(block.payload),
            "timestamp": block.timestamp,
        }
    }


def kolibri_verify(path: str = "logs/chain.jsonl") -> Dict[str, Any]:
    result = kprl_manager.verify()
    audit_logger.log("assistant", "kolibri_verify", {"path": path, "ok": result["ok"]})
    return result


def mission_plan(goal: str, constraints: str | None = None, deadline: str | None = None) -> Dict[str, Any]:
    plan = {
        "goal": goal,
        "constraints": constraints,
        "deadline": deadline,
        "steps": [
            "Сформулировать ключевые результаты",
            "Назначить ответственных",
            "Отслеживать прогресс в Ledger",
        ],
        "risks": ["Недостаток данных", "Сдвиг сроков"],
    }
    audit_logger.log("assistant", "mission_plan", plan)
    return plan


def iot_safe_action(device_id: str, action: str, confirm: bool = False) -> Dict[str, Any]:
    if not confirm:
        audit_logger.log(
            "assistant",
            "iot_safe_action_denied",
            {"device_id": device_id, "action": action},
        )
        return {
            "allowed": False,
            "reason": "Требуется явное подтверждение перед выполнением команды.",
        }
    audit_logger.log(
        "assistant",
        "iot_safe_action",
        {"device_id": device_id, "action": action, "status": "simulated"},
    )
    return {
        "allowed": True,
        "status": "simulated",
        "device_id": device_id,
        "action": action,
    }


TOOL_HANDLERS = {
    "kg_search": kg_search,
    "kolibri_run": kolibri_run,
    "kolibri_verify": kolibri_verify,
    "mission_plan": mission_plan,
    "iot_safe_action": iot_safe_action,
}


def execute_tool(name: str, payload: Dict[str, Any]) -> Dict[str, Any]:
    if name not in TOOL_HANDLERS:
        raise ValueError(f"Unknown tool: {name}")
    if name == "kolibri_run":
        payload = payload.copy()
        lambda_ = payload.pop("lambda", None)
        return TOOL_HANDLERS[name](lambda_=lambda_, **payload)
    return TOOL_HANDLERS[name](**payload)


__all__ = ["execute_tool", "TOOL_HANDLERS"]
