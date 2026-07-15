// WebAssembly drive test. It loads the compiled module in Node and drives the decision-task
// fixture through the same run, stop, and resume model as the native test, confirming that the
// engine actually executes inside WebAssembly: the entry, choice, and exit decisions are
// requested and supplied, the submitted choice is applied, and a stale submission is rejected.

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

const modelXml = readFileSync(join(root, 'test', 'fixtures', 'DecisionTask_with_enumeration.bpmn'), 'utf8');
const instanceCsv =
  'INSTANCE_ID; NODE_ID; INITIALIZATION\n' +
  'Instance_1; Process_1;\n' +
  'Instance_1; Activity_1; x := -2\n';

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

const fullLog = [];
const absorb = (s) => { if (s.log) for (const e of s.log) fullLog.push(e); };
absorb(state);

let submittedChoice = 0;
let entryCount = 0, choiceCount = 0, exitCount = 0, guard = 0;
while (state.pending.length > 0 && guard++ < 50) {
  const request = state.pending[0];
  const decision = { requestId: request.requestId, type: request.type };
  if (request.type === 'entry') entryCount++;
  else if (request.type === 'exit') exitCount++;
  else if (request.type === 'choice') {
    choiceCount++;
    decision.choices = request.choices.map((c) => {
      check(c.enumeration && c.enumeration.length > 0, 'the choice offers an enumeration');
      submittedChoice = c.enumeration[0];
      return c.enumeration[0];
    });
  }
  const accepted = JSON.parse(controller.submitDecision(JSON.stringify(decision)));
  check(!('rejected' in accepted), `submitDecision accepted (${request.type})`);
  state = JSON.parse(engine.resume());
  check(!('error' in state), 'resume');
  absorb(state);
}
check(state.pending.length === 0, 'the engine quiesced with no pending decision');
check(entryCount === 1 && choiceCount === 1 && exitCount === 1,
  'entry, choice, and exit were each driven exactly once');

const applied = fullLog.some((e) =>
  e.token && e.token.nodeId === 'Activity_1' && e.token.state === 'COMPLETED' &&
  e.token.status && e.token.status.choice === submittedChoice);
check(applied, 'the submitted choice was applied on Activity_1 at COMPLETED');

const stale = JSON.parse(controller.submitDecision(JSON.stringify({ requestId: 1, type: 'entry' })));
check('rejected' in stale, 'a consumed request is rejected, not a crash');

console.error('ALL PASSED (WebAssembly)');
