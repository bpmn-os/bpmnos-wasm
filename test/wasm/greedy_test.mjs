// WebAssembly autonomous run test. With no controller attached the engine runs itself under the
// greedy controller with the guided evaluator, exactly as the engine's own greedy application does.
// This drives the decision-task fixture to completion and checks that the run terminates and that the
// monitor captured a log recording the decision task's completion.

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
const engine = new module.Engine(input, JSON.stringify({ provider: 'static' }), monitor, null);
input.delete();

const log = [];
monitor.addObserver((entryJson) => log.push(JSON.parse(entryJson)));

engine.run(0);
check(!engine.isAlive(), 'the autonomous run terminated');
check(Array.isArray(log) && log.length > 0, 'the monitor captured a log');
check(log.some((e) => e.token && e.token.nodeId === 'Activity_1' && e.token.state === 'COMPLETED'),
  'the decision task completed');

const objective = engine.getWeightedObjective();
check(typeof objective === 'number' && Number.isFinite(objective),
  'the engine reports a finite weighted objective');

console.error(`${log.length} log entries, final time ${engine.getCurrentTime()}, objective ${objective}`);
console.error('ALL PASSED (WebAssembly autonomous greedy run)');
