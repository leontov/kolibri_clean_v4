"""Runtime mKSI aggregation utilities."""
from __future__ import annotations

import json
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from statistics import mean
from typing import Deque, Mapping, MutableMapping, Optional, Sequence
from urllib.error import URLError
from urllib.request import Request, urlopen


def _clamp(value: float) -> float:
    return max(0.0, min(1.0, value))


@dataclass(frozen=True)
class MKSIValues:
    """Container for individual mKSI axis values."""

    generalization: float
    parsimony: float
    autonomy: float
    reliability: float
    explainability: float
    usability: float

    @property
    def mksi(self) -> float:
        return mean(
            [
                self.generalization,
                self.parsimony,
                self.autonomy,
                self.reliability,
                self.explainability,
                self.usability,
            ]
        )

    def as_dict(self) -> Mapping[str, float]:
        return {
            "generalization": self.generalization,
            "parsimony": self.parsimony,
            "autonomy": self.autonomy,
            "reliability": self.reliability,
            "explainability": self.explainability,
            "usability": self.usability,
            "mksi": self.mksi,
        }


@dataclass(frozen=True)
class RuntimeMksiReport:
    """Current and rolling mKSI snapshot."""

    current: MKSIValues
    rolling: MKSIValues

    def as_dict(self) -> Mapping[str, Mapping[str, float]]:
        return {"current": dict(self.current.as_dict()), "rolling": dict(self.rolling.as_dict())}


