// The demo's engine worker.
//
// The engine's run is a single blocking call, so it runs here in a worker rather than on the page.
// The worker keeps one engine across three steps so the page can prompt for a model's lookup tables
// before running: it loads a model and reports which lookup tables it references, accepts each lookup
// table's CSV, and then runs. During the run a monitor posts each observed entry to the page the moment
// it is recorded, so the log fills in as execution proceeds. No controller is attached, so the engine
// runs autonomously under the greedy controller.

import createBpmnos from './bpmnos.mjs';

const ready = createBpmnos();
ready.then(() => self.postMessage({ type: 'ready' }));

let Module = null;
let engine = null;
let monitor = null;

function reset() {
  if (monitor) { monitor.delete(); monitor = null; }
  if (engine) { engine.delete(); engine = null; }
}

self.onmessage = async (event) => {
  const message = event.data;
  try {
    Module = Module || await ready;
  } catch (err) {
    self.postMessage({ type: 'error', error: 'module failed to load: ' + String(err) });
    return;
  }

  try {
    if (message.type === 'loadModel') {
      // A new model starts fresh; discard any engine from a previous model.
      reset();
      engine = new Module.Engine();
      monitor = new Module.Monitor();
      engine.attachMonitor(monitor);
      const result = JSON.parse(engine.loadModel(message.model));
      if (result.error) throw new Error(result.error);
      const required = JSON.parse(engine.requiredLookups());
      self.postMessage({ type: 'lookups', required });
      return;
    }

    if (message.type === 'lookup') {
      const result = JSON.parse(engine.loadLookupTable(message.name, message.csv));
      if (result.error) throw new Error(result.error);
      return;
    }

    if (message.type === 'run') {
      let result = JSON.parse(engine.loadInstances(message.instances));
      if (result.error) throw new Error(result.error);
      engine.configure(JSON.stringify({ provider: 'stochastic', seed: message.seed }));
      // Each notification is posted the moment the monitor records it, from inside the blocking run.
      monitor.onNotice((entry) => self.postMessage({ type: 'entry', entry }));

      // Time only the engine's run, before the snapshot JSON is parsed back on this side.
      const startedAt = performance.now();
      const raw = engine.start();
      const engineMs = performance.now() - startedAt;
      const snapshot = JSON.parse(raw);
      if (snapshot.error) throw new Error(snapshot.error);

      self.postMessage({
        type: 'done',
        outcome: snapshot.outcome,
        objective: snapshot.objective,
        time: snapshot.time,
        count: snapshot.log.length,
        seed: message.seed,
        engineMs,
      });
      return;
    }
  } catch (err) {
    self.postMessage({ type: 'error', error: String((err && err.message) || err) });
  }
};
