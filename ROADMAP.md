# bpmnos-wasm — Roadmap

**Scope: 100% workbench-agnostic.** This repo compiles the C++ BPMN-OS engine
(`~/Code/bpmnos/engine`) to WebAssembly and ships a JavaScript API to **drive it interactively**. It
knows nothing about bpmn-js, diagram-js, playback, or animation logs. It takes a BPMN model + instance
data in, lets JS act as the engine's dispatcher (supplying clock ticks and decisions), and streams the
engine's own token/event/message log out. Any mapping into an animation/playback vocabulary happens
**downstream in `bpmnos-workbench`**, never here.

## Required capabilities (this is the contract)

1. **Input** — pass BPMN model XML, lookup CSV tables (a folder, or the tables directly), and
   `instance.csv` from JS into the engine.
2. **Log** — let JS observe the recorder log (or a dedicated bridge observer).
3. **Clock** — let JS create a `ClockTickEvent` and pass it to the engine.
4. **Decisions** — let JS create all four decision types and pass them to the engine.
5. **Run/resume** — let JS run and resume the engine.
6. **Concurrency-robust** — a decision request may expire, a subprocess may be interrupted, a
   sequential performer may be taken, a token withdrawn. None of this may corrupt state or crash the
   wasm module.

## How the engine actually works (verified against the source)

The engine is **single-threaded and synchronous** (`execution/engine/src/Engine.{h,cpp}`).

- **Drive:** `run(scenario, timeout)` for a fresh run; then `resume(timeout)`,
  `resume(shared_ptr<Event>, timeout)`, or `resume(shared_ptr<Decision>, timeout)` to continue. Each
  call runs the internal loop until the timeout is hit **or no dispatcher yields more work**, then
  returns. Interactive driving = call `resume(...)` again with the next event/decision.
- **JS is the dispatcher.** Normally a `GreedyController` + `TimeWarp` are connected to supply
  decisions and clock ticks. For JS-driven mode we **connect neither** (or connect them selectively):
  with no dispatcher, the engine advances all it can automatically, then returns — and JS injects the
  next `ClockTickEvent` or `Decision` via `resume`.
- **Events** (`execution/engine/src/events/`): engine-internal (`ReadyEvent`, `CompletionEvent`,
  `TimerEvent`, `ErrorEvent`, `TerminationEvent`) fire during advancement. JS creates the *external*
  inputs: `ClockTickEvent(const SystemState*)` (time) and the four decisions.
- **Decisions** (`execution/controller/src/decisions/`): `EntryDecision(token, evaluator)`,
  `ExitDecision(token, evaluator)`, `ChoiceDecision(token, choices, evaluator)`,
  `MessageDeliveryDecision(token, message, evaluator)`. Each multiply-inherits its matching `Event`,
  so `resume()` accepts it directly. JS may instead pass the plain `EntryEvent/ExitEvent/ChoiceEvent/
  MessageDeliveryEvent` to bypass evaluation, or build the `Decision` and call `evaluate()` first to
  get a feasibility/reward signal for the UI.
- **Decision requests:** when a token needs a decision it posts a `DecisionRequest` (Observable
  types `EntryRequest|ChoiceRequest|ExitRequest|MessageDeliveryRequest`) and lands in
  `systemState->pending{Entry,Choice,Exit,MessageDelivery}Decisions` — `auto_list<weak_ptr<Token>,
  weak_ptr<DecisionRequest>>` that **self-prune expired entries on traversal**.
- **Observation** (`execution/engine/src/Observer.h`): one method `notice(const Observable*)`.
  `Recorder` subscribes to `Token`/`Event`/`Message` and serialises each via `jsonify()`;
  `Recorder::inject(tag, json)` lets the bridge interleave its own entries.
- **Clock:** `ClockTickEvent` advances `SystemState::currentTime` and fires due timers. It is the
  loop's yield boundary and never advances past the `timeout` argument. `TimeWarp` = as-fast-as-
  possible sim time; `Metronome` = wall-clock paced. JS-driven mode supplies ticks itself.

### Built-in concurrency robustness (rely on it, expose it — do not reinvent it)

The engine already invalidates stale work; the wasm layer must **surface** this, not fight it:

