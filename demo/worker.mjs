// The demo's engine worker.
//
// The engine's run is a single blocking call, so it runs here in a worker rather than on the page.
// A monitor is attached with a live callback that posts each observed entry to the page as it is
// recorded, before the run returns, so the log fills in as execution proceeds instead of only at
// the end. No controller is attached, so the engine runs autonomously under the greedy controller.

import createBpmnos from './bpmnos.mjs';

const ready = createBpmnos();
ready.then(() => self.postMessage({ type: 'ready' }));

self.onmessage = async (event) => {
  const { model, instances, seed } = event.data;

  let Module;
  try {
    Module = await ready;
  } catch (err) {
    self.postMessage({ type: 'error', error: 'module failed to load: ' + String(err) });
    return;
  }

  let engine;
  let monitor;
  try {
    engine = new Module.Engine();
    monitor = new Module.Monitor();
    engine.attachMonitor(monitor);
    // Each notification is posted the moment the monitor records it, from inside the blocking run.
    monitor.onNotice((entry) => self.postMessage({ type: 'entry', entry }));

    let result = JSON.parse(engine.loadModel(model));
    if (result.error) throw new Error(result.error);
    result = JSON.parse(engine.loadInstances(instances));
    if (result.error) throw new Error(result.error);
    engine.configure(JSON.stringify({ provider: 'stochastic', seed }));

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
      seed,
      engineMs,
    });
  } catch (err) {
    self.postMessage({ type: 'error', error: String((err && err.message) || err) });
  } finally {
    if (monitor) monitor.delete();
    if (engine) engine.delete();
  }
};
