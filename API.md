# JavaScript API

Instantiate the module, then use its four classes. A run is composed of them: an `Input` becomes the data
provider an `Engine` draws scenarios from, a `Controller` supplies every event that does not come from the
engine, and a `Monitor` watches. Structured values cross the boundary as JSON strings;
scalars, identifiers, and CSV text cross as native numbers and strings. Instances are C++ objects; call
`delete()` on each when done. The type declarations are in `types/bpmnos.d.ts`.

```js
import createBPMNOS from './bpmnos.mjs';
const module = await createBPMNOS();
```

## Input

Assembles a run's inputs. It is consumed when an Engine is built from it, so one Input builds one Engine.

- `new Input(bpmnXml: string)` — parse the model.
- `getLookupTableNames(): string` — a JSON array of the lookup table source names the model references.
- `addLookupTable(name: string, csv: string)` — supply one lookup table by its source name.
- `setInstance(csv: string)` — supply the instance data.

## Engine

- `new Engine(input: Input, providerJson: string, controller: Controller, monitor: Monitor | null)` —
  `providerJson` is `{"provider": "static"|"expected"|"dynamic"|"stochastic", "seed": n}`, each field
  optional. A controller is required, a run without one fetching no event; a monitor is not, an
  unobserved run still reporting through `isAlive`, `getCurrentTime`, and `getWeightedObjective`.
- `run(scenarioId: number)` — draw the scenario and run from the start. Repeatable without reparsing;
  with the stochastic provider a different scenario id is a different sample.
- `resume()` — continue the run.
- `isAlive(): boolean` — whether the system may still proceed; a run is done once it is false.
- `getCurrentTime(): number` — the current simulated time.
- `getWeightedObjective(): number` — the total weighted objective value accumulated so far; a live running value, valid at any pause, not only at termination.

## Monitor

- `new Monitor()`.
- `addObserver(observer: (entryJson: string) => void)` — the observer receives every notification, as
  JSON, in the engine's execution order, the moment it is recorded. The monitor keeps no history, so
  attach observers before the run and observe only, never advancing the engine. `run` and `resume` block,
  so a consumer that must not block the calling thread runs the engine off it and forwards each entry.

Each entry is a single-keyed object naming the notification:

```
{"token": …} | {"event": …} | {"message": …} |
{"entryRequest"|"exitRequest"|"choiceRequest"|"messageDeliveryRequest": …deciding token…}
```

## Controller

A controller is the dispatchers it is composed of, walked in the order given on every fetch. What a
dispatcher settles is settled without the caller; what none of them settles waits for what the caller
enqueues, which the queue among them dispatches. Where that queue stands in the list is therefore the
precedence of the caller's decisions, and how much of a run the caller drives is a matter of composition
rather than of a mode.

- `new Controller(compositionJson: string)` — `{"evaluator": name?, "dispatchers": [name, …]}`.

A dispatcher is named by its class, or, for the ones a greedy dispatcher drives, by the candidates class
that distinguishes it:

| Name | Dispatches |
| --- | --- |
| `FirstFeasibleExit` | the first feasible exit |
| `FirstFeasibleEntry` | the first feasible entry, excluding children of a sequential ad hoc subprocess |
| `FirstEnumeratedChoice` | the best feasible choice of a decision task, by enumeration |
| `FirstBisectionalChoice` | the same, by bisection over a bounded choice |
| `SequentialEntries` | the best feasible entry of a child of a sequential ad hoc subprocess |
| `MessageDeliveries` | the best feasible message delivery |
| `CompetingCandidates` | the best of the two above, which have no precedence over each other |
| `InstantDirectMessage` | a delivery whose sender addresses its recipient, or whose recipient names its sender |
| `TimeWarp` | a clock tick advancing to the next scheduled moment |
| `Metronome`, `Metronome(ms)` | a clock tick in step with real time, a tick lasting `ms` milliseconds |
| `EnqueuedEvents` | what the caller enqueued and has not expired |

