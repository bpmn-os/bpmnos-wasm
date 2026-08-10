// WebAssembly test for silencing a dispatcher of a composition. One composition serves several ways of
// running a model: a run is driven by whichever of its dispatchers answer, and that is turned over between
// fetches rather than by rebuilding anything. With the dispatcher that settles choices silenced, the run
// waits at the choice; letting it speak again settles it. Only dispatching is withheld, so the silenced
// dispatcher goes on observing and is correct the moment it is activated.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
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

// The composition is stated here rather than taken from composition.mjs, so that the positions this test
// switches are the ones it states and no reordering elsewhere can move them. The entry and the exit are
// settled by their dispatchers, so the run reaches the choice; the choice dispatcher is what is silenced;
// the queue is what the caller would answer through.
const composition = JSON.stringify({
  dispatchers: [ 'EnqueuedEvents', 'FirstFeasibleExit', 'FirstFeasibleEntry', 'FirstEnumeratedChoice' ]
});
const enqueuedEvents = 0;
const firstEnumeratedChoice = 3;
const beyondTheComposition = 4;

const input = new module.Input(modelXml);
input.setInstance(instanceCsv);
const monitor = new module.Monitor();
const controller = new module.Controller(composition);
const engine = new module.Engine(input, JSON.stringify({ provider: 'static' }), controller, monitor);
input.delete();

const log = [];
monitor.addObserver((entryJson) => log.push(JSON.parse(entryJson)));

const choicePending = () =>
  JSON.parse(controller.getPendingDecisions()).some((decision) => decision.type === 'choice');

check(controller.isActive(firstEnumeratedChoice), 'every dispatcher speaks in a composition as composed');

controller.deactivate(firstEnumeratedChoice);
check(!controller.isActive(firstEnumeratedChoice), 'the dispatcher reports that it is silenced');

engine.initialize(0);
while (engine.advance() && !choicePending()) {
  // carry the run forward until it can go no further or the choice is left to the caller
}
check(choicePending(), 'with the choice dispatcher silenced the run waits at the choice');

controller.activate(firstEnumeratedChoice);
check(controller.isActive(firstEnumeratedChoice), 'the dispatcher reports that it speaks again');

let guard = 0;
while (engine.advance() && guard++ < 10000) {
  // the reactivated dispatcher answers from candidates it kept while silent
}
check(guard < 10000, 'the run ended rather than standing still');
check(!choicePending(), 'the reactivated dispatcher settled the choice it had left');
check(log.some((e) => e.token && e.token.nodeId === 'Activity_1' && e.token.state === 'COMPLETED'),
  'the decision task completed');

// The queue is what the caller says, so a run that never dispatched it would ignore every decision, clock
// tick and termination it is given.
let refusedQueue = false;
try {
  controller.deactivate(enqueuedEvents);
} catch {
  refusedQueue = true;
}
check(refusedQueue, 'the queue cannot be silenced');

let refusedRange = false;
try {
  controller.deactivate(beyondTheComposition);
} catch {
  refusedRange = true;
}
check(refusedRange, 'a position the composition does not hold is refused');

engine.delete();
controller.delete();
monitor.delete();

console.error('ALL PASSED (WebAssembly silencing a dispatcher)');
