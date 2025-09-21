# Kolibri Clean v4 — deterministic JSON-signing

**Что важно:** `hash` и `hmac` считаются ровно по тем же байтам, что и пишутся в лог (канонический JSON без пробелов, `%.17g`), а `verify` использует тот же сериализатор. Поэтому верификация **всегда** совпадает.

## macOS
```bash
./build_macos.sh
# без ключа:
unset KOLIBRI_HMAC_KEY
./bin/kolibri_run configs/kolibri.json
./bin/kolibri_verify logs/chain.jsonl
# с ключом:
# export KOLIBRI_HMAC_KEY="secret"
# ./bin/kolibri_run configs/kolibri.json && ./bin/kolibri_verify logs/chain.jsonl
```

## Ubuntu
```bash
sudo apt-get update && sudo apt-get install -y build-essential libssl-dev pkg-config
make -j"$(nproc)"
```
