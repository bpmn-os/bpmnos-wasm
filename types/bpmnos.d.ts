// Type declarations for the bpmnos-wasm module.
//
// The module exposes four classes bound through embind: Input, Engine, Controller, and Monitor. Every
// value that the engine expresses as JSON crosses the boundary as a JSON string, so each method below
// that carries JSON takes or returns a string that the caller parses or produces. The JSON shapes are
// described in the project README and roadmap. Instances are C++ objects; call delete on them when
// finished to release their memory.

export interface Input {
  /**
   * Return a JSON array string of the lookup table source names the model references (the keys to
   * supply to addLookupTable), so a caller can prompt for exactly those.
   */
  getLookupTableNames(): string;
  /** Provide one lookup table by its source name. */
  addLookupTable(name: string, csv: string): void;
  /** Provide the instance CSV. */
  setInstance(csv: string): void;
  delete(): void;
}

export interface Engine {
  /**
   * Draw the named scenario and run from the beginning, mirroring the execution engine's run. A
   * stochastic provider samples the base seed plus this index, so a different scenario id is a
   * different sample of the same model.
   */
  run(scenarioId: number): void;
  /** Continue a run, mirroring the execution engine's resume. */
  resume(): void;
  /** Report whether the system state is still alive; a run is done once this is false. */
  isAlive(): boolean;
  /** Report the current simulated time. */
  getCurrentTime(): number;
  /**
   * Report the total weighted objective value accumulated so far, mirroring the engine's
   * getWeightedObjective. It is a live running value, valid at any pause, not only at termination.
   */
  getWeightedObjective(): number;
  delete(): void;
}

export interface Monitor {
  /**
   * Register an observer invoked with each entry, as a JSON string, the moment it is recorded. Every
   * registered observer receives every entry, in the engine's execution order, so a caller attaches one
   * per module that needs the stream. The monitor keeps no history, so an observer attached after a run
   * begins misses the entries before it. The observer runs during the engine's blocking run, so a caller
   * that must not block the page runs the engine in a worker and forwards each entry from the observer.
   * Each entry is a single {"token"|"event"|"message"|"entryRequest"|"exitRequest"|"choiceRequest"|
   * "messageDeliveryRequest": payload}; a decision request carries the deciding token.
   */
  addObserver(observer: (entryJson: string) => void): void;
  delete(): void;
}

export interface Controller {
  /**
   * Return the decisions left for the caller as a JSON array string, each carrying its kind and its
   * token's instance and node: [{"type":"entry|exit|choice|messageDelivery","instanceId":s,"nodeId":s}].
   * For a choice or a message delivery, query the allowed decisions with getChoiceCandidates or
   * getMessageCandidates.
   */
  getPendingDecisions(): string;
  /**
   * Return, per choice of the decision task at the given token, the candidate values as a JSON array
   * string: [{"attribute":s,"enumeration":[...]}] for a drop-down, or
   * [{"attribute":s,"lowerBound":n,"upperBound":n,"multipleOf":n?}] for a slider.
   */
  getChoiceCandidates(instanceId: string, nodeId: string): string;
  /**
   * Return the messages that may be delivered to the given waiting token as a JSON array string:
   * [{"origin":s,"sender":s,"message":{...}}], the origin and sender naming the message for enqueue.
   */
  getMessageCandidates(instanceId: string, nodeId: string): string;
  /**
   * Queue the entry of a waiting token, identified by its instance and node, for the next resume. The
   * decision is {"instanceId":s,"nodeId":s,"status":[...]?}. Returns {"queued":true} or
   * {"rejected":reason}. The engine auto-resolves feasible non-sequential entries itself.
   */
  enqueueEntryDecision(decisionJson: string): string;
  /**
   * Queue the exit of a waiting token for the next resume. The decision is
   * {"instanceId":s,"nodeId":s,"status":[...]?}. Returns {"queued":true} or {"rejected":reason}. The
   * engine auto-resolves feasible exits itself.
   */
  enqueueExitDecision(decisionJson: string): string;
  /**
   * Queue a choice for a waiting token for the next resume. The decision is
   * {"instanceId":s,"nodeId":s,"choices":[...]}, one value per choice of the decision task. Returns
   * {"queued":true} or {"rejected":reason}.
   */
  enqueueChoiceDecision(decisionJson: string): string;
  /**
   * Queue the delivery of a message to a waiting token for the next resume. The decision is
   * {"instanceId":s,"nodeId":s,"origin":s,"sender":s}, naming the chosen message by its origin and its
   * sender from the header. Returns {"queued":true} or {"rejected":reason}. The engine auto-resolves
   * directly addressed message deliveries itself.
   */
  enqueueMessageDeliveryDecision(decisionJson: string): string;
  /** Queue a clock tick that advances simulated time by one unit at the next resume. */
  enqueueClockTickEvent(): string;
  /** Queue a termination event that ends execution at the next resume. */
  enqueueTerminationEvent(): string;
  delete(): void;
}

export interface BPMNOSModule {
  /** Parse a BPMN model XML into an input the engine is built from. */
  Input: { new (bpmnXml: string): Input };
  /**
   * Build an engine from an input, a configuration JSON string (for example {"provider":"static"} or
   * {"provider":"stochastic","seed":1}), a monitor, and a controller or null to run autonomously. The
   * input is consumed, so one input builds one engine.
   */
  Engine: { new (input: Input, configJson: string, monitor: Monitor, controller: Controller | null): Engine };
  Monitor: { new (): Monitor };
  Controller: { new (): Controller };
}

/** Instantiate the WebAssembly module. The wasm is resolved relative to this module. */
export default function createBPMNOS(): Promise<BPMNOSModule>;
