// WebAssembly choice test. Entry and exit are resolved automatically, so the only decision left for
// the caller is the choice, identified by the token's instance and node. This mirrors the native
// choice test through the module's JavaScript interface.

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

const input = new module.Input(modelXml);
input.setInstance(instanceCsv);
const monitor = new module.Monitor();
const controller = new module.Controller();
const engine = new module.Engine(input, JSON.stringify({ provider: 'static' }), monitor, controller);
input.delete();

const log = [];
monitor.addObserver((entryJson) => log.push(JSON.parse(entryJson)));

engine.run(0);
let pending = JSON.parse(controller.pendingDecisions());
check(pending.length > 0, 'the engine stopped at the choice');

let submittedChoice = 0;
let choiceCount = 0;
let guard = 0;
while (pending.length > 0 && guard++ < 50) {
  check(pending.every((d) => d.type === 'choice'), 'the only pending decision is a choice');
  const request = pending[0];
  choiceCount += 1;
  const choices = [];
  for (const choice of request.choices) {
    check(Array.isArray(choice.enumeration) && choice.enumeration.length > 0,
      'the choice offers an enumeration of allowed values');
    submittedChoice = choice.enumeration[0];
    choices.push(submittedChoice);
  }
  const decision = {
    type: 'choice',
    instanceId: request.instanceId,
    nodeId: request.nodeId,
    choices,
  };
  check(!('rejected' in JSON.parse(controller.submitDecision(JSON.stringify(decision)))), 'submitDecision accepted');
  engine.resume();
  pending = JSON.parse(controller.pendingDecisions());
}

check(pending.length === 0, 'no decision is pending after the choice');
check(choiceCount === 1, 'exactly one choice was made');
check(
  log.some((e) => e.token && e.token.nodeId === 'Activity_1' && e.token.state === 'COMPLETED'
    && e.token.status && e.token.status.choice === submittedChoice),
  'the submitted choice was applied on Activity_1 at COMPLETED');

console.error('ALL PASSED (WebAssembly choice)');
