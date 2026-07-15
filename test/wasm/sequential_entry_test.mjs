// WebAssembly sequential entry test. The children of a sequential ad-hoc subprocess are entered one at
// a time by the caller, while every other entry is resolved automatically. This mirrors the native
// sequential entry test through the module's JavaScript interface.

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

const modelXml = readFileSync(join(root, 'test', 'fixtures', 'AdHocSubProcess.bpmn'), 'utf8');
const instanceCsv =
  'INSTANCE_ID; NODE_ID; INITIALIZATION\n' +
  'Instance_1; Process_1;\n';

const engine = new module.Engine();
const monitor = new module.Monitor();
const controller = new module.Controller();
engine.attachMonitor(monitor);
engine.attachController(controller);

check(!('error' in JSON.parse(engine.loadModel(modelXml))), 'loadModel');
check(!('error' in JSON.parse(engine.loadInstances(instanceCsv))), 'loadInstances');
engine.configure(JSON.stringify({ provider: 'static' }));

let state = JSON.parse(engine.start());
check(!('error' in state), 'start');
check(state.pending.length > 0, 'the engine stopped at a sequential entry');

const log = [...state.log];
let entered = 0;
let guard = 0;
while (state.pending.length > 0 && guard++ < 50) {
  const request = state.pending[0];
  check(request.type === 'entry', 'the pending decision is a sequential entry');
  const decision = { type: 'entry', instanceId: request.instanceId, nodeId: request.nodeId };
  check(!('rejected' in JSON.parse(controller.submitDecision(JSON.stringify(decision)))), 'submitDecision accepted');
  entered += 1;
  state = JSON.parse(engine.resume());
  check(!('error' in state), 'resume');
  log.push(...state.log);
}

check(state.pending.length === 0, 'no decision is pending after the sequential entries');
check(entered === 2, 'both ad-hoc children were entered');

const completed = new Set(
  log.filter((e) => e.token && e.token.state === 'COMPLETED').map((e) => e.token.nodeId));
check(completed.has('Activity_1') && completed.has('Activity_2'), 'both ad-hoc children completed');
check(completed.has('AdHocSubProcess_1'), 'the ad-hoc subprocess completed');

console.error('ALL PASSED (WebAssembly sequential entry)');
