// The demo's engine worker.
//
// The engine's run is a single blocking call, so it runs here in a worker rather than on the page.
// The worker assembles an input from a model in three steps so the page can prompt for a model's lookup
// tables before running: it parses a model and reports which lookup tables it references, accepts each
// lookup table's CSV, and then, once instances are supplied, builds an engine and runs it. During the
// run a monitor posts each observed entry to the page the moment it is recorded, so the log fills in as
// execution proceeds. No controller is attached, so the engine runs autonomously under the greedy
// controller. Because the engine is built once from the input, repeated runs on the same instances
// reuse the parsed model and only advance the scenario, so each run is the next stochastic sample.

import createBpmnos from './bpmnos.mjs';

const ready = createBpmnos();
ready.then(() => self.postMessage({ type: 'ready' }));

let Module = null;
let modelXml = null;
let lookupTables = {};
let engine = null;
let monitor = null;
let scenarioId = 0;
let lastInstances = null;
let entryCount = 0;

function reset() {
  if (monitor) { monitor.delete(); monitor = null; }
  if (engine) { engine.delete(); engine = null; }
  lastInstances = null;
}

// Build a fresh engine from the stored model, lookup tables, and the given instances. The input is
// consumed by construction, so it is assembled and released here each time the instances change.
function buildEngine(instances) {
  reset();
  const input = new Module.Input(modelXml);
  for (const [name, csv] of Object.entries(lookupTables)) {
    input.addLookupTable(name, csv);
  }
  input.setInstance(instances);
  monitor = new Module.Monitor();
  // Each notification is posted the moment the monitor forwards it, from inside the blocking run.
  monitor.addObserver((entry) => { self.postMessage({ type: 'entry', entry }); entryCount += 1; });
  engine = new Module.Engine(input, JSON.stringify({ provider: 'stochastic', seed: 0 }), monitor, null);
  input.delete();
  scenarioId = 0;
  lastInstances = instances;
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
      // A new model starts fresh; discard any engine from a previous model, then report the lookup
      // tables the model references so the page can prompt for exactly those.
      reset();
      modelXml = message.model;
      lookupTables = {};
      const probe = new Module.Input(modelXml);
      const required = JSON.parse(probe.getLookupTableNames());
      probe.delete();
      self.postMessage({ type: 'lookups', required });
      return;
    }

    if (message.type === 'lookup') {
      lookupTables[message.name] = message.csv;
      return;
    }

    if (message.type === 'run') {
      // Rebuild only when the instances change; otherwise reuse the built engine and advance the
      // scenario, so the parsed model is not touched again.
      if (!engine || message.instances !== lastInstances) {
        buildEngine(message.instances);
      } else {
        scenarioId += 1;
      }

      entryCount = 0;
      // Time only the engine's run.
      const startedAt = performance.now();
      engine.run(scenarioId);
      const engineMs = performance.now() - startedAt;

      self.postMessage({
        type: 'done',
        time: engine.getCurrentTime(),
        objective: engine.getWeightedObjective(),
        count: entryCount,
        scenarioId,
        engineMs,
      });
      return;
    }
  } catch (err) {
    self.postMessage({ type: 'error', error: String((err && err.message) || err) });
  }
};
