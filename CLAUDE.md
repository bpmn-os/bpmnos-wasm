# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Current state

The interactive bridge exists and is tested. The repository compiles the C++ BPMNOS execution engine
and exposes a JavaScript facing interface that drives it. Three classes in the namespace
`BPMNOS::WASM` make up the bridge. The monitor is a passive observer that records the token, event,
and message log. The controller is an event dispatcher that supplies the caller's decisions and owns
the opaque handle registry. The engine class owns the execution engine and drives its lifecycle. All
four decision kinds, entry, exit, choice, and message delivery, are implemented, and two native tests
drive real fixtures and pass with no sanitizer finding.

The monitor keeps a log that only grows and returns fresh entries through `drainLog`, and it also
accepts a live callback through `onNotice` that it invokes with each entry, serialised as a JSON
string, at the moment the entry is recorded. Both paths deliver the same entries in the same order,
and that order is the engine's own order of execution. The engine notifies its observers
synchronously on one thread, calling `notice` once for each state change as execution proceeds, and
the monitor appends the entry to its log and then invokes the callback within that same call, before
the engine issues the next notification. Nothing in the bridge sorts, batches, or defers, so a
consumer observes exactly the sequence the engine produced, and passing a null callback clears the
sink. The one condition is that the callback runs inside `notice` inside the engine's run and must
only observe. A callback that advanced the engine, or that deferred its work across a promise or a
timer before forwarding the entry, could interleave notifications and lose the order.

Because `start` and `resume` are single blocking calls, a consumer that must not block while the
engine runs drives the engine from a worker and forwards each entry from the callback into its own
context. A message posted from a worker arrives in the order it was sent, so the order survives that
boundary as well. The demo does exactly this: it runs the engine in a worker, appends each entry to
the page as it arrives, and reports both the engine's run time and the time until the display is
complete. The workbench will consume the stream the same way, and because it paces playback to the
animation of token movement, the cost of serialising the log stays hidden behind the wait for
movement.

The WebAssembly build works. Under the Emscripten toolchain the same CMake fetches xerces, bpmn++,
and the engine from source into the build tree, cross compiles them there, and links the bridge and
its embind bindings into `build-wasm/bpmnos.js` and `build-wasm/bpmnos.wasm`. The engine cross
compiles from its upstream source with no patch. The Node tests under `test/wasm` drive the same
fixtures as the native tests and pass. See `docs/wasm-build.md` for the build and the UTF-8 locale
it selects.

Read `ROADMAP.md` for the milestone plan, the verified model of how the engine executes, and the
reasoning behind the design. Read `README.md` for the account of the architecture and the driving
model. This file records what is needed to work in the code.

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
through the engine's public interface, its public system state, and the fact that a token and a
message each yield a weak pointer to themselves. Confirm any engine fact against the source under
`../engine` rather than assuming it, and check the BPMN specification where the behaviour is a
specification matter.

A few facts discovered while building the bridge are easy to get wrong. A choice of a decision task
is defined either as an enumeration or as a bounded range, and the engine's `getEnumeration` and
`getBounds` each assert unless called for the matching kind, so the kind must be discriminated on the
choice's `enumeration` member before either is called. A static scenario reports completion only once
simulated time has passed the last instantiation, so a model with a single instance at time zero
stays alive after all its work is done until time advances, which is why a clock is needed to reach a
formally terminal state even for a model without timers. The engine's pending decision lists prune
expired entries only while they are traversed, so the bridge treats its own weak pointer, not
membership in a list, as the test of liveness, and it never holds a strong reference that would keep
an engine object alive.

Advancing simulated time by a clock tick is not yet implemented. Whether the engine class injects it
or the controller dispatches it is an open question left for a session that settles it together.

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
