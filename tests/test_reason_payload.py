import json
import subprocess
from pathlib import Path


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    binary = repo_root / "bin" / "reason_payload_test"
    result = subprocess.run(
        [str(binary)],
        check=True,
        capture_output=True,
        text=True,
    )
    payload = result.stdout.strip()

    data = json.loads(payload)
    expected_order = [
        "step",
        "parent",
        "seed",
        "formula",
        "eff",
        "compl",
        "prev",
        "votes",
    ]
    search_start = 0
    for key in expected_order:
        token = f'"{key}":'
        position = payload.find(token, search_start)
        if position == -1:
            raise AssertionError(f"missing field order for {key}")
        search_start = position + len(token)

    if not payload.endswith("}"):
        raise AssertionError("payload must end with closing brace")

    if payload.count("}") != 1:
        raise AssertionError("payload must contain exactly one closing brace")

    if payload.count("]") != 1:
        raise AssertionError("votes array must close exactly once")

    if len(data.get("votes", [])) != 10:
        raise AssertionError("votes array must have exactly 10 entries")


if __name__ == "__main__":
    main()
