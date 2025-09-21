
# Архитектура Kolibri-x vX и план поставки

## 1. Видение и руководящие принципы
- **Измеримый интеллект:** каждая возможность должна повышать мультимодальную метрику KSI (mKSI) Kolibri по обобщающей способности, экономности, автономности, надёжности, объяснимости и удобству.
- **Доверие и контроль человека:** приватность, происхождение данных и объяснимость — первоклассные требования, а не довесок.
- **Комбинируемое мышление:** мультимодальные навыки, знания и инструменты взаимодействуют через явные контракты и общий рантайм.
- **Устойчивость с приоритетом офлайн:** критичные потоки работают локально и синхронизируются оппортунистически, сохраняя контроль у пользователя.

## 2. Слоистая системная архитектура

```
┌────────────────────────────────────────────────────────────────┐
│                        Пользовательские поверхности            │
│  (приложения/веб/AR/CLI) → Планировщик задач → SkillStore →    │
│                          песочницы инструментов                │
└────────────────────────────────────────────────────────────────┘
           │ события/запросы                    │ ответы/логи
┌────────────────────────────────────────────────────────────────┐
│                 Слой оркестрации рантайма                      │
│  Менеджер сессий · Движок воркфлоу · Офлайн-кэш · Шина синхр.  │
└────────────────────────────────────────────────────────────────┘
           │ мультимодальные токены/логи       │ планы/контекст
┌────────────────────────────────────────────────────────────────┐
│                 Ядро мультимодального мышления                 │
│  Энкодеры · Фьюжн-трансформер · Нейро-семантический планировщик│
└────────────────────────────────────────────────────────────────┘
           │ рёбра сущность/событие            │ доказательства/запросы
┌────────────────────────────────────────────────────────────────┐
│             Нейро-семантическая память и поиск                 │
│  Локальный граф знаний · RAG-пайплайны · Проверка фактов       │
└────────────────────────────────────────────────────────────────┘
           │ сигналы персонализации            │ политики/ключи
┌────────────────────────────────────────────────────────────────┐
│        Приватность, персонализация и управление                │
│  Профайлер · Оператор приватности · Фед. обучение · Аудит-лог  │
└────────────────────────────────────────────────────────────────┘
           │ телеметрия (опционально)            │ метрики/отчёт
┌────────────────────────────────────────────────────────────────┐
│        Наблюдаемость, XAI и безопасность                       │
│  XAI-панели · Контент-фильтры · Этические гарды · mKSI-оценка  │
└────────────────────────────────────────────────────────────────┘
```

### 2.1. Ядро мультимодального мышления
- **Энкодеры:** модульные энкодеры текста, речи (ASR), аудио, изображения/видео (кадры) и сенсорных потоков с общим эмбеддинг-пространством.
- **Фьюжн-трансформер:** модель перекрёстного внимания, объединяющая токенизированные события/кадры в единый контекст для рассуждений и планирования инструментов.
- **Нейро-семантический планировщик:** переводит намерения в упорядоченные вызовы навыков с критериями успеха и требуемыми доказательствами.
- **Стриминговый интерфейс:** поддерживает инкрементальное поглощение (например, живую транскрибацию) и частичные обновления планов.

### 2.2. Нейро-семантическая память и извлечение
- **Граф знаний (KG):** локальный property-граф, в котором хранятся сущности, события, утверждения, источники и задачи. Хранилище на JSONL с индексами по идентификаторам узлов и временным фасетам.
- **RAG-контур:** планировщик формирует семантические запросы → извлечение из KG → подбор документов/фактов → компоновка промпта для ядра → проверенный ответ.
- **Пайплайн достоверности:** ранжирование источников (доверие, актуальность), детекция противоречий, внедрение ссылок в выводы планировщика и отказ от ответа при уверенности ниже порога.

### 2.3. Приватность, персонализация и управление
- **Профайлер на устройстве:** изучает предпочтения пользователя (стиль, тон, паттерны задач) через федеративное обучение с безопасной агрегацией и дифференциальной приватностью.
- **Эмпатический слой:** создаёт вектора модуляции (тон, темп, формальность), применяемые при декодировании ответа.
- **Оператор приватности:** применяет политики согласий к типам данных, управляет офлайн-режимом, ключами шифрования и пользовательскими переключателями.
- **Журнал действий:** хеш-цепочка Меркла, фиксирующая мультимодальные входы, планы, вызовы навыков и ссылки на доказательства для аудита.