- **The engine notifies withdrawal (a reconciliation aid, not a fix).** `Token::withdraw()` →
  `update(State::WITHDRAWN)` → `notify()` (`Token.cpp:1371`), and `WITHDRAWN` is a first-class token
  state (`Token.h:48/50`), so `jsonify()` emits `"state":"WITHDRAWN"` on the **same observable log**
  the consumer already reads; messages similarly get `Message::State::WITHDRAWN`. This does **not**
  solve the concurrency problem — it does not prevent a stale submission or resolve any boundary race
  on its own. What it does is make it *easy* for JS to reach a consistent view: a withdrawn
  token/decision is something the engine *pushes* rather than something JS must detect out of band, so
  JS reconciles its own model on the next snapshot drain. JS's view can be momentarily stale between
  `resume()` calls; the WITHDRAWN entries just let it converge with the engine soon after, from the
  log itself. The actual safety (no crash, no dangling access, no stale decision taking effect) still
  comes from the weak-ptr pruning + command guards below and handle validation in design decision 1.
- **Weak-ptr auto-pruning** (`execution/utility/src/{expired,auto_list,auto_set}.h`): pending and
  candidate collections drop entries whose token/`DecisionRequest` has expired — but the cleanup is
  **lazy and traversal-driven**: erasure happens only inside the iterator's `skipExpired()` (its
  constructor and `operator++`), so an entry is pruned only when an iterator actually passes over it.
  An early `break` leaves later expired entries in place; nothing prunes on insert/`reset`. Iterating
  **mutates** the (`mutable`, `const_cast`-ed) container, even via `begin() const`/`cbegin()`.
  `find()` matches via `lock()` so it never returns an expired entry, but does not clean the whole
  container. `auto_set` skips index 0 (the reward value) via `expired<1>`; `auto_list` checks from
  index 0.

  **Bridge rules that follow (design decisions 1–2 depend on these):**
  - **Never treat "present in the engine's pending list" as live.** A lingering, not-yet-traversed
    expired entry can still be in the list. The authoritative liveness check is the bridge's **own
    `weak_ptr.lock()`** in the handle registry — independent of the engine's cleanup timing.
  - **Registry stores `weak_ptr` only, never `shared_ptr`.** A `shared_ptr` would pin a
    `DecisionRequest`/`Token` alive and defeat the engine's `reset`/withdraw/renew expiry — breaking
    the very mechanism relied on. Obtain handles from `token->weak_from_this()` and the pending-list
    tuple's `weak_ptr<DecisionRequest>` (via `find`), copying them out — do not keep a strong ref.
  - **Key a decision handle on the `weak_ptr<DecisionRequest>`, not the token.** A renewed request
    (`decisionRequest.reset()` + new `DecisionRequest`, e.g. sequential-performer release) is a
    *different* request on the same token; keying on the request makes the stale handle expire and the
    renewed one appear as new — exactly right.
  - **Build each snapshot by iterating every `pending*Decisions` fully once** (this both prunes and
    yields a clean live set); don't retain engine iterators across any call that might erase.
- **Command guards** (`Engine::Command::execute`): a queued `advanceTo…` is skipped if its
  `weak_ptr<Token>`/`weak_ptr<StateMachine>` expired — a decision for a withdrawn token is a no-op.
- **Withdraw / renew** (`Token::occupySequentialPerformer`/`releaseSequentialPerformer`): taking a
  sequential performer **withdraws** siblings' entry requests and **renews** them later (fresh
  `DecisionRequest`).
- **Interrupt / obsolete** (`StateMachine::interruptActivity`/`clearObsoleteTokens`,
  `Token::withdraw`): subprocess interruption withdraws activity/boundary/multi-instance tokens.

## Core design decision: handle-based snapshot drive-loop (not raw pointers, not re-entrant callbacks)

Two rules make the JS API concurrency-safe by construction:

