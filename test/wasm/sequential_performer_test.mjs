// WebAssembly sequential performer test. What each performer conducts and what waits for it is read from
// the engine's own bookkeeping rather than from the pending decisions, and this checks the shape the
// bindings report it in. It mirrors the native sequential performer test through the module's JavaScript
// interface, on the fixture whose first activity is multi-instance and has a duration, so that a performer
// is caught while it conducts a child rather than between two.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import createBPMNOS from '../../dist/bpmnos.mjs';
import { interactive } from './composition.mjs';

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

const modelXml = readFileSync(join(root, 'test', 'fixtures', 'AdHocSubProcess_multi_instance.bpmn'), 'utf8');
const instanceCsv =
  'INSTANCE_ID; NODE_ID; INITIALIZATION\n' +
  'Instance_1; Process_1;\n';

const input = new module.Input(modelXml);
input.setInstance(instanceCsv);
const monitor = new module.Monitor();
const controller = new module.Controller(interactive);
const engine = new module.Engine(input, JSON.stringify({ provider: 'static' }), controller, monitor);
input.delete();

engine.run(0);

let performers = JSON.parse(controller.getSequentialPerformers());
check(performers.length === 1, 'one performer is reported');

const [ performer ] = performers;
check(performer.performer.nodeId === 'AdHocSubProcess_1', 'the ad-hoc subprocess performs for its own children');
check(performer.performer.instanceId === 'Instance_1', 'and its token is the one standing there');
check(performer.performing === null, 'it is conducting nothing');
check(performer.waiting.length === 3, 'both instances of the first activity and the second activity wait');

const instances = performer.waiting.filter((token) => token.nodeId === 'Activity_1');
check(instances.length === 2, 'the multi-instance activity queues by its instances');
check(new Set(instances.map((token) => token.instanceId)).size === 2, 'each under an identity of its own');
check(instances.every((token) => token.instanceId !== 'Instance_1'),
  'and the token of the activity itself queues for nothing, never becoming ready');
check(performer.waiting.some((token) => token.nodeId === 'Activity_2'), 'the plain activity is among them');

const [ request ] = JSON.parse(controller.getPendingDecisions());
check(request && request.type === 'entry', 'the engine asks for an entry');
check(!('rejected' in JSON.parse(controller.enqueueEntryDecision(
  JSON.stringify({ instanceId: request.instanceId, nodeId: request.nodeId })))),
  'the entry is enqueued');
engine.resume();

performers = JSON.parse(controller.getSequentialPerformers());
check(performers.length === 1, 'the performer is still reported');
check(performers[0].performing !== null, 'and is conducting a token');
check(performers[0].performing.instanceId === request.instanceId
  && performers[0].performing.nodeId === request.nodeId,
  'which is the one that was entered');
check(performers[0].waiting.length === 2, 'the rest still waits');
check(JSON.parse(controller.getPendingDecisions()).length === 0,
  'and is asked for nothing while the performer is busy');

console.error('ALL PASSED (WebAssembly sequential performer)');
