// Type declarations for the bpmnos-wasm module.
//
// The module exposes three classes bound through embind. Every value that the engine expresses as
// JSON crosses the boundary as a JSON string, so each method below takes or returns a string that
// the caller parses or produces. The JSON shapes are described in the project README and roadmap.
// Instances are C++ objects; call delete on them when finished to release their memory.

export interface Engine {
  /** Attach a monitor before starting; the caller owns it and keeps it alive. */
  attachMonitor(monitor: Monitor): void;
  /** Attach a controller to supply decisions; omit it to run autonomously under the greedy controller. */
  attachController(controller: Controller): void;

  /** Load the BPMN model XML. Returns {"ok":true} or {"error":message}. */
  loadModel(bpmnXml: string): string;
  /** Load one lookup table by name. Returns {"ok":true} or {"error":message}. */
  loadLookupTable(name: string, csv: string): string;
  /** Load the instance CSV. Returns {"ok":true} or {"error":message}. */
  loadInstances(csv: string): string;
  /** Configure the run, for example {"provider":"static"}. Returns {"ok":true} or {"error":message}. */
  configure(configJson: string): string;

  /** Build and run. Returns a snapshot, or {"error":message}. */
  start(): string;
  /** Continue an interactive run after submitting a decision. Returns a snapshot. */
  resume(): string;
  /** Return the current snapshot without advancing. */
  snapshot(): string;

  delete(): void;
}

export interface Monitor {
  /**
   * Register a callback invoked with each entry, as a JSON string, the moment it is recorded,
   * so the caller observes notifications live rather than by draining after the run. The callback
   * runs during the engine's blocking run, so a caller that must not block the page runs the engine
   * in a worker and forwards each entry from the callback.
   */
  onNotice(callback: (entryJson: string) => void): void;
  /** Return the log entries recorded since the previous drain, as a JSON array string. */
  drainLog(): string;
  delete(): void;
}

export interface Controller {
  /**
   * Submit a decision the caller must resolve, identified by its token's instance and node. The
   * decision is {"type":"entry|exit|choice|messageDelivery","instanceId":s,"nodeId":s,
   * "status":[...]?,"choices":[...]?,"origin":s?,"sender":s?}, where a choice carries one value per
   * choice of the decision task and a message delivery names the chosen message by its origin and
   * sender. Returns {"queued":true} or {"rejected":reason}. The engine auto-resolves the unambiguous
   * entries, exits, and directly addressed message deliveries itself, so those are not submitted.
   */
  submitDecision(decisionJson: string): string;
  /** Queue a clock tick that advances simulated time by one unit at the next resume. */
  submitClockTick(): string;
  /** Queue a termination event that ends execution at the next resume. */
  submitTermination(): string;
  delete(): void;
}

export interface BpmnosModule {
  Engine: { new (): Engine };
  Monitor: { new (): Monitor };
  Controller: { new (): Controller };
}

/** Instantiate the WebAssembly module. The wasm is resolved relative to this module. */
export default function createBpmnos(): Promise<BpmnosModule>;