1. **JS never holds a C++ pointer.** Every token / decision-request / message crosses the boundary as
   an **opaque integer handle** backed by a `weak_ptr` in a bridge-side registry. Every JS→engine
   call (`submitDecision(requestId, …)`, etc.) **re-validates the handle against live state**; an
   expired handle returns a typed `{ rejected: "expired" }`, never a dangling-pointer crash. **This
   handle validation is the actual safety mechanism on the JS boundary** — it is what guarantees a
   stale decision cannot take effect or crash the module. Observing `WITHDRAWN` token/message entries
   in the log and retracting the corresponding UI/pending decisions each drain is a *complementary
   convenience* that keeps JS's own view consistent, but it does not by itself prevent a stale
   submission (JS may act before it has processed the WITHDRAWN entry). Validation + the engine's
   weak-ptr machinery make requirement 6 safe; the log just makes staying consistent easy.
2. **Snapshot, don't call back mid-run.** `resume()` runs synchronously and `notify()`s observers
   *during* the call. Rather than re-enter JS mid-run (fragile, JS mustn't call back into a running
   engine), the bridge **buffers** observed log entries and the current pending-decision set, and JS
   **drains the snapshot after `resume()` returns**. Each `resume` boundary re-snapshots the
   auto-pruned pending lists, so JS always sees a consistent, live view.

Drive loop from JS:

```js
const engine = await BpmnosEngine.create();          // instantiate wasm
engine.loadModel(bpmnXml);                            // (1) inputs
engine.loadLookupTables({ 'rates.csv': csv, ... });  //     or loadLookupFolder(path)
engine.loadInstances(instanceCsv);
engine.configure({ provider: 'static', evaluator: 'local', seed });

let step = engine.start();                            // (5) run to first stall
while (!step.done) {
  // step = { time, log: [...delta], pending: [{ requestId, type, node, instance, choices? }],
  //          alive, outcome }                          (2) log + decision requests, opaque handles
  const req = choose(step.pending);                   // JS decides
  step = engine.submitDecision({ requestId: req.requestId, type: req.type,
                                 status, choices, messageRequestId });   // (4) resume(decision)
  // or: step = engine.tick();                         (3) resume(ClockTickEvent)
  // or: step = engine.resume();                       (5) just continue automatic advancement
  // rejected/expired requests come back as step.rejected; JS re-reads step.pending
}
```

## Does the engine need changes? No code changes; at most additive CMake

Verified against the engine source — every seam the wasm bridge needs is already public and reusable,
so the entire interactive bridge (handle registry, observer, decision/tick construction, concurrency
validation) lives **in bpmnos-wasm**, using the engine as-is.

**Engine logic — no changes:**
- **Driving** — `run`, `resume(timeout)`, `resume(Decision)`, `resume(Event)`, `getSystemState()`,
  `getCurrentTime()`, `addSubscriber`/subscribe are all public (`Engine.h:10–68`).
- **Pending decisions** — `SystemState::pending{Entry,Choice,Exit,MessageDelivery}Decisions` are
  public, auto-pruning `weak_ptr` lists (`SystemState.h:92–95`) — read live, self-cleaning state.
- **Safe handles (the crucial pre-existing enabler)** — `Token : public
  std::enable_shared_from_this<Token>` (`Token.h:35`), so from the raw `DecisionRequest::token` the
  bridge calls `token->weak_from_this()` and stores a `weak_ptr` in its registry. The "JS never holds
  a raw pointer" rule needs no engine change to satisfy.
- **Message-delivery options** — the message pools (`unsent`, `inbox`, `outbox`,
  `messageAwaitingDelivery`, `messages`) are public on `SystemState`, and the candidate-enumeration
  classes (`candidates/MessageDeliveries`, `FirstEnumeratedChoice`, …) are public controller classes
  the bridge can instantiate; they touch only public members.

**Engine build config — at most small, additive, and worth trying to avoid.** The only place the
engine repo *may* need touching is CMake, not code: it currently assumes a native host
(`find_library(xerces-c)`/`find_library(bpmn++)`/`find_program(schematic++)`, `-pg` profiling,
sanitizers, ccache, and building `app`/`tests`/`docs`). Two options:
- **Preferred — toolchain-only, zero engine edits:** drive the engine's existing `bpmnos-model` /
  `bpmnos-execution` *library* targets via an Emscripten toolchain file that points `find_library`
  at the wasm-cross-compiled xerces/bpmn++ and skips the app/tests/docs subdirs.
