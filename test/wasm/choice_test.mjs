// WebAssembly choice test. Entry and exit are resolved automatically, so the only decision left for
// the caller is the choice, identified by the token's instance and node. This mirrors the native
// choice test through the module's JavaScript interface.

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

const modelXml = readFileSync(join(root, 'test', 'fixtures', 'DecisionTask_with_enumeration.bpmn'), 'utf8');
const instanceCsv =
  'INSTANCE_ID; NODE_ID; INITIALIZATION\n' +
  'Instance_1; Process_1;\n' +
  'Instance_1; Activity_1; x := -2\n';

const input = new module.Input(modelXml);
input.setInstance(instanceCsv);
const monitor = new module.Monitor();
const controller = new module.Controller(interactive);
const engine = new module.Engine(input, JSON.stringify({ provider: 'static' }), controller, monitor);
input.delete();

const log = [];
monitor.addObserver((entryJson) => log.push(JSON.parse(entryJson)));

engine.run(0);
let pending = JSON.parse(controller.getPendingDecisions());
check(pending.length > 0, 'the engine stopped at the choice');

let enqueuedChoice = 0;
let choiceCount = 0;
let guard = 0;
while (pending.length > 0 && guard++ < 50) {
  check(pending.every((d) => d.type === 'choice'), 'the only pending decision is a choice');
  const request = pending[0];
  choiceCount += 1;
  // The candidates are asked for one choice at a time, each against the values already selected, and the
  // walk ends when the answer says every choice has a value.
  const choices = [];
  for (;;) {
    const next = JSON.parse(
      controller.getChoiceCandidates(request.instanceId, request.nodeId, JSON.stringify(choices)));
    check(!('complete' in next) || next.complete === true, 'the answer is a choice or says it is complete');
    if (next.complete) {
      break;
    }
    check(Array.isArray(next.enumeration) && next.enumeration.length > 0,
      'the choice offers an enumeration of allowed values');
    enqueuedChoice = next.enumeration[0];
    choices.push(enqueuedChoice);
  }
  const decision = {
    instanceId: request.instanceId,
    nodeId: request.nodeId,
    choices,
  };
  check(!('rejected' in JSON.parse(controller.enqueueChoiceDecision(JSON.stringify(decision)))),
    'enqueueChoiceDecision accepted');
  engine.resume();
  pending = JSON.parse(controller.getPendingDecisions());
}

check(pending.length === 0, 'no decision is pending after the choice');
check(choiceCount === 1, 'exactly one choice was made');
check(
  log.some((e) => e.token && e.token.nodeId === 'Activity_1' && e.token.state === 'COMPLETED'
    && e.token.status && e.token.status.choice === enqueuedChoice),
  'the enqueued choice was applied on Activity_1 at COMPLETED');

engine.delete();
controller.delete();
monitor.delete();

// A decision task whose second choice depends on the first: `base <= level <= base + 4` with a discretizer
// of two. A base of two admits the even numbers from two to six; a base of five admits six and eight, the
// lower one being the first multiple of the discretizer at or above the bound rather than the bound itself.
{
  const dependentXml = readFileSync(
    join(root, 'test', 'fixtures', 'DecisionTask_with_dependent_choices.bpmn'), 'utf8');
  const dependentCsv = 'INSTANCE_ID; NODE_ID; INITIALIZATION\nInstance_1; Process_1;\n';

  const dependentInput = new module.Input(dependentXml);
  dependentInput.setInstance(dependentCsv);
  const dependentMonitor = new module.Monitor();
  const dependentController = new module.Controller(interactive);
  const dependentEngine = new module.Engine(
    dependentInput, JSON.stringify({ provider: 'static' }), dependentController, dependentMonitor);
  dependentInput.delete();

  dependentEngine.run(0);

  const [ request ] = JSON.parse(dependentController.getPendingDecisions())
    .filter((decision) => decision.type === 'choice');
  check(!!request, 'the engine stopped at the dependent choice');

  const ask = (selectedValues) => JSON.parse(dependentController.getChoiceCandidates(
    request.instanceId, request.nodeId, JSON.stringify(selectedValues)));

  const first = ask([]);
  check(first.attribute === 'base', 'the first choice is of the base');
  check(JSON.stringify(first.enumeration) === JSON.stringify([ 2, 5 ]),
    'the first choice offers the enumeration of the model');

  const withBaseTwo = ask([ 2 ]);
  check(withBaseTwo.attribute === 'level', 'the second choice is of the level');
  check(withBaseTwo.lowest === 2 && withBaseTwo.highest === 6 && withBaseTwo.multipleOf === 2,
    'a base of two admits two to six by two');

  const withBaseFive = ask([ 5 ]);
  check(withBaseFive.lowest === 6,
    'a base of five admits from six, the first multiple of the discretizer at or above the bound');
  check(withBaseFive.highest === 8,
    'a base of five admits up to eight, the last multiple at or below the bound');
  check(withBaseFive.lowerBound === 5 && withBaseFive.upperBound === 9,
    'and the bounds are reported beside them, neither of them selectable');

  check(ask([ 2, 4 ]).complete === true, 'nothing is offered once every choice has a value');

  check(!('rejected' in JSON.parse(dependentController.enqueueChoiceDecision(JSON.stringify({
    instanceId: request.instanceId, nodeId: request.nodeId, choices: [ 5, 8 ]
  })))), 'a dependent choice is accepted');
  dependentEngine.resume();
  check(JSON.parse(dependentController.getPendingDecisions()).length === 0,
    'no decision is pending after the dependent choice');

  dependentEngine.delete();
  dependentController.delete();
  dependentMonitor.delete();
}

// A bounded choice need not state a discretizer. An integer takes whole values by construction, so the type
// implies a step of one and it is reported; a decimal has nothing implying one and is reported without.
{
  const implicitXml = readFileSync(
    join(root, 'test', 'fixtures', 'DecisionTask_with_implicit_step.bpmn'), 'utf8');

  const implicitInput = new module.Input(implicitXml);
  implicitInput.setInstance('INSTANCE_ID; NODE_ID; INITIALIZATION\nInstance_1; Process_1;\n');
  const implicitMonitor = new module.Monitor();
  const implicitController = new module.Controller(interactive);
  const implicitEngine = new module.Engine(
    implicitInput, JSON.stringify({ provider: 'static' }), implicitController, implicitMonitor);
  implicitInput.delete();

  implicitEngine.run(0);

  const [ request ] = JSON.parse(implicitController.getPendingDecisions())
    .filter((decision) => decision.type === 'choice');
  check(!!request, 'the engine stopped at the choice stating no discretizer');

  const ask = (selectedValues) => JSON.parse(implicitController.getChoiceCandidates(
    request.instanceId, request.nodeId, JSON.stringify(selectedValues)));

  const whole = ask([]);
  check(whole.attribute === 'level', 'the integer choice is offered');
  check(whole.multipleOf === 1, 'an integer states the step its type implies, even where the model does not');
  check(whole.lowest === 1 && whole.highest === 5, 'with the bounds unchanged');
  check(whole.lowest === whole.lowerBound && whole.highest === whole.upperBound,
    'so every bound is selectable and a reader is told of no imprecision');

  const fraction = ask([ 3 ]);
  check(fraction.attribute === 'share', 'the decimal choice is offered');
  check(!('multipleOf' in fraction), 'a decimal states no step, nothing implying one');

  implicitEngine.delete();
  implicitController.delete();
  implicitMonitor.delete();
}

console.error('ALL PASSED (WebAssembly choice)');
