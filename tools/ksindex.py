#!/usr/bin/env python3
import json
import sys
from pathlib import Path

def load_lines(path: Path):
    for line in path.read_text().splitlines():
        if line.strip():
            yield json.loads(line)

def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("Usage: ksindex.py <chain.jsonl>")
        return 1
    path = Path(argv[1])
    if not path.exists():
        print(f"File not found: {path}")
        return 1
    for block in load_lines(path):
        step = block.get("step")
        fa = block.get("fa")
        eff = block.get("eff")
        hash_hex = block.get("hash")
        print(f"step={step:02d} fa={fa} eff={eff:.5f} hash={hash_hex}")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