- **Fallback — additive guards:** wrap host-only bits in `if(NOT EMSCRIPTEN)` (`-pg`, sanitizers,
  `add_subdirectory(app/tests/docs)`). Build-config only, never execution logic; stays consistent
  with "don't patch the engine's default targets." The M1 xerces spike decides whether it's needed.

**Runtime — confirmed no change needed:** `DataProvider`s read via `ifstream` (works against MEMFS);
`std::random_device` stochastic seeding is supported under Emscripten. `Metronome`'s `sleep_until`
is wasm-hostile, but JS-driven mode does not connect it.

## Milestones

### M0 — Toolchain & scaffold
- Pin emsdk; repo layout (`CMakeLists.txt` emscripten toolchain, `src/` bindings, engine reference,
  `test/`, `package.json`). Decide engine coupling (submodule / FetchContent / sibling) and document.
- **Exit:** `emcmake cmake && emmake make` configures and reaches the dependency build.

### M1 — Cross-compile dependencies (the primary risk)
- Cross-compile **xerces-c** to wasm; then **bpmn++** against it. Keep **schematic++** a *native*
  host codegen tool (generates the BPMNOS parser from `xsd/BPMNOS.xsd` at configure time). Portable
  deps (cnl/limex/nlohmann-json/strutil) via FetchContent as-is.
- **Exit:** all deps link into a trivial wasm test binary. *Spike this before committing to the rest.*

### M2 — Compile engine libs to wasm
- Build `bpmnos-model` + `bpmnos-execution` under Emscripten with **`-fwasm-exceptions`** (native
  WebAssembly exception handling, the form standardized in Wasm 3.0) and **RTTI on** — the engine
  both throws (`std::runtime_error`/`std::logic_error`, xerces parse errors) and uses `dynamic_cast`
  via `Event::is<T>()`. Drop `-pg`/sanitizers, keep `-std=c++23`. Resolve wasm-incompatible
  I/O / `std::random_device` / locale.
- **Exception mode is a whole-program ABI choice:** every object in the final link (engine, xerces-c,
  bpmn++) must be compiled with the *same* `-fwasm-exceptions` flag; mixing modes fails to link.
  `-fexceptions` (JavaScript-based EH) is the fallback only if a target runtime is too old for native
  Wasm EH (recent Node / Chrome / Firefox / Safari support it).
- **Exit:** a wasm module links the full engine and runs a hardcoded model to completion.

### M3 — Inputs from JS (requirement 1)
- Marshal model XML, lookup CSV tables (map of name→content **or** a folder path), and instance CSV
  from JS into **Emscripten MEMFS**, then hand paths to the unmodified `DataProvider`
  (`static|expected|dynamic|stochastic`) → `createScenario()`. Keeps engine file-reading code intact.
- **Exit:** `loadModel/loadLookupTables/loadInstances/configure` build a valid `Scenario` from
  JS-supplied strings.

### M4 — Handle registry + observer bridge (requirement 2 + concurrency foundation)
- Bridge-side registry mapping opaque integer handles ↔ `weak_ptr<Token>` /
  `weak_ptr<DecisionRequest>` / `weak_ptr<const Message>`; every lookup validates liveness.
- A bridge `Observer` (subscribing Token/Event/Message + the four `*Request` types) that buffers
  `jsonify()` entries and maintains the live pending-decision snapshot. Expose the log delta to JS
  (reuse `Recorder`, or a dedicated observer; `Recorder::inject` for JS-side entries).
- **Exit:** after a run, JS reads the full token/event/message log and a pending-decision list of
  opaque handles.

### M5 — Interactive run/resume, clock & decisions (requirements 3, 4, 5)
- `start()` = `run(scenario, timeout)`; `resume()`; `tick()` builds `ClockTickEvent(systemState)` and
  `resume(event)`; `submitDecision({requestId,type,status?,choices?,messageRequestId?})` resolves the
  handle, constructs the matching `EntryDecision/ExitDecision/ChoiceDecision/MessageDeliveryDecision`
  (or plain Event), and `resume(decision)`. Each call returns the post-resume snapshot.
- Decide plain-Event vs Decision+`evaluate()` (feasibility/reward for the UI) — support both.
- **Exit:** JS fully drives a model from start to terminal outcome, supplying every clock tick and
  decision itself.

