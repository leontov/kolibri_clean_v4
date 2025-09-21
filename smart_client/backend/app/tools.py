from __future__ import annotations

import math
import random
import re
from dataclasses import dataclass
from typing import Any, Dict, List, Tuple

import sympy as sp
from sympy.core.sympify import SympifyError

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


def _parse_number(value: str) -> float:
    return float(value.replace(",", "."))


def _geometry_solver(problem: str) -> Dict[str, Any] | None:
    lower = problem.lower()
    number_pattern = r"[-+]?\d+(?:[\.,]\d+)?"

    if "круг" in lower or "circle" in lower:
        radius_match = re.search(r"(?:радиус(?:ом)?|radius)\s*(?P<radius>" + number_pattern + ")", lower)
        if radius_match:
            radius = _parse_number(radius_match.group("radius"))
            area = math.pi * radius**2
            circumference = 2 * math.pi * radius
            return {
                "type": "geometry",
                "figure": "circle",
                "summary": (
                    f"Для круга радиусом {radius:.6g} площадь {area:.6g}, длина окружности {circumference:.6g}."
                ),
                "answer": f"S = πr² = {area:.6g}, L = 2πr = {circumference:.6g}",
                "steps": [
                    "Используем формулу площади круга S = πr².",
                    "Используем формулу длины окружности L = 2πr.",
                ],
                "references": ["Геометрия: площадь и длина окружности"],
            }

    if "прямоугольник" in lower or "rectangle" in lower:
        length_match = re.search(r"(?:длина|length)\s*(?P<length>" + number_pattern + ")", lower)
        width_match = re.search(r"(?:ширина|width)\s*(?P<width>" + number_pattern + ")", lower)
        if length_match and width_match:
            length = _parse_number(length_match.group("length"))
            width = _parse_number(width_match.group("width"))
            area = length * width
            perimeter = 2 * (length + width)
            return {
                "type": "geometry",
                "figure": "rectangle",
                "summary": (
                    f"Площадь прямоугольника {area:.6g}, периметр {perimeter:.6g}."
                ),
                "answer": f"S = a·b = {area:.6g}, P = 2(a+b) = {perimeter:.6g}",
                "steps": [
                    "Используем формулу площади прямоугольника S = a·b.",
                    "Используем формулу периметра P = 2(a + b).",
                ],
                "references": ["Геометрия: площадь и периметр прямоугольника"],
            }

    if "треугольник" in lower or "triangle" in lower:
        base_match = re.search(r"(?:основани[ея]|base)\s*(?P<base>" + number_pattern + ")", lower)
        height_match = re.search(r"(?:высот[аеы]|height)\s*(?P<height>" + number_pattern + ")", lower)
        if base_match and height_match:
            base = _parse_number(base_match.group("base"))
            height = _parse_number(height_match.group("height"))
            area = 0.5 * base * height
            return {
                "type": "geometry",
                "figure": "triangle",
                "summary": f"Площадь треугольника {area:.6g}.",
                "answer": f"S = 1/2 · a · h = {area:.6g}",
                "steps": [
                    "Используем формулу площади треугольника S = 1/2 · a · h.",
                ],
                "references": ["Геометрия: площадь треугольника"],
            }

    return None


def math_solver(problem: str) -> Dict[str, Any]:
    geometry = _geometry_solver(problem)
    if geometry:
        audit_logger.log("assistant", "math_solver", {"problem": problem, "type": geometry["figure"]})
        return geometry

    allowed_chars = set("0123456789+-*/^=()., πabcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")
    cleaned = "".join(ch for ch in problem if ch in allowed_chars or ch.isspace())
    cleaned = cleaned.replace(",", ".").replace("π", "pi")
    cleaned = re.sub(r"\s+", " ", cleaned).strip()

    expression_text = cleaned or problem

    locals_map: Dict[str, sp.Symbol] = {}
    for symbol in sorted(set(re.findall(r"[a-zA-Z]", expression_text))):
        locals_map[symbol] = sp.symbols(symbol)
    if not locals_map:
        locals_map["x"] = sp.symbols("x")

    try:
        if "=" in expression_text:
            left, right = expression_text.split("=", 1)
            left_expr = sp.sympify(left, locals=locals_map)
            right_expr = sp.sympify(right, locals=locals_map)
            equation = sp.Eq(left_expr, right_expr)
            solutions = sp.solve(equation, list(locals_map.values()), dict=True)
            if solutions:
                formatted = [
                    ", ".join(f"{str(var)} = {sp.simplify(value)}" for var, value in solution.items())
                    for solution in solutions
                ]
                answer = "; ".join(formatted)
            else:
                answer = "Решений не найдено в вещественных числах."
            result = {
                "type": "equation",
                "summary": f"Решил уравнение {sp.pretty(equation)}.",
                "answer": answer,
                "steps": [
                    f"Записываем уравнение: {sp.pretty(equation)}.",
                    "Решаем уравнение с помощью символьного решателя SymPy.",
                ],
                "references": ["Алгебра: решение уравнений", "SymPy solve"],
            }
        else:
            expr = sp.sympify(expression_text, locals=locals_map)
            exact_value = sp.simplify(expr)
            try:
                numeric_value = float(exact_value.evalf())
            except (TypeError, ValueError):
                numeric_value = None
            steps = [f"Упрощаем выражение: {sp.pretty(expr)} → {exact_value}."]
            if numeric_value is not None:
                steps.append(f"Численное значение: {numeric_value:.6g}.")
            result = {
                "type": "expression",
                "summary": "Вычислено алгебраическое выражение.",
                "answer": (
                    f"точно: {exact_value}" + (f", приближенно: {numeric_value:.6g}" if numeric_value is not None else "")
                ),
                "steps": steps,
                "references": ["Алгебра: преобразование выражений", "SymPy simplify"],
            }
    except SympifyError as exc:
        audit_logger.log(
            "assistant",
            "math_solver_error",
            {"problem": problem, "error": str(exc)},
        )
        return {
            "type": "error",
            "summary": "Не удалось разобрать математическое выражение.",
            "answer": "",
            "steps": [],
            "references": [],
            "error": str(exc),
        }

    audit_logger.log("assistant", "math_solver", {"problem": problem, "type": result["type"]})
    return result


TOOL_HANDLERS = {
    "kg_search": kg_search,
    "kolibri_run": kolibri_run,
    "kolibri_verify": kolibri_verify,
    "mission_plan": mission_plan,
    "iot_safe_action": iot_safe_action,
    "math_solver": math_solver,
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
