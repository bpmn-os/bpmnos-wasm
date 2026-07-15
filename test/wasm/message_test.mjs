// WebAssembly message delivery test. It drives the simple messaging fixture, in which one
// instance throws a message and another catches it, confirming that message delivery works
// inside WebAssembly: the waiting token is offered the thrown message as a candidate, delivering
// it applies the message content to the receiver, and both instances complete.

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

const modelXml = readFileSync(join(root, 'test', 'fixtures', 'Simple_messaging.bpmn'), 'utf8');
const instanceCsv =
  'INSTANCE_ID; NODE_ID; INITIALIZATION\n' +
  'Instance_1; Process_1; timestamp := 0\n' +
  'Instance_2; Process_2; timestamp := 0\n';

const engine = new module.Engine();
const monitor = new module.Monitor();
const controller = new module.Controller();
engine.attachMonitor(monitor);
engine.attachController(controller);

engine.loadModel(modelXml);
engine.loadInstances(instanceCsv);
engine.configure(JSON.stringify({ provider: 'static' }));

let state = JSON.parse(engine.start());
check(!('error' in state), 'start');

const fullLog = [];
const absorb = (s) => { if (s.log) for (const e of s.log) fullLog.push(e); };
absorb(state);

let sawCandidate = false, madeDelivery = false, guard = 0;
while (guard++ < 100) {
  if (state.pending.length === 0) break;
  let acted = false;
  for (const request of state.pending) {
    const decision = { requestId: request.requestId, type: request.type };
    if (request.type === 'messageDelivery') {
      if (request.candidates.length === 0) continue;
      sawCandidate = true;
      decision.messageId = request.candidates[0].messageId;
    }
    const accepted = JSON.parse(controller.submitDecision(JSON.stringify(decision)));
    if ('rejected' in accepted) continue;
    if (request.type === 'messageDelivery') madeDelivery = true;
    state = JSON.parse(engine.resume());
    absorb(state);
    acted = true;
    break;
  }
  if (!acted) break;
}

check(sawCandidate, 'the waiting token was offered the thrown message as a candidate');
check(madeDelivery, 'a message delivery decision was driven');
check(fullLog.some((e) => e.message && e.message.state === 'DELIVERED'), 'the message was delivered');
check(state.pending.length === 0, 'the engine quiesced with no pending decision');

console.error('ALL PASSED (WebAssembly message delivery)');
