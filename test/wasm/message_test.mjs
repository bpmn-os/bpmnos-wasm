// WebAssembly message delivery test. The message of the assignment problem is not explicitly addressed,
// so its delivery is surfaced to the caller, who names the waiting message by its origin and sender from
// the header. This mirrors the native message test through the module's JavaScript interface.

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
const controller = new module.Controller(interactive);
const engine = new module.Engine(input, JSON.stringify({ provider: 'static' }), controller, monitor);
input.delete();

const log = [];
monitor.addObserver((entryJson) => log.push(JSON.parse(entryJson)));

engine.run(0);
let pending = JSON.parse(controller.getPendingDecisions());
check(pending.length > 0, 'the engine stopped at the message delivery');

// What a caller replaying the stream does: the messages it has seen created and not yet seen disposed of,
// matched against the criterion the delivery request carried. The engine is not asked, and needs not be.
function heldMessages() {
  const held = new Map();

  for (const entry of log) {
    if (!entry.message) {
      continue;
    }
    const key = `${entry.message.origin}|${entry.message.header.sender}`;

    if (entry.message.state === 'CREATED') {
      held.set(key, entry.message);
    } else {
      held.delete(key);
    }
  }

  return [ ...held.values() ];
}

function matches(message, criterion) {
  const expected = criterion.recipientHeader;

  return criterion.senders.includes(message.origin)
    && Object.keys(expected).length === Object.keys(message.header).length
    && Object.entries(expected).every(([ key, value ]) =>
      value === null || message.header[key] === null || message.header[key] === value);
}

let delivered = 0;
let guard = 0;
while (pending.length > 0 && guard++ < 50) {
  const request = pending[0];
  check(request.type === 'messageDelivery', 'the pending decision is a message delivery');

  const criterion = log
    .filter((entry) => entry.messageDeliveryRequest
      && entry.messageDeliveryRequest.instanceId === request.instanceId
      && entry.messageDeliveryRequest.nodeId === request.nodeId)
    .pop();

  check(criterion !== undefined, 'the request was announced as a record');
  check(Array.isArray(criterion.messageDeliveryRequest.senders),
    'carrying the senders the token accepts');
  check(typeof criterion.messageDeliveryRequest.recipientHeader === 'object',
    'and the header it expects');

  const candidates = heldMessages().filter((message) =>
    matches(message, criterion.messageDeliveryRequest));

  check(candidates.length > 0, 'a message held matches what the token waits for');
  const candidate = candidates[0];
  const decision = {
    instanceId: request.instanceId,
    nodeId: request.nodeId,
    origin: candidate.origin,
    sender: candidate.header.sender,
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
