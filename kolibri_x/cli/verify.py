"""CLI utility to trigger knowledge verification runs manually."""

from __future__ import annotations

import argparse
import json
from typing import Mapping, Optional, Sequence

from kolibri_x.cli.chat import DEFAULT_USER_ID, _ingest_paths, build_runtime


def _serialise_results(runtime) -> Mapping[str, object]:
    graph = runtime.graph
    verification = graph.verify_with_critics()
    conflicts = graph.detect_conflicts()
    payload = {
        "verification": [
            {
                "node_id": result.node_id,
                "critic": result.critic,
                "score": result.score,
                "provenance": result.provenance,
                "details": dict(result.details),
            }
            for result in verification
        ],
        "conflicts": conflicts,
    }
    return payload


def main(argv: Optional[Sequence[str]] = None) -> None:
    parser = argparse.ArgumentParser(description="Verify knowledge graph entries")
    parser.add_argument("--user-id", default=DEFAULT_USER_ID, help="runtime user identifier")
    parser.add_argument(
        "--knowledge",
        action="append",
        default=[],
        metavar="PATH",
        help="ingest files or directories before verification",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="emit verification report as JSON",
    )
    args = parser.parse_args(argv)

    runtime = build_runtime(user_id=args.user_id)

    if args.knowledge:
        _ingest_paths(runtime, args.knowledge)

    payload = _serialise_results(runtime)

    if args.json:
        print(json.dumps(payload, ensure_ascii=False, indent=2))
        return

    print("[verification] critics executed:", len(payload["verification"]))
    conflicts = payload["conflicts"]
    if conflicts:
        print("[verification] conflicts detected:")
        for left, right in conflicts:
            print(f"  - {left} <> {right}")
    else:
        print("[verification] conflicts: none")


__all__ = ["main"]
