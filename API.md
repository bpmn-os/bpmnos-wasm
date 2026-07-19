# JavaScript API

Instantiate the module, then use its four classes. Structured values cross the boundary as JSON strings;
scalars, identifiers, and CSV text cross as native numbers and strings. Instances are C++ objects; call
`delete()` on each when done. The type declarations are in `types/bpmnos.d.ts`.

```js
import createBpmnos from './bpmnos.mjs';
const module = await createBpmnos();
```

## Input

Assembles a run's inputs. It is consumed when an Engine is built from it, so one Input builds one Engine.

- `new Input(bpmnXml: string)` — parse the model.
- `requiredLookupTables(): string` — a JSON array of the lookup table source names the model references.
- `addLookupTable(name: string, csv: string)` — supply one lookup table by its source name.
- `setInstance(csv: string)` — supply the instance data.

## Engine

- `new Engine(input: Input, configJson: string, monitor: Monitor, controller: Controller | null)` —
  `configJson` is `{"provider": "static"|"expected"|"dynamic"|"stochastic", "seed": n}`, each field
  optional. A null controller runs the engine autonomously under the greedy controller.
- `run(scenarioId: number)` — draw the scenario and run from the start. Repeatable without reparsing;
  with the stochastic provider a different scenario id is a different sample.
- `resume()` — continue the run.
- `isAlive(): boolean` — whether the system may still proceed; a run is done once it is false.
- `getCurrentTime(): number` — the current simulated time.

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

Attaching a controller makes execution interactive. It resolves the first feasible exit, the first
feasible non-sequential entry, and the directly addressed message delivery itself; the choice, the
sequential ad hoc entry, and the ambiguous message delivery are left to the caller.

- `getPendingDecisions(): string` — `[{"type": "entry"|"exit"|"choice"|"messageDelivery", "instanceId":
  s, "nodeId": s}]`.
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
- `enqueueClockTick()` — advance the clock by one at the next resume.
- `enqueueTermination()` — end the run at the next resume.

Each `enqueue…` returns `{"queued": true}` or `{"rejected": reason}`. A decision names its token by
instance and node, and a message by its origin and sender; an enqueued decision whose token, request, or
message has expired is silently dropped.

## Driving

```js
const input = new module.Input(bpmnXml);
for (const name of JSON.parse(input.requiredLookupTables())) input.addLookupTable(name, lookup[name]);
input.setInstance(instanceCsv);

const monitor = new module.Monitor();
monitor.addObserver(entryJson => log(JSON.parse(entryJson)));
const controller = new module.Controller();
const engine = new module.Engine(input, JSON.stringify({ provider: 'static' }), monitor, controller);

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

Without a controller, `run` proceeds to completion on its own. `enqueueClockTick` and `enqueueTermination`
advance a controller-driven run when no decision is pending.
