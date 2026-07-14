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

The WebAssembly build is in progress on the `feature/wasm-build` branch and is not yet on the
integration branch. The bridge sources compile under Emscripten without change; the remaining work is
confined to the engine's dependencies and to cross compiling xerces and bpmn++. That branch carries a
note recording the precise state and the plan.

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
