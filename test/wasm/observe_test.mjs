// WebAssembly live observation test. The monitor is a stateless fan-out: it forwards each entry, the
// moment it is recorded, to every registered observer, in the engine's execution order. This drives the
// decision-task fixture to completion and checks that several observers each receive the same entries in
// the same order, that decision requests are forwarded alongside token, event, and message records, and
// that an observer attached after a run misses that run because the monitor keeps no history. The demo's
// worker relies on this to stream the log to the page as execution proceeds.

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

// Two observers, each collecting the stream independently, in the order it arrives.
const first = [];
const second = [];
monitor.addObserver((entryJson) => first.push(entryJson));
monitor.addObserver((entryJson) => second.push(entryJson));

engine.run(0);

check(first.length > 0, `the first observer received entries (${first.length})`);
check(first.length === second.length, 'both observers received the same number of entries');
check(JSON.stringify(first) === JSON.stringify(second),
  'both observers received the same entries in the same order');

// Each entry is a single-key object of a known kind, including the decision requests.
const kinds = new Set([
  'token', 'event', 'message',
  'entryRequest', 'exitRequest', 'choiceRequest', 'messageDeliveryRequest',
]);
const parsed = first.map((entry) => JSON.parse(entry));
check(parsed.every((entry) => Object.keys(entry).length === 1 && kinds.has(Object.keys(entry)[0])),
  'every entry is a single token, event, message, or decision request record');
check(parsed.some((entry) => Object.keys(entry)[0].endsWith('Request')),
  'a decision request is forwarded alongside the token, event, and message records');

// The monitor keeps no history, so an observer attached after a run misses that run; a fresh run reaches
// it, which the reusable run lets us verify on the same engine.
const late = [];
monitor.addObserver((entryJson) => late.push(entryJson));
check(late.length === 0, 'an observer attached after the run received nothing from it');
engine.run(1);
check(late.length > 0, 'the late observer receives the next run');

monitor.delete();
engine.delete();

console.error(`fanned ${first.length} entries to each observer, in order`);
console.error('ALL PASSED (WebAssembly live observation)');
