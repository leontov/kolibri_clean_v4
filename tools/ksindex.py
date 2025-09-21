#!/usr/bin/env python3
"""Kolibri Skill index helper."""

import json
import pathlib


def main() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    skills_path = root / "docs" / "skills.json"
    data = {
        "generated": True,
        "skills": [
            {"id": "demo", "name": "Kolibri Demo Skill", "quality": "beta"}
        ],
    }
    skills_path.parent.mkdir(parents=True, exist_ok=True)
    skills_path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    print(f"Skill index written to {skills_path}")


if __name__ == "__main__":
    main()