### M6 — Concurrency-robustness hardening (requirement 6)
- Guarantee: submitting a decision for an expired/withdrawn/renewed request returns a typed
  `rejected` result, never a crash. Surface withdraw/interrupt/performer-renewal as observable events
  so JS can retract stale UI. Fuzz: interleave ticks, decisions, and stale/duplicate/out-of-order
  submissions; assert no dangling-pointer access (Emscripten ASan build) and state parity with native.
- **Golden + adversarial tests:** native `bpmnos-greedy --json` vs wasm on deterministic fixtures
  (`static`/`expected` provider or fixed seed) for parity; plus expiry/interrupt scenarios.
- **Exit:** stale-handle and interrupt scenarios pass under ASan with no leaks/UB.

### M7 — Packaging & DX
- ESM npm package, bundler/Vite-friendly, TypeScript types for the API/options/handle/log/decision
  shapes. Optional Web Worker wrapper (synchronous `resume()` off the main thread). Size pass
  (`-Oz`, closure, strip). Documented `npm run build`.
- **Exit:** `npm pack` yields a consumable module (wasm + JS glue + types).

### M8 — CI
- emsdk install → build wasm → golden + concurrency tests (`node --test test/*.test.mjs`) → publish.
- **Exit:** green CI producing a published artifact.

## Project shape, fixtures & test strategy

**Shape: a CMake + Node.js mix.**
- *CMake layer (C++ → wasm):* Emscripten toolchain build that cross-compiles xerces-c + bpmn++,
  pulls in the engine's `bpmnos-model`/`bpmnos-execution` library targets, compiles the bridge
  (`src/*.cpp` — handle registry, observer, embind bindings), and emits `bpmnos.wasm` + JS glue.
  Driven by `emcmake cmake` / `emmake make`, wrapped in an npm script (`npm run build:wasm`).
- *Node.js layer (JS API + packaging):* the ESM wrapper around the glue (`BpmnosEngine.create`,
  `loadModel`, `tick`, `submitDecision`, the drive-loop snapshot), the npm package + TS types, and
  the test suite (`node --test test/*.test.mjs`).

**Build up from small controlled tests to integration — never the reverse.** The `engine/tests/`
fixtures are minimal, single-feature models with exact `WHEN/THEN` assertions: use them to test each
API method and each decision type **in isolation** first. The `BPMNOSInstances.jl` instances
(bipartite matching, TSP, knapsack, …) are **full-fledged integration tests** — many decisions,
clock ticks, and interrupts interleaved — and should only be run **once every individual piece
already passes** its small controlled test. A failure in an integration run is near-useless for
debugging until the unit tier is green. Three tiers, in order:

1. **Per-API / per-decision tests.** One suite per method (`loadModel`, `loadLookupTables`,
   `loadInstances`, `configure`, `start`, `resume`, `tick`, `submitDecision` × the four decision
   types, log observation). `engine/tests/` already has fixtures targeting each decision type:

   | Decision / case | Fixture(s) in `engine/tests/` |
   |---|---|
   | **Entry / Exit** | `execution/task/Task_with_linear_expression.bpmn`, gated variants in `execution/status/`, `execution/condition/` |
   | **Choice** | `execution/decisiontask/DecisionTask_with_enumeration.bpmn` (enumerated), `…_with_bounds.bpmn` (bounded/bisectional) |
   | **Message delivery** | `execution/message/{Simple_messaging,Message_tasks,Multi-instance_send_task,Multi-instance_receive_task}.bpmn` |
   | **Interrupt / withdraw (req 6)** | `execution/eventsubprocess/Interrupting_escalation{,_throwing_error}.bpmn` and `execution/boundaryevent/{Failed_SubProcess,Failed_Task}.bpmn` — interrupting event subprocess / boundary failure withdraws activity tokens (`clearObsoleteTokens`/`interruptActivity`/`Token::withdraw`), the engine-internal decision-request-expiry path |

   The Catch2 `WHEN/THEN` assertions in each `test.h` double as an expected-lifecycle spec the wasm
   drive path must reproduce. **Only use fixtures whose `test.h` is actually `#include`d in
   `tests/main.cpp`** — e.g. `execution/request/` (`Simple_request`, `Revoked_request`) is currently
   commented out (open TODO) and tests model-level request/revoke *messages*, not decision-request
   expiry; do not anchor tests on it.

