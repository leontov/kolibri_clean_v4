# Kolibri One-Pass Build

Kolibri is a deterministic reasoning ledger that implements FA-10 fractal positioning and the Kolibri Proof-of-Reasoning Ledger (KPRL). The single `kolibri` binary supports generation (`run`), verification (`verify`), and replay (`replay`) of reasoning chains with canonical JSON serialization and OpenSSL SHA/HMAC integrity.

## Prerequisites

### Ubuntu
```bash
sudo apt-get update
sudo apt-get install build-essential pkg-config libssl-dev
```

### macOS
```bash
brew update
brew install openssl@3
export PKG_CONFIG_PATH="/opt/homebrew/opt/openssl@3/lib/pkgconfig:$PKG_CONFIG_PATH"
```

## Build
```bash
make clean && make
```
This produces the `kolibri` binary in the repository root.

## Dataset
`datasets/demo.csv` contains a sampled sine curve (`x,y`) used for demonstration metrics. The runtime splits the dataset into train/validation/test segments to calculate the `eff_*` scores.

## FA-10 Fractal Addressing
Votes from ten virtual agents are deterministically mapped to digits `d_i = round(9 * votes[i])`, producing a 10-character address such as `fa="7056172034"`. Each digit selects a transformation from `configs/fractal_map.default.json`, applying chained DSL rewrites that sculpt the reasoning formula. `fa_stab` records the length of the shared prefix across the latest window (size 5 by default) of FA-10 addresses.

## Kolibri Proof-of-Reasoning Ledger (KPRL)
Each reasoning block is serialized as canonical JSON with the following field order:

| Field | Description |
|-------|-------------|
| `step` | Block index starting from 0 |
| `parent` | Parent index (`-1` for genesis) |
| `seed` | Deterministic seed used for vote generation |
| `formula` | Canonical DSL string |
| `eff` | Score (`eff_val - λ·compl`) |
| `compl` | Formula complexity (node count) |
| `prev` | Hex hash of previous block (empty for genesis) |
| `votes` | Deterministic agent votes |
| `fmt` | Output format identifier |
| `fa` | FA-10 address |
| `fa_stab` | Shared prefix length in the stability window |
| `fa_map` | Fractal map name |
| `r` | Map scaling coefficient |
| `run_id` | Run identifier |
| `cfg_hash` | SHA256 of the config file |
| `eff_train` | Training efficiency proxy |
| `eff_val` | Validation efficiency proxy |
| `eff_test` | Test efficiency proxy |
| `explain` | ≤80 char explanation snippet |
| `hmac_alg` | HMAC algorithm (empty if unused) |
| `salt` | Optional salt string |

Additional fields must always be appended to preserve backward compatibility and verification symmetry.

## CLI Usage
```
kolibri run [--config path] [--steps N] [--beam B] [--lambda L] [--fmt F] [--output file] [--selfcheck]
kolibri verify <file>
kolibri replay <file>
kolibri --help
```

### Example Workflow
```bash
./kolibri run --config configs/kolibri.json --steps 30
./kolibri verify logs/chain.jsonl
./kolibri replay logs/chain.jsonl
```

### Self-check
```bash
./kolibri run --config configs/kolibri.json --steps 30 --selfcheck
```

### HMAC Mode
```bash
export KOLIBRI_HMAC_KEY="secret"
./kolibri run --config configs/kolibri.json --steps 30
./kolibri verify logs/chain.jsonl   # succeeds
export KOLIBRI_HMAC_KEY="wrong"
./kolibri verify logs/chain.jsonl   # fails due to mismatched HMAC
```

## Test Plan
```bash
make test                           # builds and runs all C unit tests
./kolibri run --config configs/kolibri.json --steps 30
./kolibri verify logs/chain.jsonl
./kolibri replay logs/chain.jsonl
python3 - <<'PY'
from pathlib import Path
path = Path('logs/chain.jsonl')
data = path.read_text()
path.write_text(data.replace('"', "'", 1))
PY
./kolibri verify logs/chain.jsonl    # should report failure at step 0
```

## Internals
- **DSL**: Supports nodes `CONST`, `VAR_X`, `PARAM`, `ADD`, `SUB`, `MUL`, `DIV`, `SIN`, `EXP`, `LOG`, `POW`, and `TANH` with safe numeric guards (`safe_div`, `safe_log`, `safe_pow`, `safe_tanh`).
- **Serialization**: Canonical JSON (no spaces, fixed order, `%.17g` numbers, C locale). The same code path is used by `run`, `verify`, and `replay`.
- **Hashing/HMAC**: SHA256 payload digest and optional `HMAC_SHA256` using OpenSSL 3.