### 2.4. Экосистема навыков и инструментов
- **SkillStore:** декларативные манифесты (`skill.json`), разрешения на основе возможностей, изолированное исполнение (контейнер или WASI), биллинг-хуки и процесс ревью.
- **Планировщик задач/воркфлоу:** поддержка долгих проектов, дедлайнов, напоминаний и отслеживания зависимостей с механизмами отката.
- **Специализированные наборы навыков:** доменные модули (код-ревью, UI-дизайн, оркестрация IoT, юр-анализ, STEM-симуляции), распространяемые через SkillStore.

### 2.5. Наблюдаемость, объяснимость и безопасность
- **XAI-консоль:** визуальная цепочка рассуждений, оценки уверенности, ссылки на источники, альтернативные гипотезы и сравнение версий плана.
- **Контент- и этические фильтры:** настраиваемый движок политик, включая стоп-классы NSFW/биометрии и «детский режим».
- **Оценочный стенд:** дашборды mKSI, миссии (STEM, код, юридические обзоры, аудио-встречи, визуальные задачи), регрессионные наборы и синтетическая лаборатория.

### 2.6. Оркестрация рантайма и пользовательские поверхности
- **Рантайм-ядро:** координирует контекст сессий, управляет асинхронными вызовами навыков и сохраняет состояние в офлайн-кэше.
- **Движок синхронизации:** оппортунистическая синхронизация с разрешением конфликтов и версионированием для KG и журналов.
- **Клиентские поверхности:** веб-дэшборд, AR-оверлей, CLI и API-шлюз используют общие контракты оркестрации.

## 3. Обзор потоков данных
1. **Взаимодействие с пользователем:** слой опыта захватывает мультимодальные входы (текст, голос, изображение) и метаданные, сразу применяя политики приватности.
2. **Кодирование:** входы проходят через модальные энкодеры, создающие согласованные токены для фьюжн-трансформера.
3. **Планирование:** ядро находит намерения, обращается к KG через RAG, а нейро-семантический планировщик строит граф навыков и задач.
4. **Исполнение:** рантайм отправляет навыки в песочницы SkillStore. Инструменты выдают результаты и доказательства, которые добавляются в журнал действий.
5. **Верификация:** выходы проверяются по KG и пайплайну достоверности. Несогласованности запускают циклы уточнения или отказ.
6. **Персонализация:** эмпатический слой корректирует ответы; профайлер обновляет модели через федеративные градиенты при наличии подключения.
7. **Объяснимость и логирование:** цепочка рассуждений, уверенность и цитаты отправляются в XAI-панель; подписанные записи журнала хранятся локально и при необходимости синхронизируются.

## 4. Дорожная карта поставки (MVP на 12 недель)

### Этап A — Фундамент (недели 1–4)
- **Мультимодальная база:** выпустить текстовый и ASR-энкодеры плюс энкодер изображений/видео по ключевым кадрам. Интегрировать с прототипом фьюжн-трансформера.
- **KG v1:** локальный JSONL property-граф с индексами, CRUD-API и временными/версионными фасетами.
- **RAG с проверкой:** реализовать пайплайн извлечения, ранжирование источников и проверки согласованности с режимом «нет ответа» при низкой уверенности.
- **SkillStore v1:** валидация манифестов, песочница, токены доступа и базовая инструментализация биллинга.
- **Прозрачность рассуждений:** структурированный лог цепочки мышления со ссылками на источники и простая веб-визуализация.
- **Основы приватности:** управление согласиями, офлайн-кэш и базовое шифрование сохранённых логов.

### Этап B — Интеллект и персонализация (недели 5–8)
- **Профайлер + федеративное обучение:** пайплайн агрегации градиентов с безопасным сервером и добавлением DP-шума.
- **Эмпатическая модуляция:** векторы стиля/темпа, влияющие на декодирование ответов; настройки по пользователям.
- **Контур активного обучения:** модель просит пользователя предоставить уточняющие данные/ярлыки; очередь задач для аннотаций.
- **Планировщик задач/воркфлоу:** поддержка многошаговых проектов, дедлайнов, напоминаний и сохранения состояния.

### Этап C — Качество и UX (недели 9–12)
- **XAI-панель:** интерактивная визуализация с регуляторами уверенности, сравнением доказательств и альтернативных планов.
- **Миссии для оценки:** пакеты миссий для STEM, кодинга, юридических обзоров, аудио-встреч и работы с изображениями. Автоматические отчёты mKSI.
- **IoT-мост (альфа):** защищённый канал команд с политиками возможностей, интеграцией с журналом действий и механизмом отката.