class RuntimeMksiAggregator:
    """Aggregates runtime events and SLO data into mKSI values."""

    def __init__(
        self,
        *,
        window: int = 20,
        slo_targets: Optional[Mapping[str, float]] = None,
        latency_budget_ms: float = 2500.0,
        modality_ceiling: int = 4,
        reasoning_target: float = 2.0,
        export_file: Optional[str] = None,
        export_endpoint: Optional[str] = None,
        http_timeout: float = 2.0,
    ) -> None:
        self._history: Deque[MKSIValues] = deque(maxlen=max(1, window))
        self._slo_targets = dict(slo_targets or {})
        self._latency_budget = max(latency_budget_ms, 0.0)
        self._modality_ceiling = max(modality_ceiling, 1)
        self._reasoning_target = max(reasoning_target, 1e-6)
        self._export_path = Path(export_file) if export_file else None
        self._export_endpoint = export_endpoint
        self._http_timeout = max(http_timeout, 0.1)
        self._default_stage_budget = (
            self._latency_budget / 6.0 if self._latency_budget > 0.0 else 600.0
        )

    def observe(
        self,
        *,
        modalities: Sequence[str],
        plan_steps: int,
        executions: Sequence[object],
        reasoning_steps: int,
        adjustments: Mapping[str, float] | None,
        cached: bool,
        slo_snapshot: Mapping[str, Mapping[str, float]] | None,
    ) -> RuntimeMksiReport:
        """Record a runtime interaction and update rolling averages."""

        current = self._compute_values(
            modalities=modalities,
            plan_steps=plan_steps,
            executions=executions,
            reasoning_steps=reasoning_steps,
            adjustments=adjustments or {},
            cached=cached,
            slo_snapshot=slo_snapshot or {},
        )
        self._history.append(current)
        report = RuntimeMksiReport(current=current, rolling=self._aggregate_history())
        self._export(report)
        return report

    def report(self) -> RuntimeMksiReport:
        """Return the most recent snapshot without recording a new event."""

        if self._history:
            current = self._history[-1]
        else:
            current = MKSIValues(0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
        return RuntimeMksiReport(current=current, rolling=self._aggregate_history())

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------
    def _compute_values(
        self,
        *,
        modalities: Sequence[str],
        plan_steps: int,
        executions: Sequence[object],
        reasoning_steps: int,
        adjustments: Mapping[str, float],
        cached: bool,
        slo_snapshot: Mapping[str, Mapping[str, float]],
    ) -> MKSIValues:
        stats = self._execution_stats(executions)
        plan_total = max(plan_steps, 1)
        success_ratio = stats["ok"] / max(stats["total"], 1)
        modality_score = min(len(set(modalities)) / self._modality_ceiling, 1.0)
        cache_penalty = 0.1 if cached else 0.0
        generalization = _clamp(0.55 * success_ratio + 0.35 * modality_score + 0.1 - cache_penalty)

        non_productive = max(plan_steps - stats["ok"], 0)
        failure_ratio = non_productive / plan_total
        parsimony = _clamp(1.0 - 0.8 * failure_ratio)

        policy_penalty = stats["policy_blocked"] / plan_total
        missing_penalty = (stats["missing"] + stats["skipped"]) / plan_total
        autonomy_base = 0.85 if not cached else 0.55
        autonomy = _clamp(autonomy_base - 0.6 * policy_penalty - 0.3 * missing_penalty)

        reliability = self._reliability_score(success_ratio, slo_snapshot)

        reasoning_ratio = reasoning_steps / plan_total
        reasoning_score = min(reasoning_ratio / self._reasoning_target, 1.0)
        coverage = stats["ok"] / plan_total
        explainability = _clamp(0.5 * reasoning_score + 0.5 * coverage)

        usability = self._usability_score(adjustments, slo_snapshot)

        return MKSIValues(
            generalization=generalization,
            parsimony=parsimony,
            autonomy=autonomy,
            reliability=reliability,
            explainability=explainability,
            usability=usability,
        )

    def _execution_stats(self, executions: Sequence[object]) -> MutableMapping[str, float]:
        stats: MutableMapping[str, float] = {
            "total": 0.0,
            "ok": 0.0,
            "policy_blocked": 0.0,
            "errors": 0.0,
            "skipped": 0.0,
            "missing": 0.0,
        }
        for execution in executions:
            output = getattr(execution, "output", None)
            if not isinstance(output, Mapping):
                continue
            stats["total"] += 1.0
            status = str(output.get("status", "unknown"))
            if status == "ok":
                stats["ok"] += 1.0
            elif status == "policy_blocked":
                stats["policy_blocked"] += 1.0
            elif status == "error":
                stats["errors"] += 1.0
            elif status == "skipped":
                stats["skipped"] += 1.0
            elif status == "missing":
                stats["missing"] += 1.0
        return stats

    def _reliability_score(
        self, success_ratio: float, slo_snapshot: Mapping[str, Mapping[str, float]]
    ) -> float:
        stage_scores = []
        for stage, metrics in slo_snapshot.items():
            try:
                p95 = float(metrics.get("p95", 0.0))
            except (TypeError, ValueError):
                p95 = 0.0
            target = self._slo_targets.get(stage, self._default_stage_budget)
            if target <= 0.0:
                continue
            ratio = p95 / target
            if ratio <= 1.0:
                stage_scores.append(1.0)
            elif ratio <= 1.5:
                stage_scores.append(0.6)
            else:
                stage_scores.append(0.2)
        latency_score = mean(stage_scores) if stage_scores else 0.5
        return _clamp(0.6 * success_ratio + 0.4 * latency_score)

    def _usability_score(
        self, adjustments: Mapping[str, float], slo_snapshot: Mapping[str, Mapping[str, float]]
    ) -> float:
        total_latency = 0.0
        for metrics in slo_snapshot.values():
            try:
                total_latency += float(metrics.get("p50", 0.0))
            except (TypeError, ValueError):
                continue
        if self._latency_budget > 0.0:
            latency_ratio = min(total_latency / self._latency_budget, 1.0)
            latency_score = 1.0 - latency_ratio
        else:
            latency_score = 0.5
        adjustment_values = [abs(float(value)) for value in adjustments.values() if isinstance(value, (int, float))]
        adjustment_penalty = mean(adjustment_values) if adjustment_values else 0.0
        adjustment_penalty = min(adjustment_penalty, 1.0)
        return _clamp(0.7 * latency_score + 0.3 * (1.0 - adjustment_penalty))

    def _aggregate_history(self) -> MKSIValues:
        if not self._history:
            return MKSIValues(0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
        return MKSIValues(
            generalization=mean(value.generalization for value in self._history),
            parsimony=mean(value.parsimony for value in self._history),
            autonomy=mean(value.autonomy for value in self._history),
            reliability=mean(value.reliability for value in self._history),
            explainability=mean(value.explainability for value in self._history),
            usability=mean(value.usability for value in self._history),
        )

    def _export(self, report: RuntimeMksiReport) -> None:
        payload = report.as_dict()
        if self._export_path:
            self._export_path.parent.mkdir(parents=True, exist_ok=True)
            self._export_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2))
        if self._export_endpoint:
            data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            request = Request(
                self._export_endpoint,
                data=data,
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            try:  # pragma: no cover - best effort telemetry
                with urlopen(request, timeout=self._http_timeout):
                    pass
            except (URLError, OSError, ValueError):
                pass


__all__ = ["MKSIValues", "RuntimeMksiAggregator", "RuntimeMksiReport"]
