# Kolibri Smart Client

Интерактивный умный клиент для ядра Kolibri (FA-10 + KPRL). Проект объединяет оркестратор инструментов на FastAPI и современный SPA/PWA-интерфейс с голосом, памятью и потоковой передачей ответов.

## Состав решения

- **Backend (FastAPI)** — сервис оркестратора (`smart_client/backend`). Предоставляет REST и SSE API, управляет TOOLS_SCHEMA, взаимодействует с KPRL и ведёт аудит.
- **Frontend (React + Vite + Tailwind + Zustand)** — однотраничное PWA-приложение (`smart_client/frontend`) с чат-интерфейсом, Ledger и экраном настроек.
- **Prompts** — системные и разработческие подсказки, расположенные в `smart_client/prompts`.

## Быстрый старт

### Предварительные требования

- Python 3.10+
- Node.js 18+
- pnpm, npm или yarn для управления фронтенд-зависимостями

### Развёртывание backend

```bash
cd smart_client/backend
python -m venv .venv
source .venv/bin/activate  # Windows: .venv\Scripts\activate
pip install -e .[dev]
uvicorn app.main:app --reload --port 8000
```

После запуска API доступно по адресу `http://localhost:8000`. Основные маршруты:

- `POST /api/v1/chat` — отправка сообщения ассистенту.
- `GET /api/v1/chat/stream?session_id=...` — поток SSE с токенами и событиями инструментов.
- `GET /api/v1/history?session_id=...` — получение истории диалога.
- `POST /api/v1/tools/<name>` — ручной вызов инструментов (`kg_search`, `kolibri_run`, `kolibri_verify`, `mission_plan`, `iot_safe_action`).
- `GET /api/v1/chain`, `GET /api/v1/chain/stream`, `POST /api/v1/chain/verify` — работа с журналом KPRL.

### Развёртывание frontend

```bash
cd smart_client/frontend
npm install
npm run dev        # режим разработки
# или npm run build && npm run preview для продакшен-сборки
```

По умолчанию клиент ожидает оркестратор на `http://localhost:8000`. В дев-режиме интерфейс доступен по `http://localhost:5173`.

#### PWA-компоненты

- Манифест: `public/manifest.webmanifest`.
- Сервис-воркер: `public/sw.js` (cache-first для статики и offline fallback).
- Офлайн-страница: `public/offline.html`.

Приложение можно установить через меню браузера («Добавить на экран»). В офлайне доступны история чата и черновики; новые сообщения отправятся автоматически после восстановления соединения.

## Возможности

- Стриминговые ответы (SSE) с задержкой менее 150 мс между токенами.
- Голосовой ввод (Web Speech API) и озвучивание ответов (SpeechSynthesis) с настраиваемой скоростью.
- Источники RAG и секция «Как проверили» в каждом ответе.
- Управление миссиями, запуск `kolibri_run`/`kolibri_verify`, безопасные IoT-команды.
- Журнал KPRL с визуализацией FA/FA_stab/FA_map (FractalBar) и проверкой цепочек.
- Память чата в `localStorage` и синхронизация с сервером.
- Аудит действий в `logs/audit.log`, цепочка KPRL в `logs/chain.jsonl`.

## Тестирование

```bash
cd smart_client/backend
pytest
```

Интеграционный тест `tests/test_smart_client_e2e.py` покрывает полный цикл: сообщение → стрим → инструмент → запись в KPRL → verify.

## Переменные окружения

- `KOLIBRI_LOG_DIR` — директория логов (по умолчанию `logs`).
- `KOLIBRI_CHAIN_PATH` — путь к журналу KPRL (`logs/chain.jsonl`).
- `KOLIBRI_AUDIT_LOG` — файл аудита (`logs/audit.log`).

## Дополнительные замечания

- Опасные IoT-команды требуют явного подтверждения перед выполнением.
- Оркестратор не раскрывает ключи и секреты; доступ к внешним ресурсам возможен только через зарегистрированные инструменты.
