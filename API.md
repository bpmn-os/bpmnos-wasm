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

## Describing a model

`describeModel(descriptionJson: string): string` reports what the engine resolves as it builds a model,
which a caller needs whether or not it runs anything. The description is `{"model": s, "lookupTables":
{name: csv}?}`; the lookup tables are required because a model cannot be built without the content of every
table it references, and are read for nothing else. The answer is

```
{"sequentialPerformers": [{"performer": s, "activities": [s]}],
 "decisions": [{"node": s,
                "choices": [{"attribute": {"id": s, "name": s, "type": s}, "kind": s, "discretized": b}]}]}
```

or `{"error": message}` where the model could not be built. A sequential ad hoc subprocess performs its
children one at a time, and the node that performs them is resolved by walking up from the subprocess to
the first activity carrying a performer named "Sequential", never crossing another sequential ad hoc
subprocess, then to the enclosing process, and failing both the subprocess performs for its own children.
One node may perform for several subprocesses, and its activities are then all of theirs.

Each decision task is reported with the choices it states, in the order it states them and with the
attribute each is of, named by the identifier it is resolved by as well as by its name and its type. A
choice is stated either as an enumeration or as a pair of bounds, which `kind` names as `"enumeration"` or
`"bounds"`, and `discretized` says whether a bounded one states a discretizer. Which of the two a choice is
was settled when the model was built, so a caller drawing a control for a choice takes it from here rather
than reading the condition again, which would restate the engine's grammar in another language. Nothing
here is evaluated, and what a choice may take is asked of the controller instead, since it depends on the
status, the data and the globals and only an engine standing at the token can answer it.

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
  unobserved run still reporting through `isAlive`, `getCurrentTime`, and `getObjective`.
- `run(scenarioId: number)` — draw the scenario and run from the start. Repeatable without reparsing;
  with the stochastic provider a different scenario id is a different sample.
- `initialize(scenarioId: number)` — draw the scenario and prepare the engine without advancing it. The
  run begins at the scenario's earliest instantiation time and the clock tick that opens it is the first
  record of the stream. `run` is this followed by `resume`.
- `resume()` — continue the run.
- `advance(): boolean` — fetch a single event and advance the system state as far as it can without
  fetching the next, returning whether the run may continue. A caller that drives the engine itself calls
  `initialize` once and then `advance` until it answers false, and is never more than one event ahead of
  the records it has received.
- `isAlive(): boolean` — whether the system may still proceed; a run is done once it is false.
- `getCurrentTime(): number` — the current simulated time.
- `getObjective(): number` — the objective value the run maintains; a live running value, valid at any pause, not only at termination, and zero before the first run. The engine keeps the objective as the first global attribute, so every token entry carries the same value under the name that attribute is declared with, and a caller that reads the entries needs this call only where it observes nothing.

## Monitor

- `new Monitor()`.
- `addObserver(observer: (entryJson: string) => void)` — the observer receives every notification, as
  JSON, in the engine's execution order, the moment it is recorded. The monitor keeps no history, so
  attach observers before the run and observe only, never advancing the engine. `run` and `resume` block,
  so a consumer that must not block the calling thread runs the engine off it and forwards each entry.

Each entry is a single-keyed object naming the notification:

```
{"token": …} | {"event": …} | {"message": …} |
{"entryRequest"|"exitRequest"|"choiceRequest": …deciding token…} |
{"messageDeliveryRequest": {…deciding token…, "senders": [s], "recipientHeader": {key: value|null}}}
```

