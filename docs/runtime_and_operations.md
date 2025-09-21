# Эксплуатация и рантайм

> Ведущий: Кочуров Владислав Евгеньевич. Любые изменения рантайма,
> конфигураций или процедур сопровождения фиксируйте здесь.

## Подготовка окружения

1. Соберите бэкенд (`make` или `./build_macos.sh`).
2. Убедитесь, что переменная `KOLIBRI_HMAC_KEY` установлена, если требуются
   HMAC-подписи. Без неё журнал содержит только `hash`.
3. Очистите предыдущие логи командой `make clean` (по необходимости).

## Конфигурация

Основной файл — `configs/kolibri.json`:

```json
{
  "steps": 30,
  "depth_max": 2,
  "depth_decay": 0.7,
  "quorum": 0.6,
  "temperature": 0.15,
  "eff_threshold": 0.8,
  "max_complexity": 32.0,
  "seed": 987654321
}
```

- Увеличение `steps` и `depth_max` повышает качество поиска, но увеличивает
  время выполнения.
- `quorum` и `eff_threshold` защищают от слабых кандидатов.
- `seed` обеспечивает воспроизводимость между запусками.

## Запуск сессии

```bash
./bin/kolibri_run configs/kolibri.json
```

- В каталоге `logs/` появится `chain.jsonl`. Каждая строка — один шаг,
  сериализованный каноническим JSON (без пробелов, числа формата `%.17g`).
- При запуске с `KOLIBRI_HMAC_KEY` каждая запись содержит `hmac`.
- Логи дополняются, поэтому при повторном прогоне удалите файл или
  выполните `make clean`.

## Проверка и воспроизведение

```bash
./bin/kolibri_verify logs/chain.jsonl
./bin/kolibri_replay configs/kolibri.json logs/chain.jsonl
```

- `kolibri_verify` считывает записи, пересчитывает `hash`/`hmac` и сообщает о
  несоответствиях.
- `kolibri_replay` восстанавливает вычислительные шаги по журналу, полезно
  для отладки и регрессионных прогонов.

## Мониторинг и журналы

| Файл | Описание |
| --- | --- |
| `logs/chain.jsonl` | Основной журнал выполнения. |
| `logs/*.json` | Побочные артефакты (например, дампы планов), создаются при включённых отладочных флагах. |

В продакшн-сценариях храните журналы в неизменяемом хранилище и
используйте `kolibri_verify` как часть CI.

## Работа с KolibriRuntime

Для интеграции Python-компонентов используйте `KolibriRuntime`:

-```python
from kolibri_x.runtime.orchestrator import KolibriRuntime, RuntimeRequest
from kolibri_x.runtime.orchestrator import SkillSandbox

sandbox = SkillSandbox()
sandbox.register("echo", lambda payload: {"text": payload["goal"].upper()})

runtime = KolibriRuntime(sandbox=sandbox)
runtime.privacy.grant("demo", ["text"])
response = runtime.process(RuntimeRequest(user_id="demo", goal="echo", modalities={"text": "hello"}))
print(response.answer["summary"])
```

- Конфигурация runtime описана в [architecture](architecture.md).
- Все внешние навыки должны регистрироваться в `SkillStore` или напрямую в
  `SkillSandbox` (для тестов). Перед первым запросом выдавайте пользователю
  разрешения на необходимые модальности через `runtime.privacy.grant`.

### Консольный чат

- Для быстрой проверки региструйте sandbox-навыки через новый CLI:

  ```bash
  python -m kolibri_x.cli.chat --user-id demo
  ```

- Поддерживаются команды `:journal`, `:reason`, `:quit`. Команда `:journal`
  выводит последние события `ActionJournal`, `:reason` — текущий `ReasoningLog`.
  Перед запуском можно прогрузить знания в граф аргументом
  `--knowledge path/to/file_or_dir`.

## Офлайн-режим

- `OfflineCache` хранит запросы и планы на диске. При восстановлении сети
  запускается синхронизация с KG и журналом.
- При разработке офлайн-логики логируйте операции через `ActionJournal` и
  обновляйте описание в этом документе.

## Оповещения и SLA

- По умолчанию платформа работает локально. Подключение телеметрии должно
  документироваться отдельно.
- SLA измеряются метриками mKSI и успешностью верификации журналов. См.
  [roadmap](roadmap.md) для целевых значений.

