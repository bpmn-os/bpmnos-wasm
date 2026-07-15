// WebAssembly message delivery test. The message of the assignment problem is not explicitly addressed,
// so its delivery is surfaced to the caller, who names the waiting message by its origin and sender from
// the header. This mirrors the native message test through the module's JavaScript interface.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import createBpmnos from '../../dist/bpmnos.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..', '..');

function check(condition, message) {
  if (!condition) {
    console.error(`FAIL: ${message}`);
    process.exit(1);
  }
  console.error(`ok: ${message}`);
}

const module = await createBpmnos();

const modelXml = readFileSync(join(root, 'test', 'fixtures', 'Assignment_problem.bpmn'), 'utf8');
const costsCsv = readFileSync(join(root, 'test', 'fixtures', 'costs.csv'), 'utf8');
const instanceCsv =
  'INSTANCE_ID; NODE_ID; INITIALIZATION\n' +
  'Client1; ClientProcess;\n' +
  'Server1; ServerProcess;\n';

const engine = new module.Engine();
const monitor = new module.Monitor();
const controller = new module.Controller();
engine.attachMonitor(monitor);
engine.attachController(controller);

check(!('error' in JSON.parse(engine.loadModel(modelXml))), 'loadModel');
const required = JSON.parse(engine.requiredLookups());
check(Array.isArray(required) && required.length === 1 && required[0] === 'costs.csv',
  "requiredLookups reports the model's lookup table");
check(!('error' in JSON.parse(engine.loadLookupTable('costs.csv', costsCsv))), 'loadLookupTable');
check(!('error' in JSON.parse(engine.loadInstances(instanceCsv))), 'loadInstances');
engine.configure(JSON.stringify({ provider: 'static' }));

let state = JSON.parse(engine.start());
check(!('error' in state), 'start');
check(state.pending.length > 0, 'the engine stopped at the message delivery');

const log = [...state.log];
let delivered = 0;
let guard = 0;
while (state.pending.length > 0 && guard++ < 50) {
  const request = state.pending[0];
  check(request.type === 'messageDelivery', 'the pending decision is a message delivery');
  check(request.candidates.length > 0, 'the delivery offers at least one candidate message');
  const candidate = request.candidates[0];
  const decision = {
    type: 'messageDelivery',
    instanceId: request.instanceId,
    nodeId: request.nodeId,
    origin: candidate.origin,
    sender: candidate.sender,
  };
  check(!('rejected' in JSON.parse(controller.submitDecision(JSON.stringify(decision)))), 'submitDecision accepted');
  delivered += 1;
  state = JSON.parse(engine.resume());
  check(!('error' in state), 'resume');
  log.push(...state.log);
}

check(delivered === 1, 'exactly one message was delivered');

const completed = new Set(
  log.filter((e) => e.token && e.token.state === 'COMPLETED').map((e) => e.token.nodeId));
check(completed.has('SendRequestTask'), 'the send task completed');
check(completed.has('ReceiveRequestTask'), 'the receive task completed');

console.error('ALL PASSED (WebAssembly message delivery)');
