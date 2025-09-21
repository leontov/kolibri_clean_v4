from __future__ import annotations

from app.tools import execute_tool


def test_math_solver_equation_solves_linear():
    result = execute_tool("math_solver", {"problem": "2*x + 4 = 10"})
    assert result["type"] == "equation"
    assert "x = 3" in result["answer"]
    assert any("SymPy" in reference for reference in result["references"])


def test_math_solver_circle_area():
    result = execute_tool("math_solver", {"problem": "Площадь круга радиусом 5"})
    assert result["type"] == "geometry"
    assert result["figure"] == "circle"
    assert "πr²" in result["answer"]
