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
