# bpmnos-wasm

[![wasm](https://github.com/bpmn-os/bpmnos-wasm/actions/workflows/build-wasm.yml/badge.svg)](https://github.com/bpmn-os/bpmnos-wasm/tree/dist/dist)
[![demo](https://github.com/bpmn-os/bpmnos-wasm/actions/workflows/pages.yml/badge.svg)](https://bpmn-os.github.io/bpmnos-wasm/)

This repository compiles the C++ BPMNOS execution engine to WebAssembly and provides a
JavaScript interface that drives it interactively. It knows nothing about any modelling
application, any diagram library, or any playback format. It takes a BPMN model together with
lookup tables and instance data, lets the caller act as the engine's dispatcher by supplying
clock ticks and decisions, and reports the engine's own record of token, event, and message
activity. Any translation of that record into an animation or playback vocabulary happens in a
separate project and never here.

The same interface serves both simulation and live execution. Nothing in it presumes a
simulated setting.

## Architecture

The bridge is three classes in the namespace `BPMNOS::WASM`, each of which corresponds to one of
the engine's own extension points. Keeping them separate is deliberate, because the engine is
meant to be extended in exactly these two directions, and because the third responsibility, owning
the engine and driving its lifecycle, belongs to neither of the other two.

The monitor is a passive sink. It derives from the engine's observer interface, subscribes to the
token, event, and message notifications, and records each of them, serialised through the engine's
own representation, into an append only log. It owns no engine and never advances execution, so it
may be attached on its own to any engine, including one that a caller drives with decisions and one
that runs autonomously under the engine's own controller. Withdrawal of a token reaches the caller
through this same log as an ordinary token record whose state is reported as withdrawn.

The controller is the input side. It derives from the engine's controller, so once it is connected
the engine's fetch loop polls it for the next event. It owns the unambiguous half of the greedy
controller, the feasible exit, the feasible non sequential entry, and the directly addressed message
delivery, each evaluated by a guided evaluator, and resolves those without the caller. Everything
contested, a choice, the entry of a child of a sequential ad hoc subprocess, and an ambiguous message
delivery, it surfaces to the caller through the snapshot's pending decisions and applies from a
submitted decision; the caller also advances the clock and ends execution through it. A decision is
identified by the natural identity of its token, its instance and its node, and a message by its
origin and its sender, and every submission is validated against the live system state when it is
dispatched, so a decision aimed at a token that has since been withdrawn finds no match and is dropped.

The engine class owns the execution engine. It takes its inputs in memory and touches no filesystem:
it parses the model XML into a tree with the engine's own parser, reports through requiredLookups which
lookup tables the model references so the caller can supply each by its source name, and holds the
instance data as text, assembling the data provider and the scenario from these when a run begins. It
connects a monitor and, when the caller intends to supply decisions, a controller, and exposes the
calls that load inputs and the calls that advance execution. A caller constructs a monitor, and a
controller when it means to decide, attaches them, loads the model and the data, and then drives.

## Driving an execution

Attaching a controller makes execution interactive. The controller resolves the unambiguous decisions
itself, the feasible exit, the feasible non sequential entry, and the directly addressed message
delivery, and it stops the engine at everything it leaves to the caller. Execution then proceeds by
starting the engine and repeatedly reading its state, submitting an input, and resuming, until the
engine has nothing further to do. Each advancing call returns a snapshot carrying the current simulated
time, the log entries recorded since the previous snapshot, the decisions the engine is waiting for,
and whether the system is still considered alive.

The decisions left to the caller are a choice at a decision task, the entry of a child of a sequential
ad hoc subprocess, and a message delivery that is not explicitly addressed. Each pending decision in a
snapshot carries its kind and the instance and node of the token it concerns. A choice additionally
carries, for every choice the decision task defines, either the enumeration of allowed values or the
lower and upper bounds, depending on how the choice is written. A message delivery carries its candidate
messages, each identified by its origin and its sender from the message header. The caller submits a
decision by naming that identity and, for a choice, the values it selects, or, for a delivery, the
origin and sender of the message it chooses.

The caller also advances simulated time and ends execution through the controller. No time handler is
attached, so time does not advance on its own; the caller submits a clock tick, which advances the
current time by one, and resumes, and it submits a termination to end execution. A model with a timer
reaches a terminal state once the caller has ticked the clock past the trigger.

Two rules keep this safe against the concurrency the engine manages internally. The caller never holds
a pointer into the engine; it holds only the natural identity of a token or a message, and every
submission is validated against the live system state when it is dispatched, so a decision for a token
that has since been withdrawn finds no match and is dropped. The caller reads a snapshot only after an
advancing call has returned, rather than being called back while the engine is running, so it always
observes a consistent view and never re enters a running engine.

## Building and testing

The bridge consumes the engine's amalgamated public headers and its static libraries and never
rebuilds or modifies the engine. The build assumes the engine is a sibling checkout; a different
location may be given through the `BPMNOS_ENGINE_DIR` cache variable.

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The native tests drive real fixtures through the interactive controller: the timer test advances the
clock a tick at a time until a timer fires and the process terminates; the choice test makes the choice
of a decision task whose entry and exit are resolved automatically; the sequential entry test enters the
children of a sequential ad hoc subprocess one at a time; and the message test delivers an unaddressed
message of the assignment problem by naming it by its origin and sender. Because the prebuilt engine
libraries are compiled with the address, undefined, and leak sanitizers, the bridge and the tests are
built and linked the same way, and they pass with no sanitizer finding of any kind.

## The WebAssembly build

The WebAssembly build is a self contained superbuild. Under the Emscripten toolchain the same CMake
fetches xerces, bpmn++, and the engine from source into the build tree, cross compiles them there,
and links the bridge and its embind bindings into a module.

```
emcmake cmake -S . -B build-wasm
cmake --build build-wasm
node test/wasm/timer_test.mjs
```

This produces `build-wasm/bpmnos.js` and `build-wasm/bpmnos.wasm`, and the Node tests under
`test/wasm` load the module and drive the same fixtures as the native tests, confirming that the
engine executes inside WebAssembly. The module exposes the engine, the controller, and the monitor
through embind, and every value that the C++ side expresses as JSON crosses the boundary as a JSON
string. The engine cross compiles from its upstream source with no patch. The details of the build
and the reason it selects a UTF-8 locale are recorded in `docs/wasm-build.md`.

## Relationship to the engine

The engine is treated as fixed. The bridge reaches everything it needs through the engine's public
interface, its public system state, and the fact that a token can yield a weak pointer to itself,
so no change to the engine is required to drive it, to observe it, or to keep the boundary safe. The
milestone plan, the verified model of how the engine executes, and the reasoning behind the design
are recorded in `ROADMAP.md`.
