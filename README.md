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

The controller is the input side. It derives from the engine's event dispatcher interface, so that
once it is connected to an engine the engine's fetch loop polls it for the next event. When the
caller has submitted a decision the controller returns it; otherwise it returns nothing, and the
engine, finding no further event to process, stops and returns to the caller. The controller owns
the registry that maps an opaque request identifier, which is the only reference the caller ever
holds, to a weak pointer into engine state. It revalidates that identifier both when a decision is
submitted and again when the decision is dispatched, so a decision aimed at a token that has since
been withdrawn cannot take effect.

The engine class owns the execution engine. It builds the data provider and the scenario from the
loaded model, lookup tables, and instance data, connects a monitor and, when the caller intends to
supply decisions, a controller, and exposes the calls that load inputs and the calls that advance
execution. A caller constructs a monitor, and a controller when it means to decide, attaches them,
loads the model and the data, and then drives.

## Driving an execution

Execution proceeds by starting the engine and then repeatedly reading its state, submitting a
decision, and resuming, until the engine has nothing further to do. Each advancing call returns a
snapshot, which is a structured object carrying the current simulated time, the log entries
recorded since the previous snapshot, the decisions the engine is currently waiting for, and
whether the system is still considered alive.

The decisions the engine waits for are the entry of a token into an activity, the exit of a token
from an activity, the choice made at a decision task, and the delivery of a message. Each pending
decision in a snapshot carries an identifier, its kind, and the token it concerns. A choice
additionally carries, for every choice the decision task defines, either the enumeration of allowed
values or the lower and upper bounds, depending on how the choice is written, so that the caller
knows what it may submit. The caller submits a decision by naming the identifier and the kind and,
for a choice, the values it selects.

Two rules make this safe against the concurrency the engine already manages internally. The caller
never holds a pointer into the engine; it holds only an identifier, and every submission is
revalidated against live state before it can act. The caller reads a snapshot only after an
advancing call has returned, rather than being called back while the engine is running, so it
always observes a consistent view and never re enters a running engine.

Advancing simulated time by a clock tick is not yet part of the interface. Where that operation
belongs, whether the engine class injects it or the controller dispatches it, is an open question,
and the behaviour tested so far does not require it. It becomes necessary once a model carries
timers or once execution must reach a formally terminal state, and it will be settled then.

## Building and testing

The bridge consumes the engine's amalgamated public headers and its static libraries and never
rebuilds or modifies the engine. The build assumes the engine is a sibling checkout; a different
location may be given through the `BPMNOS_ENGINE_DIR` cache variable.

```
cmake -S . -B build
cmake --build build --target native_drive_test
./build/native_drive_test test/fixtures
```

The native drive test exercises the decision task fixture through the run, stop, and resume model
with no clock. It supplies the entry, the choice, and the exit, confirms that the choice
enumeration is offered and that the submitted value is applied, and confirms that resubmitting a
consumed identifier is rejected rather than crashing. Because the prebuilt engine libraries are
compiled with the address, undefined, and leak sanitizers, the bridge and the test are built and
linked the same way, and the test passes with no sanitizer finding of any kind.

## The WebAssembly build

The WebAssembly build is a self contained superbuild. Under the Emscripten toolchain the same CMake
fetches xerces, bpmn++, and the engine from source into the build tree, cross compiles them there,
and links the bridge and its embind bindings into a module.

```
emcmake cmake -S . -B build-wasm
cmake --build build-wasm
node test/wasm/drive_test.mjs
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
