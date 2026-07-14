# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Current state

**Empty scaffold.** The repo contains only `README.md`, `LICENSE` (Apache-2.0), and `ROADMAP.md`;
remote is `github.com/bpmn-os/bpmnos-wasm`. Nothing has been built yet — the goal, layout, and
wiring below describe the *intended* project, drawn from its source repositories. When you add real
code, update this file to match (and drop this notice).

**Read `ROADMAP.md` first** — it holds the milestone plan, the verified interactive execution model,
and the two load-bearing design decisions (handle-based boundary, snapshot drive-loop).

## Goal

`bpmnos-wasm` is the **WebAssembly build of the C++ BPMN-OS engine plus its JS glue** — a
distributable module that runs the real engine in the browser (and Node). It is the piece that
[`bpmnos-workbench`](../../bpmnos-workbench) depends on: workbench is to `bpmnos-js` what
`bpmn-workbench` is to `bpmn-js`, but its playback/simulation must reflect **real BPMNOS extension
semantics** (status attributes, operators, restrictions, decisions, objectives) instead of the JS
token simulator. That means driving playback from the actual engine — which is what this repo
compiles and exposes.

The JS-facing API is **interactive**, not just a batch runner. JS acts as the engine's dispatcher:

- **Inputs:** load a BPMN model XML, lookup CSV tables (folder or the tables directly), and an
  instance CSV — all as in-memory strings written to Emscripten MEMFS.
- **Observe:** stream the engine's token/event/message log out via the `Observer` interface (or a
  dedicated bridge observer) — the same JSON the native `Recorder` emits.
- **Drive:** JS creates `ClockTickEvent`s and all four decision types (entry/exit/choice/message
  delivery) and passes them to the engine; JS runs and resumes the engine (`run`/`resume`).
- **Concurrency-robust:** a decision request may expire, a subprocess may be interrupted, a
  sequential performer may be taken. The engine already handles this via weak-ptr auto-pruning and
  command guards; the wasm layer must **surface** it, never crash. Two rules enforce this: JS holds
  only **opaque, liveness-validated handles** (never C++ pointers), and JS drains a **snapshot after
  each `resume()`** rather than being called back mid-run. See `ROADMAP.md`.

**Not in this repo:** the animation-log *adapter* (mapping the engine's token vocabulary to
`bpmn-js-animation`'s `createToken|advanceToken|forkToken|joinTokens|consumeToken` log) lives in
**`bpmnos-workbench`**, not here. This repo's contract is to deliver the raw engine token stream and
the interactive drive API; workbench adapts it. Keep that boundary.

## Source repositories (read these first)

All are siblings under `~/Code` / `~/Code/bpmnos`; each has its own `CLAUDE.md`.

- **`~/Code/bpmnos/engine`** — the **C++23 BPMN-OS engine** this repo compiles to wasm. Standard
  CMake build producing two static libs (`bpmnos-model`, `bpmnos-execution`) and the `bpmnos-greedy`
  executable. Namespaces `BPMNOS::Model` (parsing/data) and `BPMNOS::Execution`
  (engine/controller/observer). `app/main.cpp` is the reference for how the pieces compose and what a
  wasm entry point must replicate.
- **`~/Code/bpmnos/bpmnos-workbench`** — the **consumer**. Its `CLAUDE.md` documents the playback
  data contract and the token-vocabulary adaptation this repo feeds into.
- **`~/Code/bpmnos/bpmnos-js`** — the bpmn-js BPMNOS modules (moddle extension, decision-task
  renderer, properties panel) the workbench app builds on. Not a build dependency of the wasm module,
  but defines the same extension elements the engine parses.

## Key seam: the Observer interface

The engine streams execution via one virtual method (`engine/execution/engine/src/Observer.h`):
`notice(const Observable*)`. Subscribe with
`engine->addSubscriber(observer, Observable::Type::{Token,Event,Message})`. The native `Recorder`
(`engine/execution/observer/src/Recorder.{h,cpp}`) is such an `Observer`; each token change calls
`notice()`, which serialises `Token::jsonify()`. A **token observable** looks like:

```jsonc
{ "processId": "...", "instanceId": "...", "nodeId": "...", "sequenceFlowId": "...",
  "state": "ARRIVED|READY|ENTERED|BUSY|COMPLETED|EXITING|DEPARTED|...",
  "status": { /* named status attributes */ }, "data": { /* named data attributes */ } }
```

For wasm, implement a JS-facing `Observer` (in place of, or alongside, `Recorder`) that forwards each
`notice()` payload across the boundary, so the JS side receives the same token JSON.

## Compiling the engine to wasm

**No Emscripten setup exists yet** — the engine is a native CMake build (`-Werror` C++23). Expect to
add an Emscripten toolchain build (`emcmake cmake` / `emmake make`, or a dedicated CMake profile)
that emits a wasm module + JS glue. Keep the wasm build **out of the engine repo's default targets**
unless coordinated there; prefer consuming the engine from *this* repo (submodule / FetchContent /
sibling path) over patching it.

The hard part is the engine's native dependencies, which must also build under Emscripten:

- **System-installed** (native build): Xerces-C++ 3.2.x, [`bpmn++`](https://github.com/bpmn-os/bpmnpp),
  [`schematic++`](https://github.com/rajgoel/schematicpp) (generates the BPMN XSD parser at build
  time). Xerces is the notable wasm hurdle — either cross-compile it for wasm or replace/stub the XML
  parsing path.
- **Fetched via CMake `FetchContent`**: cnl (fixed-point numerics), limex (expression evaluation),
  nlohmann/json, strutil, Catch2. These are header/portable C++ and should port more readily.

Consumers (the app, tests) include the engine's **generated single headers** (`lib/bpmnos-model.h`,
`lib/bpmnos-execution.h`), not individual source headers — link the wasm build against those.

## Expected stack & conventions (once scaffolded)

- **JS side:** follow bpmnos-js / bpmn-workbench — **Vite** (`npm run dev` / `build` / `preview`),
  ESM, `"node": ">=22"`, `node --test test/*.test.mjs` for tests. Ship a package the workbench can
  depend on (wasm binary + JS glue + typed-ish API surface).
- **C++ side:** the engine is strict C++23 (`-Werror -Wpedantic -Wall -Wextra -Wconversion
  -Wsign-conversion -Wshadow=local`). Numeric values use `BPMNOS::number` (cnl fixed-point);
  convert via `BPMNOS::to_number` / typed conversions, not raw casts. Any glue/binding code that
  touches engine types must honour these.
- License is **Apache-2.0** here (the engine and bpmn-os repos vary — check each; do not assume MIT).