2. **Concurrency / adversarial tests (req 6) — still small & controlled.** Feed the active
   interrupt/withdraw fixtures (`eventsubprocess/Interrupting_escalation*`,
   `boundaryevent/Failed_*`) through the JS drive loop with hostile interleavings —
   expired/duplicate/out-of-order `submitDecision`, decisions after interrupt — under an Emscripten
   ASan build asserting no dangling access and state parity. These are single-feature models chosen
   precisely so a failure points at one mechanism.

3. **Golden-parity / integration tests — gated behind tiers 1–2.** Only once every API method,
   decision type, and interrupt case passes in isolation. Run native `bpmnos-greedy --json` (or use a
   precomputed reference) and assert the wasm module produces the same log/objective, using a
   deterministic provider (`static`/`expected`) or fixed seed.
   - *Small parity first:* `engine/examples/` — ~13 canonical problems, each `<Problem>.bpmn` +
     `instance.csv`; still modest, the bridge between unit and full integration.
   - *Full integration (last):* `bpmnos-bench/instances/bipartite_weighted_matching/` —
     `Bipartite_weighted_matching.bpmn` + many `bwm_10_10_*` instances, with
     `bpmnos-bench/results/deterministic-greedy/bipartite_weighted_matching/bwm_*-log.json` holding
     the **full precomputed Recorder log** (plus `*.json` objective), so wasm output diffs against a
     complete reference **without re-running native**. Many decisions/ticks/interrupts interleaved —
     a smoke signal that the whole stack composes, **not** a debugging tool. Do not reach for it
     until tiers 1–2 are green.

**Fixture corpus (three sources, distinct roles):**
- `engine/examples/` — small curated model+data pairs → golden-parity fixtures.
- `engine/tests/` — 88 feature `.bpmn` + lifecycle assertions → per-decision + concurrency fixtures.
- `BPMNOSInstances.jl/bpmnos-bench/` — 434 bpmn / 1314 csv generated by problem + solver `results/`
  → scale/perf fixtures and precomputed references (and a generator for more).

**Do not vendor everything.** Copy a small curated subset into `test/fixtures/` (a few from
`engine/examples/` + the key `engine/tests/` decision & interrupt cases); reference the sibling repos
by path for the optional large sweep, keeping the npm package lean.

**CI note:** golden-parity tests need a **native** engine build (xerces/bpmn++/schematic++) alongside
emsdk to generate/compare references — unless leaning solely on `bpmnos-bench/results/`. This makes
CI heavier than a pure-JS package (see M8).

## Key decisions to lock early
1. **Engine source coupling** — submodule vs FetchContent vs sibling path (reproducible CI).
2. **xerces on wasm** — cross-compile the real library vs. stub/replace the XML parse path.
   Everything downstream depends on it; spike in M1 first.
3. **Handle model over raw pointers** — non-negotiable for requirement 6; every boundary crossing is
   an opaque, liveness-validated handle.
4. **Snapshot drive-loop over re-entrant callbacks** — JS drains after each `resume()`; JS never
   calls into a running engine.
5. **Plain Events vs Decisions+evaluate()** — support both; `evaluate()` gives the UI feasibility.
6. **Threading** — single-threaded engine in a Web Worker (start here) vs pthreads (SharedArrayBuffer
   + COOP/COEP). Start single-threaded.
7. **Time mode** — JS-supplied ticks (default) vs connect `Metronome` for real-time pacing.
8. **Exception handling** — `-fwasm-exceptions` (native Wasm 3.0 EH) across the whole link; the bridge
   wraps every entry point in `try`/`catch` and returns a typed error, so an engine throw never traps
   the module. `-fexceptions` is the fallback for old runtimes only.
9. **Driving model** — Model A (run/stop/resume stepping; synchronous; no clock; natively testable)
   vs Model B (single `run()` to completion or user `TerminationEvent`, an interactive dispatcher with
   an Asyncify `emscripten_sleep` when idle so the CPU does not spin). Implement A first (it needs no
   Emscripten), add B once emsdk exists. Both share one bridge core.
