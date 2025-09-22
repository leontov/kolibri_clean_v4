#!/usr/bin/env python3
"""Benchmark Kolibri CLI helpers across hardware configurations."""

from __future__ import annotations

import argparse
import json
import platform
import statistics
import subprocess
import sys
import time
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Dict, List


def load_config(path: Path) -> List[Dict[str, Any]]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    profiles = data.get("profiles", [])
    if not isinstance(profiles, list):
        raise ValueError("profiles must be a list")
    return profiles


def run_command(cmd: List[str], cwd: Path | None) -> float:
    start = time.perf_counter()
    proc = subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        text=True,
    )
    duration = time.perf_counter() - start
    if proc.returncode != 0:
        raise RuntimeError(
            f"command {' '.join(cmd)} failed with code {proc.returncode}: {proc.stderr.strip()}"
        )
    return duration


def ensure_dependency(profile: Dict[str, Any], results: Dict[str, Dict[str, Any]]) -> None:
    depends_on = profile.get("depends_on")
    if not depends_on:
        return
    if depends_on not in results or results[depends_on].get("error"):
        raise RuntimeError(f"profile '{profile['name']}' depends on '{depends_on}'")


def measure_profile(profile: Dict[str, Any], runs: int, cwd: Path) -> Dict[str, Any]:
    name = profile.get("name")
    if not name:
        raise ValueError("profile entry is missing a name")
    command = profile.get("command")
    if not isinstance(command, list) or not command:
        raise ValueError(f"profile '{name}' must define a non-empty command list")
    local_cwd = cwd / profile.get("cwd", ".")
    warmup = int(profile.get("warmup", 0))
    durations: List[float] = []
    for _ in range(max(0, warmup)):
        try:
            run_command(command, local_cwd)
        except Exception:
            # warmups should not mask failures, re-raise below
            break
    for _ in range(runs):
        duration = run_command(command, local_cwd)
        durations.append(duration)
    stats = {
        "runs": runs,
        "durations": durations,
        "min": min(durations),
        "max": max(durations),
        "mean": statistics.mean(durations),
        "median": statistics.median(durations),
    }
    if len(durations) > 1:
        stats["stdev"] = statistics.pstdev(durations)
    return stats


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="configs/cli_profiles.json", help="path to profile config")
    parser.add_argument("--runs", type=int, default=3, help="number of measured runs per profile")
    parser.add_argument("--hardware", help="label for hardware profile")
    parser.add_argument(
        "--output-dir",
        default="logs/cli_profiles",
        help="directory for JSON reports",
    )
    args = parser.parse_args()
    config_path = Path(args.config)
    profiles = load_config(config_path)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    cwd = Path.cwd()
    hardware_label = args.hardware or f"{platform.node()}-{platform.machine()}"
    git_rev = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    timestamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
    results: Dict[str, Dict[str, Any]] = {}
    for profile in profiles:
        name = profile.get("name", "")
        try:
            ensure_dependency(profile, results)
            results[name] = measure_profile(profile, args.runs, cwd)
        except Exception as exc:  # noqa: BLE001
            results[name] = {"error": str(exc)}
    report = {
        "generated_at": timestamp,
        "hardware": hardware_label,
        "git_rev": git_rev,
        "config": str(config_path),
        "runs": args.runs,
        "profiles": results,
    }
    output_path = output_dir / f"{timestamp}_{hardware_label.replace(' ', '_')}.json"
    output_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    for name, data in results.items():
        if "error" in data:
            print(f"{name}: ERROR {data['error']}", file=sys.stderr)
        else:
            print(
                f"{name}: mean={data['mean']:.4f}s min={data['min']:.4f}s max={data['max']:.4f}s",
                file=sys.stdout,
            )
    print(f"report saved to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
