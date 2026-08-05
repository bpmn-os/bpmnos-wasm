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

`Engine` is given the parts a run is made of and assembles nothing: the data provider arrives built, the
controller drives every run, and a monitor, if there is one, observes it. `run(scenarioId)` draws a scenario
and runs a fresh engine, reusing the parse; `resume`, `isAlive`, and `getCurrentTime` follow the execution
engine. A controller is required, a run without one fetching no event; a monitor is not, since an
unobserved run still reports through `isAlive`, `getCurrentTime`, and `getWeightedObjective`.

`Monitor` forwards each token, event, message, and decision request to every observer registered with
`addObserver`, serialised through `jsonify`, synchronously in the engine's order, keeping no log. An
observer only observes and is attached before the run.

`Controller` is the dispatchers it is composed of, connected to it when it is constructed and walked in the
order given on every fetch. What a dispatcher settles is settled without the caller; what none of them
settles waits for what the caller enqueues, which `EnqueuedEvents` dispatches, so the position of that queue
is the precedence of the caller's decisions. Exactly one is required, and the constructor refuses a
composition with none or with several. How much of a run the caller drives is therefore a composition and
not a mode: interactive is the exit, the entry, the direct message and the queue; greedy adds the choice and
the competing candidates before the queue and a clock after it. The names the bindings build from are the
engine's class names, `Metronome` optionally with its tick duration as `Metronome(500)`.

The controller exposes `getPendingRequests`, `getChoices` (the choices a decision task states, in order) and
`getChoiceCandidates` (the attribute and the raw numbers or the bounds of the next choice, given the values
already selected), resolves an enqueued delivery to a message through `getMessageCandidates`, and takes a decision
as the request weak pointer and its payload
through `enqueue*`, returning `std::expected`. An enqueued decision is built into an event at once and
dispatched only while `Event::expired()` is false. It names no evaluator: whichever dispatcher evaluates
keeps a share of the one the bindings built.

What a caller cannot read from the entries it replays is what the model resolves, and `describeModel` in
`src/Structure.cpp` answers it: the sequential performers and the activities each performs, and the decision
tasks with the choices each states, built from the XML and the lookup tables the model references and
nothing of a run. It is deliberately not part of `Input`, which assembles what a run is given rather than
analysing it.

The choices are reported here rather than left to the caller because `Choice::Choice` has already read the
condition. Whether a choice is an enumeration or a pair of bounds, and which attribute it is of, are settled
when the model is built, and a caller reading the condition a second time would restate the engine's grammar
in another language and would be wrong wherever the two readings differed. Nothing about them is evaluated
here: what a choice may take depends on the status, the data and the globals, and is asked of the controller
through `getChoiceCandidates`. The two answers divide as the bridge divides everywhere, the description
saying what the model states and the controller what a pending request may take.

A message delivery request carries, beside the deciding token, the senders that token accepts and the header
it expects. A caller replaying the entries cannot ask which messages a token may receive, since that answer
changes with every message created while the request stands and the request is not raised again; the
criterion does not change, so the caller matches a message against it as the engine does.

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
as indices.

A bounded choice is answered as two pairs rather than one. The bounds the condition states are not in
general values that may be selected, since the multiples of the discretizer fall where they fall, and a
fractional step is held slightly beside the one written, so a tenth puts its grid a little off every round
number. `BoundedChoice` therefore carries `lowerBound` and `upperBound`, which are the condition's own
after strictness and the attribute's type have been resolved, beside `lowest` and `highest`, which are the
first and the last multiple within them. A caller offers the second pair and may mention the first. Where
the model states no discretizer the type is asked instead: an integer or a boolean takes whole values, so
the step is one, and a decimal has nothing implying one and is answered without a step.

The choices of one decision task are not independent. `DecisionTask::determineAlternatives` writes each
chosen value into the status through `AttributeRegistry::setValue` before it evaluates the condition of the
next choice, so what a later choice admits is a function of the earlier ones and the candidates can only be
answered for a prefix of values. The bridge copies the status, the data and the globals before applying
anything, the data as `BPMNOS::Values` and not as the `SharedValues` the token holds, since that is a vector
of references and copying it would share the referents and make a question into an act. It stamps the
current time onto the copied status first, as `Engine::process` stamps it before applying a choice, so that
a condition reading the timestamp is evaluated against the value the engine will use.

The bounds a choice reports are moved to the multiples of its discretizer. `Choice::getBounds` has by then
resolved two things that are independent of each other: strictness, by moving a strict bound inward by
`BPMNOS_NUMBER_PRECISION`, so the pair is inclusive whichever of `<` and `<=` the condition wrote; and the
attribute's type, by narrowing the interval to the integers within it for everything but a decimal, so that
`0.5 <= x <= 1.5` on an integer admits only one. Neither is recomputed. What the bridge adds is the move to
the grid, because the values admitted are the multiples of the discretizer counted from zero while a
caller's control counts its steps from the minimum it is given, and the two agree only once that minimum is
itself a multiple. A bounded choice stating no discretizer is not asked of `getEnumeration`, which throws
for a decimal and silently assumes a step of one for an integer or a boolean; its bounds are reported
unmoved instead. A static scenario reports completion only once simulated time has passed the last
instantiation, so a model with a single instance at time zero stays alive after all its work is done
until time advances, which is why a clock is needed to reach a formally terminal state even for a model
without timers. The pending decision lists prune expired entries only while traversed, and a built event
self-validates through `Event::expired()`, so the bridge treats the weak pointer, not list membership, as
liveness, and never holds a strong reference that would keep an engine object alive.

Advancing simulated time is a matter of composition. A composition without a clock advances only by what the
caller enqueues: a clock tick reaches the queue, is dispatched at the next fetch, and moves the current time
by one, so a model with a timer reaches a terminal state once the caller has ticked past the trigger, which
the native and WebAssembly timer tests exercise. A composition with `TimeWarp` or `Metronome` advances by
itself, and either must stand behind the queue, because a clock answers every fetch and nothing behind it
would ever be reached.

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