## 5. API-контракты (объём MVP)
- **Манифест навыка (`skill.json`):** описывает входы, разрешения, биллинг и точки входа.
- **Единица факта в графе знаний:** фиксирует текст утверждения, источники, связи поддержки/противоречия, уверенность и временные метки.
- **Схема планировщика задач:** определяет цели, последовательность инструментов, дедлайны и расписание напоминаний.
- **Формат записи журнала:** канонический JSON с детерминированной подписью (`hash`, `hmac`), совместимый с существующей цепочкой логов Kolibri.

## 6. Защита и приватность
- **Песочницы:** каждый навык выполняется в контейнере/WASI с ограничениями по сети и файловой системе.
- **Политики данных:** явные согласия по каждой модальности и классу хранения; офлайн-режим блокирует внешние передачи.
- **Безопасность фед. обучения:** безопасная агрегация, бюджеты дифф-приватности и аттестация клиентов.
- **Журнал с подписью Меркла:** расширяет цепочку Kolibri v4 на мультимодальные события и позволяет внешнюю верификацию.
- **Мультимодальные гарды:** детекторы NSFW/биометрии, пакет «child-safe» и немедленные стоп-экшены.

## 7. Метрики и оценка (mKSI)
- **Обобщение (G):** точность на задачах разных доменов и модальностей.
- **Экономность (P):** отношение минимального использования навыков/инструментов к качеству результата.
- **Автономность (A):** улучшение, достигнутое благодаря вмешательствам активного обучения.
- **Надёжность (R):** доля успешных повторных запусков, включая офлайн-режим.
- **Объяснимость (E):** полнота и точность ссылок и визуализаций рассуждений.
- **Удобство (U):** время выполнения задач, количество ошибок UX и удовлетворённость пользователей.
- **Цель:** mKSI ≥ 0,75 на миссиях по коду, документам, аудио-встречам и изображениям.

## 8. Структура репозитория (каркас)
```
kolibri-x/
  core/         # Фьюжн-трансформер, планировщики, энкодеры
  kg/           # Хранилище графа знаний, извлечение, верификация
  skills/       # SDK навыков, манифесты, инструменты песочницы
  privacy/      # Оператор приватности, менеджер согласий, профайлер
  xai/          # Визуализация рассуждений, API объяснимости
  runtime/      # Оркестрационное ядро, офлайн-кэш, движок синхронизации
  apps/         # Пользовательские поверхности (web, AR, CLI)
  eval/         # Миссии, mKSI-оценщики, синтетическая лаборатория
```

## 9. Управление рисками и операционные плейбуки
- **Контроль галлюцинаций:** обязательная верификация доказательств, политика «лучше без ответа», запросы уточнений при низкой уверенности.
- **Гарантии приватности:** локальная обработка по умолчанию, минимальный перенос данных и прозрачные журналы согласий.
- **Управление сложностью:** модульные интерфейсы, версионируемые контракты и непрерывные миссии интеграции по слоям.
- **Настройка производительности:** инкрементальные обновления KG, потоковый ASR, квантизация моделей и ускорение на периферии.

## 10. Следующие шаги
1. Создать архитектурную гильдию с лидами по слоям (ядро, KG, навыки, приватность, XAI, рантайм).
2. Определить бэклог эпиков этапа A с критериями приёмки и прогнозом влияния на mKSI.
3. Развернуть оценочный стенд с базовыми метриками для еженедельного мониторинга улучшений.
4. Согласовать проверку безопасности песочниц, подписи журналов и фед-обучения до пользовательских испытаний.

## 11. Прогресс реализации

### Спринт A — фундамент реализован
- **Каркас мультимодального ядра:** добавлены детерминированные энкодеры текста, аудио и изображений плюс заготовка фьюжн-трансформера, чтобы разблокировать последующие пайплайны.
- **Нейро-семантический планировщик:** реализован облегчённый планировщик, который сопоставляет цели пользователя с доступными манифестами SkillStore и строит планы с учётом зависимостей.
- **Граф знаний + RAG-контур:** поставлен локальный KG на JSONL с детекцией конфликтов и извлечением, подкладывающим подтверждённые источниками доказательства.
- **Приватность и гарды рантайма:** начальный оператор приватности применяет политики согласий, а офлайн-кэш гарантирует детерминированное вытеснение для локального режима.
- **Контракты SkillStore:** загрузчик манифестов и проверки разрешений позволяют ранним партнёрским навыкам интегрироваться с оркестратором.
- **Прозрачность рассуждений:** логи рассуждений фиксируют шаги извлечения и проверки для будущей XAI-консоли.
=======
# Kolibri-x Architecture vX and Delivery Plan

