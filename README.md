# Kolibri One-Pass Build

Kolibri is a deterministic research platform that ships a cryptographically verifiable ledger (KPRL), FA-10 fractal positioning and a SPA/PWA dashboard. This repository contains both the C backend (single `kolibri` binary) and the React + Vite frontend.

## Features

- **FA-10 positioning** – votes are transformed into a 10-digit fractal address and stability index.
- **KPRL ledger** – payloads are serialized with a canonical JSON printer, hashed with SHA256 and optionally HMAC-SHA256.
- **CLI** – `kolibri` supports `run`, `verify`, `replay` and `serve` commands for generation, validation and HTTP serving.
- **SPA/PWA dashboard** – Vite + React 18 + Zustand + TailwindCSS + shadcn/ui patterns and Recharts visualizations.
- **PWA** – offline-first service worker, install prompt, cache-first for static assets and stale-while-revalidate for `/api/v1/status`.
- **SSE streaming** – `/api/v1/chain/stream` pushes `block`, `verify` and `metric` events to the dashboard.

## Backend layout

```
backend/
  include/...
  src/...
configs/
datasets/
logs/
```

### CLI commands

- `kolibri run --config configs/kolibri.json --steps 30 --beam 1 --lambda 0.01 --fmt 5` – generate a 30 block chain.
- `kolibri verify --config configs/kolibri.json logs/chain.jsonl` – recompute hashes/HMAC with the supplied config and validate parent links.
- `kolibri replay --config configs/kolibri.json` – print a textual summary of the recorded chain.
- `kolibri serve --port 8080 --static web/dist [--cors-dev]` – launch the HTTP API, SPA static file server and SSE stream.

### Payload schema (canonical order)

`step,parent,seed,formula,eff,compl,prev,votes,fmt,fa,fa_stab,fa_map,r,run_id,cfg_hash,eff_train,eff_val,eff_test,explain,hmac_alg,salt`

Fields are append-only – new attributes must be added to the end of the payload to preserve deterministic verification.

### API surface

- `GET /api/v1/status` → `{version, run_id, time_utc, fmt}`
- `GET /api/v1/chain?tail=N` → `[{payload:{...},hash:"...",hmac:"..."}]`
- `GET /api/v1/chain/stream` → SSE (`block`, `verify`, `metric` events)
- `POST /api/v1/run` → `{started:true, steps}`
- `POST /api/v1/verify` → `{ok:true, path}`
- `GET /api/v1/skills` → demo payload

### Building the backend

The build depends on OpenSSL 3, `pkg-config`, `pthread` and a C11 compiler.

```bash
make            # builds kolibri
make test       # runs the C test suite
```

#### OpenSSL setup

- **Ubuntu**: `sudo apt install build-essential pkg-config libssl-dev`
- **macOS (Homebrew)**: `brew install openssl@3 pkg-config`

### Frontend

The SPA/PWA lives in `web/`. It uses Vite + React + TypeScript and includes TailwindCSS utilities and Recharts charts.

```bash
cd web
npm install
npm run build   # output in web/dist
npm run dev     # start Vite dev server
```

> **Note:** PWA icon PNGs are generated locally via `npm run generate:icons` (automatically executed by `predev`/`prebuild`) so the repository stays binary-free.

The service worker performs cache-first for static bundles, stale-while-revalidate for `/api/v1/status` and returns `offline.html` for navigation errors. Install prompts are surfaced through `beforeinstallprompt`.

### HTTP server

`kolibri serve` exposes:

- Static SPA with history fallback
- REST API described above
- SSE stream (`/api/v1/chain/stream`)
- Optional `--cors-dev` flag for permissive local testing

### Dataset & configs

- `datasets/demo.csv` – simple regression demo set
- `configs/kolibri.json` – runtime defaults (run ID, lambda, fmt, seeds)
- `configs/fractal_map.default.json` – FA-10 transform coefficients

### Tests

`make test` compiles and executes:

- `tests/test_payload.c` – ensures canonical log format
- `tests/test_fa.c` – validates FA-10 encoding/stability
- `tests/test_verify_break.c` – ensures matching configs verify successfully and that HMAC/byte tampering breaks the chain

## Security & determinism notes

- Locale is forced to `C` for serialization.
- Numbers are printed with `%.17g`.
- `kolibri_verify_file` recomputes SHA256 and optional HMAC-SHA256 (key provided via config).
- Payload evolution must append fields at the tail of the canonical order.

## Licensing

Kolibri is provided for internal prototyping and evaluation.
