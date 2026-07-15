// WebAssembly live observation test. The monitor's onNotice callback delivers each entry the moment
// it is recorded, during the engine's run, rather than only through a drain afterwards. This drives
// the decision-task fixture to completion and checks that the callback fired once per entry, in the
// same order and with the same content as the drained log, which is what the demo's worker relies on
// to stream the log to the page as execution proceeds.

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

// Collect every entry the callback delivers, in the order it arrives.
const streamed = [];
monitor.onNotice((entryJson) => streamed.push(entryJson));

check(!('error' in JSON.parse(engine.loadModel(modelXml))), 'loadModel');
check(!('error' in JSON.parse(engine.loadInstances(instanceCsv))), 'loadInstances');
engine.configure(JSON.stringify({ provider: 'static' }));

const snapshot = JSON.parse(engine.start());
check(!('error' in snapshot), 'start');

check(streamed.length > 0, `the callback fired (${streamed.length} entries)`);
check(streamed.length === snapshot.log.length,
  `one callback per log entry (streamed ${streamed.length}, log ${snapshot.log.length})`);

// Each streamed entry is a JSON object carrying exactly one of the three notification kinds.
const kinds = new Set(['token', 'event', 'message']);
const parsed = streamed.map((entry) => JSON.parse(entry));
check(parsed.every((entry) => Object.keys(entry).length === 1 && kinds.has(Object.keys(entry)[0])),
  'every streamed entry is a single token, event, or message record');

// The stream is exactly the drained log, in order.
check(JSON.stringify(parsed) === JSON.stringify(snapshot.log),
  'the streamed entries equal the drained log, in order');

// Passing null removes the sink. A fresh run on a new engine with the same monitor, after clearing,
// delivers no further callbacks even though it records a log.
monitor.onNotice(null);
const before = streamed.length;
const engine2 = new module.Engine();
engine2.attachMonitor(monitor);
engine2.loadModel(modelXml);
engine2.loadInstances(instanceCsv);
engine2.configure(JSON.stringify({ provider: 'static' }));
const snapshot2 = JSON.parse(engine2.start());
check(streamed.length === before, 'no callbacks fire after the sink is cleared');
check(snapshot2.log.length > 0, 'the second run still recorded a log');

monitor.delete();
engine.delete();
engine2.delete();

console.error(`streamed ${streamed.length} entries live, matching the drained log`);
console.error('ALL PASSED (WebAssembly live observation)');
