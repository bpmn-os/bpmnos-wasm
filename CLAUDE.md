# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Current state

The bridge is four classes in `BPMNOS::WASM`; `src/wasm/Bindings.cpp` is the JSON boundary — the classes
take and return native types, the bindings translate and resolve the caller's identities to native
handles. `types/bpmnos.d.ts` has the signatures and JSON shapes. All four decision kinds, the clock tick,
and one autonomous run are covered by native and WebAssembly tests that pass with no sanitizer finding.

`Input` parses the model once, reports the referenced lookup tables through `getLookupTableNames`
(`Model::getLookupTableNames`), and takes the lookup tables and instance as text. It yields a
`BPMNOS::Model::Input`, moved out when an `Engine` is built.

`Engine` builds the data provider on construction. `run(scenarioId)` draws a scenario and runs a fresh
engine, reusing the parse; `resume`, `isAlive`, and `getCurrentTime` follow the execution engine.

`Monitor` forwards each token, event, message, and decision request to every observer registered with
`addObserver`, serialised through `jsonify`, synchronously in the engine's order, keeping no log. An
observer only observes and is attached before the run.

`Controller` auto-resolves the first feasible exit, the first feasible non-sequential entry, and the
directly addressed message delivery under a guided evaluator, and leaves the choice, the sequential ad hoc
entry, and the ambiguous message delivery to the caller. It exposes `getPendingRequests`,
`getChoiceCandidates` (the attribute and the raw numbers or the bounds), and `getMessageCandidates`, and
takes a decision as the request weak pointer and its payload through `enqueue*`, returning `std::expected`.
It attaches no time handler; the caller enqueues a clock tick or a termination. An enqueued decision is
built into an event at once and dispatched only while `Event::expired()` is false. Omit the controller to
run under the greedy application.

The WebAssembly build links into `dist/bpmnos.mjs` and `dist/bpmnos.wasm`. `API.md` documents the
JavaScript API and `types/bpmnos.d.ts` declares it.

## Building and testing

The bridge consumes the engine's amalgamated headers and its prebuilt static libraries and never
modifies or rebuilds the engine. The default engine location is a sibling checkout, overridable with
the `BPMNOS_ENGINE_DIR` cache variable.

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The prebuilt engine archives are compiled with the address, undefined, and leak sanitizers, so the
build links the bridge and the tests the same way. This is not optional; a plain link fails to
resolve the sanitizer runtime. If a release build of the engine is used instead, clear the
`BPMNOS_SANITIZE` cache variable. The tests read their fixtures from an absolute path passed on the
command line, so they do not depend on the working directory.

## Working with the engine

The engine is treated as fixed and is not to be changed. Everything the bridge needs is reachable
through the engine's public interface, its public system state, and the engine's weak-pointer lifecycle:
tokens, decision requests, and messages each yield a weak pointer, and an event reports through
`Event::expired()` whether it has become stale. Confirm any engine fact against the source under
`../engine` rather than assuming it, and check the BPMN specification where the behaviour is a
specification matter.

A few facts discovered while building the bridge are easy to get wrong. A choice of a decision task is
defined either as an enumeration or as a bounded range, and `getEnumeration` and `getBounds` each assert
unless called for the matching kind, so the bridge discriminates on the choice's `enumeration` member and
returns the raw numbers or the bounds with `multipleOf`; the bindings then render each value in the
attribute's type (`ValueType`), so a string choice reads as its labels through the string registry, not
as indices. A static scenario reports completion only once simulated time has passed the last
instantiation, so a model with a single instance at time zero stays alive after all its work is done
until time advances, which is why a clock is needed to reach a formally terminal state even for a model
without timers. The pending decision lists prune expired entries only while traversed, and a built event
self-validates through `Event::expired()`, so the bridge treats the weak pointer, not list membership, as
liveness, and never holds a strong reference that would keep an engine object alive.

Advancing simulated time by a clock tick is done through the controller, which attaches no time handler:
the caller enqueues a clock tick, which the controller dispatches as a clock tick event at the next
fetch, and each tick advances the current time by one. A model with a timer therefore reaches a terminal
state once the caller has ticked the clock past the trigger, which the native and WebAssembly timer tests
exercise.

## Branching

The `develop` branch is the integration branch. Each independent feature is developed on its own
`feature` branch and merged into `develop` with a merge commit, so that a feature is one revertable
commit in review. Commit only what compiles and passes its test. The engine may not be modified; a
change that genuinely requires one is proposed for approval first, and experimental changes to a
local copy of a dependency belong under a build directory rather than in the engine.

## Relationship to the workbench

The translation of the engine's token vocabulary into an animation or playback log lives in
`bpmnos-workbench`, not here. This repository delivers the engine's own record and the interactive
drive interface; the workbench adapts it. That boundary is deliberate and is kept.
