// WebAssembly message delivery test. The message of the assignment problem is not explicitly addressed,
// so its delivery is surfaced to the caller, who names the waiting message by its origin and sender from
// the header. This mirrors the native message test through the module's JavaScript interface.

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

const modelXml = readFileSync(join(root, 'test', 'fixtures', 'Assignment_problem.bpmn'), 'utf8');
const costsCsv = readFileSync(join(root, 'test', 'fixtures', 'costs.csv'), 'utf8');
const instanceCsv =
  'INSTANCE_ID; NODE_ID; INITIALIZATION\n' +
  'Client1; ClientProcess;\n' +
  'Server1; ServerProcess;\n';

const input = new module.Input(modelXml);
const required = JSON.parse(input.getLookupTableNames());
check(Array.isArray(required) && required.length === 1 && required[0] === 'costs.csv',
  "getLookupTableNames reports the model's lookup table");
input.addLookupTable('costs.csv', costsCsv);
input.setInstance(instanceCsv);
const monitor = new module.Monitor();
const controller = new module.Controller();
const engine = new module.Engine(input, JSON.stringify({ provider: 'static' }), monitor, controller);
input.delete();

const log = [];
monitor.addObserver((entryJson) => log.push(JSON.parse(entryJson)));

engine.run(0);
let pending = JSON.parse(controller.getPendingDecisions());
check(pending.length > 0, 'the engine stopped at the message delivery');

let delivered = 0;
let guard = 0;
while (pending.length > 0 && guard++ < 50) {
  const request = pending[0];
  check(request.type === 'messageDelivery', 'the pending decision is a message delivery');
  const candidates = JSON.parse(controller.getMessageCandidates(request.instanceId, request.nodeId));
  check(candidates.length > 0, 'the delivery offers at least one candidate message');
  const candidate = candidates[0];
  const decision = {
    instanceId: request.instanceId,
    nodeId: request.nodeId,
    origin: candidate.origin,
    sender: candidate.sender,
  };
  check(!('rejected' in JSON.parse(controller.enqueueMessageDeliveryDecision(JSON.stringify(decision)))),
    'enqueueMessageDeliveryDecision accepted');
  delivered += 1;
  engine.resume();
  pending = JSON.parse(controller.getPendingDecisions());
}

check(delivered === 1, 'exactly one message was delivered');

const completed = new Set(
  log.filter((e) => e.token && e.token.state === 'COMPLETED').map((e) => e.token.nodeId));
check(completed.has('SendRequestTask'), 'the send task completed');
check(completed.has('ReceiveRequestTask'), 'the receive task completed');

console.error('ALL PASSED (WebAssembly message delivery)');