A message delivery request carries what the waiting token accepts beside the token itself, because a
caller replaying the entries cannot ask which messages it may receive: that answer changes whenever a
message is created, and the request is not raised again. The criterion does not change while the token
waits, so a caller matches a message against it as the engine does, the message's origin being among the
senders, its header holding the same keys, and the values equal wherever both sides state one.

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
- `getChoiceCandidates(instanceId: string, nodeId: string, selectedValues: string): string` — the next
  choice the decision task waits for, given the values selected so far. `{}` where the request no longer
  stands, `{"complete": true}` where every choice has a value, and otherwise `{"attribute": s,
  "enumeration": [v, …]}` or `{"attribute": s, "lowerBound": n, "upperBound": n, "lowest": n, "highest":
  n, "multipleOf": n?}`, each value in the choice attribute's type. A decision task states its choices in
  order and a later one may depend on the earlier ones, so the candidates are asked for one choice at a
  time, each against the values already selected. A bounded choice is reported twice over: `lowerBound`
  and `upperBound` are the bounds as the condition states them, with strictness and the attribute's type
  already resolved, while `lowest` and `highest` are the least and the greatest value that may be
  selected. They are the choice's own answer, the ends of the values it admits, and not a grid computed a
  second time from the bounds and the step, so what a caller is offered and what the engine accepts are one
  set. Those values are the multiples of `multipleOf` within the bounds, counted from zero. The two pairs
  differ wherever a bound is not itself a multiple, and a caller that offers only the bounds would offer
  values the engine does not admit. Where the model states no discretizer the step is the one the
  attribute's type implies, one for an integer or a boolean and none for a decimal; where there is no
  step, `lowest` and `highest` are the bounds and `multipleOf` is absent.

  Every value is reported in the choice attribute's type, and `multipleOf` is the exception: a step is not
  a value an attribute holds, so it is reported at the precision it was evaluated at rather than rounded to
  what a value may be. A caller computes the values the choice admits as the engine does, by rounding each
  multiple of the step to the precision of a value: for `1 <= x <= 10, 1/3 | x` the step is a third, and
  the values are `1`, `1.333333`, `1.666667` and so on up to `10`. A step rounded first would put every
  multiple a little below the thirds, and further below with each one, so that neither bound was
  selectable although both are.
- `enqueueEntryDecision(json)` / `enqueueExitDecision(json)` — `{"instanceId": s, "nodeId": s, "status":
  [v, …]?}`.
- `enqueueChoiceDecision(json)` — `{"instanceId": s, "nodeId": s, "choices": [v, …]}`, one value per
  choice of the decision task, in the order the choices are stated. The values are positional where the
  record of a choice names its attributes, because a decision task is not forbidden to state two choices
  on the same attribute: a named object would merge them and lose a value, which a record being read can
  afford and a decision being made cannot.
- `enqueueMessageDeliveryDecision(json)` — `{"instanceId": s, "nodeId": s, "origin": s, "sender": s}`.
- `enqueueClockTickEvent()` — advance the clock by one at the next resume.
- `enqueueTerminationEvent()` — end the run at the next resume.
- `activate(index)`, `deactivate(index)`, `isActive(index)` — whether the dispatcher at that position of the
  composition speaks. A silenced one is carried past, so what it would have settled falls to whatever follows
  it, and to the caller where nothing does; one composition thereby serves several ways of running a model,
  turned over between fetches without rebuilding anything. Only dispatching is withheld, so a silenced
  dispatcher goes on observing and is correct the moment it speaks again. The position is the one the
  dispatcher was composed in, and the queue cannot be silenced.

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
  const choices = [];
  for (;;) {
    const next = JSON.parse(
      controller.getChoiceCandidates(d.instanceId, d.nodeId, JSON.stringify(choices)));
    if (next.complete || !next.attribute) break;
    choices.push(next.enumeration ? next.enumeration[0] : next.lowest);
  }
  controller.enqueueChoiceDecision(JSON.stringify({ instanceId: d.instanceId, nodeId: d.nodeId, choices }));
  engine.resume();
  pending = JSON.parse(controller.getPendingDecisions());
}
```

Composed greedily, `run` proceeds to completion without any of this. `enqueueClockTickEvent` and
`enqueueTerminationEvent` advance or end a run when no decision is pending, which is how a composition
without a clock is carried forward.