The evaluator is `"GuidedEvaluator"` (the default) or `"LocalEvaluator"`, and every dispatcher that
evaluates shares the one built here. Exactly one `EnqueuedEvents` is required: without it nothing could be
enqueued, and with several the precedence would fall to whichever was found first. A clock answers every
fetch, so `TimeWarp` and `Metronome` come last, behind the queue, or nothing behind them is ever reached.

The interactive composition is `["FirstFeasibleExit", "FirstFeasibleEntry", "InstantDirectMessage",
"EnqueuedEvents"]`: the unambiguous decisions resolve themselves, the choice, the sequential ad hoc entry
and the ambiguous message delivery fall to the caller, and time advances only by an enqueued tick. The
greedy composition adds `"FirstEnumeratedChoice"` and `"CompetingCandidates"` before the queue and
`"TimeWarp"` after it, and then a run needs nothing from the caller at all.

- `getPendingDecisions(): string` — `[{"type": "entry"|"exit"|"choice"|"messageDelivery", "instanceId":
  s, "nodeId": s}]`.
- `getSequentialPerformers(): string` — `[{"performer": {"processId": s, "instanceId": s, "nodeId": s?},
  "performing": {…} | null, "waiting": [{…}]}]`, each token named by the keys its own record carries and
  the node absent for a token standing at a process. A performer is reported while its token is busy,
  which is while the scope it stands in runs, and `waiting` is in the order the tokens became ready. This
  cannot be read from the pending decisions: while a performer is busy the entry requests of everything
  waiting under it are withdrawn, and one that has been asked for nothing has none at all.
- `getChoiceCandidates(instanceId: string, nodeId: string): string` — per choice of the decision task,
  `{"attribute": s, "enumeration": [v, …]}` or `{"attribute": s, "lowerBound": n, "upperBound": n,
  "multipleOf": n?}`, each value in the choice attribute's type.
- `getMessageCandidates(instanceId: string, nodeId: string): string` — `[{"origin": s, "sender": s,
  "message": {…}}]`.
- `enqueueEntryDecision(json)` / `enqueueExitDecision(json)` — `{"instanceId": s, "nodeId": s, "status":
  [v, …]?}`.
- `enqueueChoiceDecision(json)` — `{"instanceId": s, "nodeId": s, "choices": [v, …]}`, one value per
  choice of the decision task.
- `enqueueMessageDeliveryDecision(json)` — `{"instanceId": s, "nodeId": s, "origin": s, "sender": s}`.
- `enqueueClockTickEvent()` — advance the clock by one at the next resume.
- `enqueueTerminationEvent()` — end the run at the next resume.

Each `enqueue…` returns `{"queued": true}` or `{"rejected": reason}`. A decision names its token by
instance and node, and a message by its origin and sender; an enqueued decision whose token, request, or
message has expired is silently dropped.

## Driving

```js
const input = new module.Input(bpmnXml);
for (const name of JSON.parse(input.getLookupTableNames())) input.addLookupTable(name, lookup[name]);
input.setInstance(instanceCsv);

const monitor = new module.Monitor();
monitor.addObserver(entryJson => log(JSON.parse(entryJson)));
const controller = new module.Controller(JSON.stringify({
  dispatchers: [ 'FirstFeasibleExit', 'FirstFeasibleEntry', 'InstantDirectMessage', 'EnqueuedEvents' ]
}));
const engine = new module.Engine(input, JSON.stringify({ provider: 'static' }), controller, monitor);

engine.run(0);
let pending = JSON.parse(controller.getPendingDecisions());
while (pending.length) {
  const d = pending[0];
  const candidates = JSON.parse(controller.getChoiceCandidates(d.instanceId, d.nodeId));
  const choices = candidates.map(c => c.enumeration[0]);
  controller.enqueueChoiceDecision(JSON.stringify({ instanceId: d.instanceId, nodeId: d.nodeId, choices }));
  engine.resume();
  pending = JSON.parse(controller.getPendingDecisions());
}
```

Composed greedily, `run` proceeds to completion without any of this. `enqueueClockTickEvent` and
`enqueueTerminationEvent` advance or end a run when no decision is pending, which is how a composition
without a clock is carried forward.
