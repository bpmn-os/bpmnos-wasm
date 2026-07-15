// WebAssembly autonomous run test. With no controller attached the engine runs itself under the
// greedy controller with the guided evaluator, exactly as the engine's own greedy application does.
// This drives the decision-task fixture to completion and checks that the run terminates, that the
// monitor captured a log, and that the outcome and objective are reported.

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
engine.attachMonitor(monitor);

check(!('error' in JSON.parse(engine.loadModel(modelXml))), 'loadModel');
check(!('error' in JSON.parse(engine.loadInstances(instanceCsv))), 'loadInstances');
engine.configure(JSON.stringify({ provider: 'static' }));

const snapshot = JSON.parse(engine.start());
check(!('error' in snapshot), 'start (autonomous greedy run)');
check(snapshot.outcome === 'COMPLETED', `outcome is COMPLETED (got ${snapshot.outcome})`);
check(typeof snapshot.objective === 'number', 'an objective is reported');
check(Array.isArray(snapshot.log) && snapshot.log.length > 0, 'the monitor captured a log');
check(snapshot.log.some((e) => e.token && e.token.nodeId === 'Activity_1' && e.token.state === 'COMPLETED'),
  'the decision task completed');
check(snapshot.pending.length === 0, 'no decision is pending after an autonomous run');

console.error(`outcome ${snapshot.outcome}, objective ${snapshot.objective}, ${snapshot.log.length} log entries`);
console.error('ALL PASSED (WebAssembly autonomous greedy run)');