## 1. Vision and Guiding Principles
- **Measurable intelligence:** Every capability must improve Kolibri's multimodal KSI (mKSI) scores for generalization, parsimony, autonomy, reliability, explainability, and usability.
- **Human trust and control:** Privacy, provenance, and explainability are first-class requirements, not afterthoughts.
- **Composable cognition:** Multimodal skills, knowledge, and tooling interoperate through explicit contracts and shared runtimes.
- **Offline-first resilience:** All critical flows function locally, syncing opportunistically to preserve user control.

## 2. Layered System Architecture

```
┌────────────────────────────────────────────────────────────────┐
│                        Experience Surfaces                      │
│  (apps/web/AR/CLI) → Task Planner → SkillStore → Tool Sandboxes │
└────────────────────────────────────────────────────────────────┘
           │ events/requests                     │ responses/logs
┌────────────────────────────────────────────────────────────────┐
│                   Runtime Orchestration Layer                   │
│  Session manager · Workflow engine · Offline cache · Sync bus   │
└────────────────────────────────────────────────────────────────┘
           │ multimodal tokens/logs               │ plans/context
┌────────────────────────────────────────────────────────────────┐
│                    Multimodal Cognition Core                    │
│  Encoders · Fusion Transformer · Neuro-semantic planner        │
└────────────────────────────────────────────────────────────────┘
           │ entity/event edges                   │ evidence/query
┌────────────────────────────────────────────────────────────────┐
│                  Neuro-semantic Memory & Retrieval              │
│  Local knowledge graph · RAG pipelines · Fact verification      │
└────────────────────────────────────────────────────────────────┘
           │ personalization signals              │ policies/keys
┌────────────────────────────────────────────────────────────────┐
│                Privacy, Personalization, and Governance         │
│  Profiler · Privacy operator · Federated learning · Audit log   │
└────────────────────────────────────────────────────────────────┘
           │ telemetry (opt-in)                    │ metrics/report
┌────────────────────────────────────────────────────────────────┐
│                Observability, XAI, and Safety Layer             │
│  XAI panels · Content filters · Ethical guardrails · mKSI eval  │
└────────────────────────────────────────────────────────────────┘
```

### 2.1 Multimodal Cognition Core
- **Encoders:** Modular encoders for text, speech (ASR), audio, image/video frames, and sensor streams with shared embedding space.
- **Fusion transformer:** Cross-attention model fusing tokenized events/frames into a unified context for reasoning and tool planning.
- **Neuro-semantic planner:** Translates intents into ordered skill invocations with success criteria and required evidentiary support.
- **Streaming interface:** Supports incremental ingestion (e.g., live transcription) and partial plan updates.

### 2.2 Neuro-semantic Memory & Retrieval
- **Knowledge graph (KG):** Local property graph storing Entities, Events, Claims, Sources, and Tasks. JSONL-backed storage with index on node IDs and temporal facets.
- **RAG loop:** Planner issues semantic queries → KG retrieval → supporting documents/facts → prompt composer for the core → response validated against KG.
- **Veracity pipeline:** Source ranking (trust scores, freshness), contradiction detection, reference injection into planner outputs, and "abstain" fallback when confidence < threshold.

### 2.3 Privacy, Personalization, and Governance
- **On-device profiler:** Learns user preferences (style, tone, task patterns) using federated learning with secure aggregation and differential privacy.
- **Empathy layer:** Generates modulation vectors (tone, tempo, formality) applied to response decoding.
- **Privacy operator:** Enforces consent policies on data types, governs offline mode, manages encryption keys, and exposes user-facing toggles.
- **Action journal:** Hash-chained Merkle log capturing multimodal inputs, plans, skill calls, and evidence links for auditability.

### 2.4 Skills and Tooling Ecosystem
- **SkillStore:** Declarative manifests (`skill.json`), capability-based permissions, sandboxed execution (container or WASI), billing hooks, and review workflow.
- **Task/workflow planner:** Supports long-lived projects, deadlines, reminders, and dependency tracking with rollback hooks.
- **Specialized skill packs:** Domain modules (code review, UI design, IoT orchestration, legal analysis, STEM simulations) delivered via SkillStore.

