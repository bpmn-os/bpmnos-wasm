// WebAssembly clock tick test. With an interactive controller and no time handler the engine runs to
// the timer and stops, and the caller advances the clock one tick at a time until the timer fires and
// the process terminates. This mirrors the native timer test through the module's JavaScript interface.

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

const modelXml = readFileSync(join(root, 'test', 'fixtures', 'Timer.bpmn'), 'utf8');
const instanceCsv =
  'INSTANCE_ID; NODE_ID; INITIALIZATION\n' +
  'Instance_1; Process_1; trigger := 3\n';

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
check(state.pending.length === 0, 'no decision is pending; the timer waits for the clock');
check(state.alive === true, 'the system is alive, waiting for the timer');

const log = [...state.log];
let ticks = 0;
let guard = 0;
while (state.alive && guard++ < 20) {
  controller.submitClockTick();
  const previousTime = state.time;
  state = JSON.parse(engine.resume());
  check(!('error' in state), 'resume after a clock tick');
  check(state.time === previousTime + 1, 'a clock tick advances time by one');
  log.push(...state.log);
  ticks += 1;
}

check(state.alive === false, 'the process terminated after the timer fired');
check(log.some((e) => e.event && e.event.event === 'clocktick'), 'the clock ticks appear in the log');
check(log.some((e) => e.token && e.token.nodeId === 'EndEvent_1'), 'the token reached the end event');

console.error(`terminated after ${ticks} clock ticks, final time ${state.time}`);
console.error('ALL PASSED (WebAssembly clock tick)');
