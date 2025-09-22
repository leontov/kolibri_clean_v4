#!/usr/bin/env python3
"""Replay Kolibri chain and print aggregate metrics."""

from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path


def load_blocks(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                yield json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"invalid JSON in {path}: {exc}") from exc


def summarize(blocks):
    blocks = list(blocks)
    if not blocks:
        return {
            "count": 0,
            "eff_mean": 0.0,
            "eff_max": 0.0,
            "compl_mean": 0.0,
            "last_formula": "",
        }
    eff_values = [float(b.get("eff", 0.0)) for b in blocks]
    compl_values = [float(b.get("compl", 0.0)) for b in blocks]
    return {
        "count": len(blocks),
        "eff_mean": statistics.mean(eff_values),
        "eff_max": max(eff_values),
        "compl_mean": statistics.mean(compl_values),
        "last_formula": str(blocks[-1].get("formula", "")),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "chain",
        nargs="?",
        default="kolibri_chain.jsonl",
        help="path to chain file (default: kolibri_chain.jsonl)",
    )
    args = parser.parse_args()
    path = Path(args.chain)
    if not path.exists():
        raise SystemExit(f"chain file not found: {path}")
    summary = summarize(load_blocks(path))
    print(
        "replay",
        f"count={summary['count']}",
        f"eff_mean={summary['eff_mean']:.4f}",
        f"eff_max={summary['eff_max']:.4f}",
        f"compl_mean={summary['compl_mean']:.2f}",
        f"last='{summary['last_formula']}'",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