### 2.5 Observability, Explainability, and Safety
- **XAI console:** Visual reasoning chain, confidence scores, source links, alternative hypotheses, and diff view of plan revisions.
- **Content and ethics filters:** Configurable policy engine including NSFW/biometric stops and "child-safe" mode.
- **Evaluation harness:** mKSI dashboards, mission packs (STEM, code, legal briefs, audio meetings, visual tasks), regression suites, and synthetic lab.

### 2.6 Runtime Orchestration and Experience Surfaces
- **Runtime kernel:** Coordinates session context, manages asynchronous skill invocations, and persists state to offline cache.
- **Sync engine:** Opportunistic synchronization with conflict resolution and versioning for KG and journals.
- **Client surfaces:** Web dashboard, AR overlay, CLI, and API gateway share the same orchestration contracts.

## 3. Data Flow Overview
1. **User interaction:** Experience layer captures multimodal inputs (text, voice, image) and metadata, applying privacy policies immediately.
2. **Encoding:** Inputs flow through modality-specific encoders generating aligned tokens for the fusion transformer.
3. **Planning:** Fusion core infers intents, consults KG via RAG, and the neuro-semantic planner produces a skill/task graph.
4. **Execution:** Runtime dispatches skills through SkillStore sandbox. Tools emit outputs and evidence, appended to the action journal.
5. **Verification:** Outputs are checked against KG and veracity pipeline. Discrepancies trigger clarification loops or abstention.
6. **Personalization:** Empathy layer adjusts responses; profiler updates models using federated gradients when connectivity permits.
7. **Explainability & logging:** Reasoning chain, confidence, and citations propagated to XAI panel; signed journal entries stored locally and optionally synced.

## 4. Delivery Roadmap (12-week MVP)

### Stage A — Foundation (Weeks 1–4)
- **Multimodal base:** Ship text + ASR encoders and keyframe image/video encoder. Integrate with fusion transformer prototype.
- **KG v1:** Local JSONL property-graph with indexing, CRUD APIs, and time/version facets.
- **RAG with verification:** Implement retrieval pipeline, source ranking, and consistency checks with "no answer" fallback.
- **SkillStore v1:** Manifest validation, sandbox runner, access tokens, and minimal billing instrumentation.
- **Reasoning transparency:** Structured chain-of-thought log with source references and simple web visualization.
- **Privacy foundations:** Consent management UI, offline cache, and baseline encryption for stored logs.

### Stage B — Intelligence & Personalization (Weeks 5–8)
- **On-device profiler + FL:** Gradient aggregation pipeline with secure aggregation server and DP noise.
- **Empathy modulation:** Style/tempo vectors influencing response decoding; configurable per user.
- **Active learning loop:** Model prompts users for clarifying data/labels; task queue for annotation.
- **Task/workflow planner:** Supports multi-step projects, deadlines, reminders, and state persistence.

### Stage C — Quality & UX (Weeks 9–12)
- **XAI panel:** Interactive visualization with confidence sliders, evidence diff, and alternative plan comparison.
- **Evaluation missions:** Mission packs for STEM, coding, legal briefs, audio meetings, and imagery. Automated mKSI reporting.
- **IoT bridge (alpha):** Secure command channel with capability policies, action journal integration, and rollback.

## 5. API Contracts (MVP Scope)
- **Skill manifest (`skill.json`):** Declares inputs, permissions, billing, and entrypoints.
- **Knowledge graph fact unit:** Captures claim metadata, sources, support/contradiction links, confidence, and timestamps.
- **Task planner schema:** Defines goals, tool sequences, deadlines, and reminder schedules.
- **Journal entry format:** Canonical JSON with deterministic signing (`hash`, `hmac`) compatible with existing Kolibri logging pipeline.

## 6. Security and Privacy Safeguards
- **Sandboxing:** Each skill runs in capability-restricted container/WASI with network and filesystem guards.
- **Data policies:** Explicit consent per modality and storage class; offline mode locks external transmission.
- **Federated learning safety:** Secure aggregation, differential privacy budgets, and attested clients.
- **Merkle-signed journal:** Extends Kolibri v4 chain to multimodal events; facilitates external verification.
- **Multimodal guards:** NSFW/biometric detectors, child-safe policy pack, and real-time stop actions.

