// WebAssembly clock tick test. With an interactive controller and a data provider holding time the engine
// runs to the timer and waits, and the caller advances the clock one tick at a time until the timer fires
// and the run ends. This mirrors the native timer test through the module's JavaScript interface.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import createBPMNOS from '../../dist/bpmnos.mjs';
import { greedy, makeInteractive, held, Driver } from './composition.mjs';

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

const input = new module.Input(modelXml);
input.setInstance(instanceCsv);
const monitor = new module.Monitor();
const controller = makeInteractive(new module.Controller(greedy));
const engine = new module.Engine(input, held, controller, monitor);
const driver = new Driver(engine, monitor);
input.delete();

const log = [];
monitor.addObserver((entryJson) => log.push(JSON.parse(entryJson)));

await driver.run(0);
check(JSON.parse(controller.getPendingDecisions()).length === 0, 'no decision is pending; the timer waits for the clock');
check(engine.isAlive(), 'the system is alive, waiting for the timer');

let ticks = 0;
let guard = 0;
while (engine.isAlive() && guard++ < 20) {
  controller.enqueueClockTickEvent();
  const previousTime = engine.getCurrentTime();
  await driver.proceed();
  check(engine.getCurrentTime() === previousTime + 1, 'a clock tick advances time by one');
  ticks += 1;
}

check(!engine.isAlive(), 'the process terminated after the timer fired');
check(log.some((e) => e.event && e.event.event === 'clocktick'), 'the clock ticks appear in the log');
check(log.some((e) => e.token && e.token.nodeId === 'EndEvent_1'), 'the token reached the end event');

console.error(`terminated after ${ticks} clock ticks, final time ${engine.getCurrentTime()}`);
console.error('ALL PASSED (WebAssembly clock tick)');
