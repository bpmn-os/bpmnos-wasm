// WebAssembly test for driving a run one event at a time. A caller that draws what a run produces must not
// be left behind by it: the greedy composition settles everything itself, so one call to run carries a model
// to its end before anything can be drawn. initialize prepares a run without advancing it, and advance
// carries it forward by a single fetch, so the caller is never more than one event ahead of the records it
// has received. What is checked is that this changes nothing but the pacing.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import createBPMNOS from '../../dist/bpmnos.mjs';
import { greedy } from './composition.mjs';

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

const modelXml = readFileSync(join(root, 'test', 'fixtures', 'Timer.bpmn'), 'utf8');
const instanceCsv =
  'INSTANCE_ID; NODE_ID; INITIALIZATION\n' +
  'Instance_1; Process_1; trigger := 3\n';

// A run is repeatable on one engine and draws the same scenario from the same provider, so the two runs
// below are the same run driven differently. The timer fixture cannot finish without a clock, so the
// comparison covers a run whose events include clock ticks rather than only token movement.
const input = new module.Input(modelXml);
input.setInstance(instanceCsv);
const monitor = new module.Monitor();
const controller = new module.Controller(greedy);
const engine = new module.Engine(input, JSON.stringify({ provider: 'static' }), controller, monitor);
input.delete();

let log = [];
monitor.addObserver((entryJson) => log.push(JSON.parse(entryJson)));

engine.run(0);
const expected = log;
const expectedAlive = engine.isAlive();
const expectedTime = engine.getCurrentTime();
const expectedObjective = engine.getObjective();
check(expected.length > 0, 'the reference run produced records');

log = [];
engine.initialize(0);

// initialize advances time to the run's first instant, and instantiation is what reaching an instant
// causes, so it produces the opening tick and the instances due at it, and stops before the first fetch.
check(log.length > 0 && log[0].event && log[0].event.event === 'clocktick',
  'the stream opens with the clock tick that begins the run');
check(log.length < expected.length, 'initialize does not carry the run to its end');
check(log.every((entry, index) => JSON.stringify(entry) === JSON.stringify(expected[index])),
  'what initialize produced is the beginning of the same stream');

let advances = 0;
while (engine.advance()) {
  ++advances;
}

check(advances > 0, 'the run was carried forward one event at a time');
check(JSON.stringify(log) === JSON.stringify(expected),
  'advancing event by event produces the record stream that run produces');
check(engine.isAlive() === expectedAlive, 'it ends in the same liveness');
check(engine.getCurrentTime() === expectedTime, 'it ends at the same time');
check(engine.getObjective() === expectedObjective, 'it ends with the same objective');

engine.delete();
controller.delete();
monitor.delete();

console.error(`${advances} advances produced ${log.length} records, matching the ${expected.length} of one run`);
console.error('ALL PASSED (WebAssembly advancing one event at a time)');