## 7. Metrics and Evaluation (mKSI)
- **Generalization (G):** Cross-domain/task accuracy across modalities.
- **Parsimony (P):** Ratio of minimal skill/tool usage versus outcome quality.
- **Autonomy (A):** Improvement attributable to active learning interventions.
- **Reliability (R):** Success rate under repeat runs and offline-only constraints.
- **Explainability (E):** Coverage and accuracy of citations and reasoning visuals.
- **Usability (U):** Task completion time, user error rates, and satisfaction metrics.
- **Target:** mKSI ≥ 0.75 across code, document, audio meeting, and image mission suites.

## 8. Repository Layout (Skeletal)
```
kolibri-x/
  core/         # Fusion transformer, planners, encoders
  kg/           # Knowledge graph storage, retrieval, verification
  skills/       # Skill SDK, manifests, sandbox tooling
  privacy/      # Privacy operator, consent manager, profiler
  xai/          # Reasoning visualization, explainability APIs
  runtime/      # Orchestration kernel, offline cache, sync engine
  apps/         # Experience surfaces (web, AR, CLI)
  eval/         # Missions, mKSI evaluators, synthetic lab
```

## 9. Risk Mitigation & Operational Playbooks
- **Hallucination control:** Mandatory evidence verification, abstain-first policy, and user prompts for clarification when confidence is low.
- **Privacy guarantees:** Default to local processing, minimal data transfer, and transparent consent logs.
- **Complexity management:** Modular interfaces, versioned contracts, and continuous integration missions per layer.
- **Performance tuning:** Incremental KG updates, streaming ASR, model quantization, and edge acceleration options.

## 10. Next Steps
1. Stand up architecture guild with leads per layer (core, KG, skills, privacy, XAI, runtime).
2. Define backlog of Stage A epics with acceptance tests and mKSI impact projections.
3. Bootstrap evaluation harness with baseline metrics to measure week-over-week improvements.
4. Align security review on sandboxing, journal signing, and federated learning rollout prior to user trials.

## 9. Implementation Progress (MVP Sprint A)
- **Multimodal core scaffolding:** Added deterministic text, audio, and image encoders with a fusion transformer placeholder to unblock downstream pipelines.
- **Neuro-semantic planner:** Implemented lightweight planner that aligns user goals with available SkillStore manifests and produces dependency-aware plans.
- **Knowledge graph + RAG loop:** Delivered local JSONL-backed KG with conflict detection plus retrieval-augmented answering that injects source-backed evidence.
- **Privacy and runtime guards:** Initial privacy operator enforces consent policies and the offline cache guarantees deterministic eviction for on-device use.
- **SkillStore contracts:** Manifest loader and permission checks enable early partner skills to integrate with the orchestration layer.
- **Reasoning transparency:** Reasoning logs capture retrieval and verification steps for the forthcoming XAI console.
- **CLI harness:** `kolibri_x.apps.cli` wires the components together so teams can experiment with queries against a local KG snapshot.


### 9.1 Sprint A Mini Status Report (Week 2)
- **Roadmap alignment:** Stage A backlog stories for KG v1, RAG verification, and SkillStore manifests are implemented in code and exercised through the CLI harness, covering ~60% of the foundation milestone.
- **In-flight work:** Multimodal fusion (cross-modal attention prototype) and reasoning visualization are in active development with interface contracts finalized and test stubs prepared.
- **Upcoming focus:** Finalize ASR streaming ingestion, extend privacy operator with consent override auditing, and prepare evaluation seeds for the Stage B profiler and empathy layer.
- **Risks/mitigations:**
  - *Fusion readiness:* mitigate by pairing the planner with deterministic fixtures so downstream teams can integrate before transformer fine-tuning completes.
  - *Verification load:* build lightweight caching for KG lookups to keep RAG latency within target budgets while datasets grow.
- **mKSI outlook:** Early dry-runs on the CLI harness indicate explainability coverage via reasoning logs and KG citations, setting a baseline E-score of 0.62 with plans to lift it above 0.7 once visualization and ASR evidence links land.



### Stage B — Implementation Progress (Weeks 5–6)
- **Personalization core:** Added an on-device profiler with federated aggregation primitives and an empathy modulator that translates behavioural signals into tone/tempo adjustments.
- **Active learning loop:** Introduced a deterministic uncertainty scorer and annotation request planner to focus human labeling on low-confidence, low-coverage domains.
- **Workflow planner:** Delivered runtime support for long-lived projects with progress tracking, reminders, and overdue detection in preparation for mission-scale orchestration.


