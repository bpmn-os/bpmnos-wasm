// WebAssembly test of a run that continues while the caller acts. The data provider holds time, and the
// choice is left to the caller, so the run waits once the choice is pending. While it waits, the run is
// suspended and the event loop runs this test, which reads the pending decisions, asks for the candidates of
// the choice and enqueues a decision; the run continues with it and ends once nothing is left.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';
import createBPMNOS from '../../dist/bpmnos.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..', '..');

function check(condition, message) {
  if (!condition) {
    console.error(`FAIL: ${message}`);
    process.exit(1);
  }
  console.error(`ok: ${message}`);
}

const module = await createBPMNOS();

const modelXml = readFileSync(join(root, 'test', 'fixtures', 'DecisionTask_with_enumeration.bpmn'), 'utf8');
const instanceCsv =
  'INSTANCE_ID; NODE_ID; INITIALIZATION\n' +
  'Instance_1; Process_1;\n' +
  'Instance_1; Activity_1; x := -2\n';

// The choice is not among the dispatchers, so it reaches the queue and is decided by this test.
const composition = JSON.stringify({
  dispatchers: [ 'EnqueuedEvents', 'FirstFeasibleExit', 'FirstFeasibleEntry', 'InstantDirectMessage' ]
});

const input = new module.Input(modelXml);
input.setInstance(instanceCsv);
const monitor = new module.Monitor();
const controller = new module.Controller(composition);
const engine = new module.Engine(
  input, JSON.stringify({ provider: 'static', clockTickDuration: 'hold' }), controller, monitor);
input.delete();

const log = [];
monitor.addObserver((entryJson) => log.push(JSON.parse(entryJson)));

// The run is started without being awaited; it is suspended whenever the engine waits.
let finished = false;
const running = engine.run(0).then(() => { finished = true; });

// While the run waits, the pending choice can be read from here.
let pending = [];
for (let attempt = 0; attempt < 1000 && pending.length === 0; attempt++) {
  await sleep(1);
  pending = JSON.parse(controller.getPendingDecisions());
}
check(!finished, 'the run continues while it waits for the caller');
check(pending.length === 1 && pending[0].type === 'choice', 'the choice is pending while the run waits');

// The candidates of the choice can be asked for while the run waits.
const request = pending[0];
const choices = [];
for (;;) {
  const next = JSON.parse(
    controller.getChoiceCandidates(request.instanceId, request.nodeId, JSON.stringify(choices)));
  if (next.complete) {
    break;
  }
  check(Array.isArray(next.enumeration) && next.enumeration.length > 0,
    'the candidates of the choice are offered while the run waits');
  choices.push(next.enumeration[0]);
}

// A decision enqueued while the run waits is processed when the run continues.
check(!('rejected' in JSON.parse(controller.enqueueChoiceDecision(
  JSON.stringify({ instanceId: request.instanceId, nodeId: request.nodeId, choices })))),
  'the decision is enqueued while the run waits');

await running;
check(finished, 'the run ends once nothing is left');
check(
  log.some((e) => e.token && e.token.nodeId === 'Activity_1' && e.token.state === 'COMPLETED'
    && e.token.status && e.token.status.choice === choices[0]),
  'the decision enqueued while the run waited was applied');
check(log.at(-1).event && log.at(-1).event.event === 'termination', 'the run ends with a termination event');

engine.delete();
controller.delete();
monitor.delete();
