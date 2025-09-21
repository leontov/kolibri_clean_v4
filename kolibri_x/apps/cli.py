"""CLI entry point to exercise the Kolibri-x MVP data flow."""
from __future__ import annotations

import argparse
from pathlib import Path

from kolibri_x.core.encoders import TextEncoder
from kolibri_x.kg.graph import KnowledgeGraph
from kolibri_x.kg.rag import RAGPipeline
from kolibri_x.xai.reasoning import ReasoningLog


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Kolibri-x MVP assistant")
    parser.add_argument("graph", type=Path, help="Path to the knowledge graph JSONL file")
    parser.add_argument("query", type=str, help="User query to answer")
    parser.add_argument("--top-k", type=int, default=5, help="Number of facts to retrieve")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    graph = KnowledgeGraph.import_jsonl(args.graph)
    rag = RAGPipeline(graph, encoder=TextEncoder(dim=32))
    reasoning = ReasoningLog()
    answer = rag.answer(args.query, top_k=args.top_k, reasoning=reasoning)

    print(reasoning.to_json())
    print()
    print(answer["summary"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
