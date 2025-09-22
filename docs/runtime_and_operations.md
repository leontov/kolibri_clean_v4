# Эксплуатация и рантайм

> Ведущий: Кочуров Владислав Евгеньевич. Любые изменения рантайма,
> конфигураций или процедур сопровождения фиксируйте здесь.

## Подготовка окружения

1. Соберите бэкенд (`make bin/kolibri_run` или `./build_macos.sh bin/kolibri_run`).
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
  "seed": 1337,
  "node_id": "kolibri-node-a",
  "sync_enabled": 1,
  "sync_listen_port": 9090,
  "sync_peers": ["127.0.0.1:9091"],
  "sync_trust_ratio": 0.5,
  "edge_mode": "hybrid",
  "edge_sync_interval": 45,
  "offline_queue_limit": 25,
  "offline_replay_batch": 5,
  "iot_bridge": {
    "max_batch_size": 5,
    "max_deferred_actions": 25,
    "sensor_signal_prefix": "iot"
  }
}
```

- Увеличение `steps` и `depth_max` повышает качество поиска, но увеличивает
  время выполнения.
- `quorum` и `eff_threshold` защищают от слабых кандидатов.
- `seed` обеспечивает воспроизводимость между запусками.
- Блок `iot_bridge` используется при инициализации `IoTBridge`: параметры
  ограничивают размер пакетных команд и глубину очереди отложенных действий.
- `edge_mode` переключает поведение рантайма между локальным, офлайн и гибридным
  режимом. Параметры `edge_sync_interval`, `offline_queue_limit` и
  `offline_replay_batch` управляют периодичностью синхронизации и объёмом
  пакетной репликации после восстановления связи.

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
response = runtime.process(
    RuntimeRequest(user_id="demo", goal="echo", modalities={"text": "hello"})
)
print(response.answer["summary"])
```

- Конфигурация runtime описана в [architecture](architecture.md).
- Все внешние навыки должны регистрироваться в `SkillStore` или напрямую в
  `SkillSandbox` (для тестов). Перед первым запросом выдавайте пользователю
  разрешения на необходимые модальности через `runtime.privacy.grant`.

### Консольный чат

1. Активируйте Python-окружение с установленным пакетом `kolibri_x`
   (например, `pip install -e .`).
2. Запустите CLI, указав идентификатор пользователя, которому нужно выдать
   доступ к текстовой модальности:

   ```bash
   python -m kolibri_x.cli.chat --user-id demo
   ```

3. При необходимости передайте аргумент `--knowledge`, чтобы сразу загрузить
   документы в граф знаний до начала диалога. CLI принимает как путь к файлу,
   так и директорию:

   ```bash
   python -m kolibri_x.cli.chat --user-id demo --knowledge docs/primer.txt
   ```

4. После запуска введите сообщение — runtime сформирует план, выполнит
   sandbox-навык по умолчанию и покажет суммарный ответ. Доступны специальные
   команды:

   - `:journal` — вывести последние события `ActionJournal`.
   - `:reason` — распечатать `ReasoningLog` текущего ответа в формате JSON.
   - `:quit` — завершить сессию.

   Все команды работают в одном цикле, поэтому можно чередовать пользовательские
   сообщения и отладочные запросы.

По умолчанию CLI регистрирует sandbox-навык `chat_responder` без дополнительных
разрешений. При необходимости замените обработчик в `kolibri_x/cli/chat.py`,
чтобы перенаправлять запросы в собственные навыки или внешние сервисы.

## Офлайн-режим

- `OfflineCache` хранит запросы и планы на диске. При восстановлении сети
  запускается синхронизация с KG и журналом.
- При разработке офлайн-логики логируйте операции через `ActionJournal` и
  обновляйте описание в этом документе.
- IoT-команды, выполненные в офлайне, теперь складываются в очередь
  `IoTBridge`. При восстановлении соединения используйте
  `IoTBridge.merge_after_offline(...)`, чтобы объединить локальные накопленные
  действия с уже отложенными командами. Все события (включая `iot_offline_merge`
  и `iot_sensor_sync`) фиксируются в `ActionJournal`, что позволяет отследить
  порядок применения и репликации.
- Для устройств, которые отдают телеметрию, мост автоматически повторяет
  выполненные действия в `SensorHub`. Сигналы получают префикс из поля
  `sensor_signal_prefix` конфигурации, после чего могут быть использованы в
  `TemporalAlignmentEngine`.

## Оповещения и SLA

- По умолчанию платформа работает локально. Подключение телеметрии должно
  документироваться отдельно.
- SLA измеряются метриками mKSI и успешностью верификации журналов. См.
  [roadmap](roadmap.md) для целевых значений.

