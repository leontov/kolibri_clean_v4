# Kolibri Smart Client

Интерактивный умный клиент для ядра Kolibri (FA-10 + KPRL): включает оркестратор-интерфейс инструментов и SPA/PWA с голосом, памятью и стримингом.

## Архитектура

- **Backend (FastAPI)** — сервис оркестратора (`smart_client/backend`). Реализует REST/SSE API, инструменты из TOOLS_SCHEMA, ведёт KPRL и аудит.
- **Frontend (React + Vite + Tailwind + Zustand)** — однотраничное PWA-приложение (`smart_client/frontend`) с чат-интерфейсом, Ledger и настройками.
- **Prompts** — файлы системного и разработческого промпта в `smart_client/prompts`.

## Установка

### Требования

- Python 3.10+
- Node.js 18+
- pnpm/npm/yarn (для фронтенда)

### Backend

```bash
cd smart_client/backend
python -m venv .venv
source .venv/bin/activate  # Windows: .venv\Scripts\activate
pip install -e .[dev]
uvicorn app.main:app --reload --port 8000
```

API доступно по `http://localhost:8000`. Основные маршруты:

- `POST /api/v1/chat` — отправка сообщения.
- `GET /api/v1/chat/stream?session_id=...` — SSE поток токенов и событий инструментов.
- `GET /api/v1/history?session_id=...` — история диалога.
- `POST /api/v1/tools/<name>` — вызов инструментов (`kg_search`, `kolibri_run`, `kolibri_verify`, `mission_plan`, `iot_safe_action`).
- `GET /api/v1/chain`, `GET /api/v1/chain/stream`, `POST /api/v1/chain/verify` — работа с журналом KPRL.

### Frontend

```bash
cd smart_client/frontend
npm install
npm run dev # или npm run build && npm run preview
```

По умолчанию клиент ожидает оркестратор на `http://localhost:8000`. Запуск `npm run dev` поднимет интерфейс на `http://localhost:5173`.

#### PWA

- Манифест: `public/manifest.webmanifest`
- Сервис-воркер: `public/sw.js` (cache-first для статики, offline fallback).
- Офлайн-страница: `public/offline.html`

Установка через меню браузера («Добавить на экран»). В офлайне отображается история, черновики запросов, а новые сообщения отправятся при восстановлении связи.

## Функциональность

- Стриминговые ответы (SSE) с задержкой <150 мс между токенами.
- Голосовой ввод (Web Speech API) и TTS (SpeechSynthesis) с настраиваемой скоростью.
- Источники RAG и «Как проверили» в каждом ответе.
- Управление миссиями, запуск kolibri_run/verify, безопасные IoT-команды.
- Журнал KPRL с визуализацией FA/FA_stab/FA_map (FractalBar) и верификацией.
- Память чата в localStorage + синхронизация с сервером.
- Аудит действий в `logs/audit.log`, цепочка `logs/chain.jsonl`.

## Тестирование

```bash
cd smart_client/backend
pytest
```

E2E-тест `tests/test_smart_client_e2e.py` проверяет полный цикл: сообщение → стрим → инструмент → запись в KPRL → verify.

## Переменные окружения

- `KOLIBRI_LOG_DIR` — директория логов (по умолчанию `logs`).
- `KOLIBRI_CHAIN_PATH` — путь к журналу KPRL (`logs/chain.jsonl`).
- `KOLIBRI_AUDIT_LOG` — файл аудита (`logs/audit.log`).

## Примечания

- Вызовы инструментов требуют явного подтверждения для опасных IoT-команд.
- Оркестратор не раскрывает ключи или секреты; доступ к сетевым ресурсам возможен только через реализованные инструменты.
