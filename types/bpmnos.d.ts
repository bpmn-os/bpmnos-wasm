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
   * "messageDeliveryRequest": payload}; a decision request carries the deciding token, and a message
   * delivery request carries with it the senders the token accepts and the header it expects, so that a
   * caller replaying the entries matches a message against it rather than asking the engine.
   */
  addObserver(observer: (entryJson: string) => void): void;
  delete(): void;
}

export interface Controller {
  /**
   * Return the decisions left for the caller as a JSON array string, each carrying its kind and its
   * token's instance and node: [{"type":"entry|exit|choice|messageDelivery","instanceId":s,"nodeId":s}].
   * For a choice, query the allowed values with getChoiceCandidates; for a message delivery, match the
   * messages seen against the criterion the request record carried.
   */
  getPendingDecisions(): string;
  /**
   * Return the next choice the decision task at the given token waits for, given the values selected so
   * far as a JSON array string, as a JSON object string. A decision task states its choices in order and a
   * later one may depend on the earlier ones, since the engine writes each chosen value into the status
   * before it evaluates the next condition, so the candidates are asked for one choice at a time.
   *
   * `{}` says the request no longer stands and the decision is to be dropped; `{"complete":true}` says
   * every choice has a value and the decision may be submitted; otherwise the answer is the next choice,
   * as {"attribute":s,"enumeration":[...]} for a drop-down or
   * {"attribute":s,"lowerBound":n,"upperBound":n,"lowest":n,"highest":n,"multipleOf":n?} for a number
   * input.
   *
   * A bounded choice is reported twice over. lowerBound and upperBound are the bounds as the condition
   * states them, with strictness and the attribute's type already resolved; lowest and highest are the
   * least and the greatest value that may be selected, being the multiples of multipleOf within those
   * bounds. A control offers the second pair, so that stepping from its minimum lands on values the
   * engine admits, and may tell its reader of the first where the two differ. Where the model states no
   * discretizer the step is the one the attribute's type implies, one for an integer or a boolean and
   * none for a decimal; where there is no step, lowest and highest are the bounds and multipleOf is
   * absent.
   */
  getChoiceCandidates(instanceId: string, nodeId: string, selectedValues: string): string;
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
  /**
   * Report what the engine resolves as it builds a model, for a caller that must know it without running:
   * {"model":s,"lookupTables":{name:csv}?} in, {"sequentialPerformers":[{"performer":s,"activities":[s]}],
   * "decisions":[{"node":s,"choices":[{"attribute":{"id":s,"name":s,"type":s},"kind":s,"discretized":b}]}]}
   * or {"error":message} out. The lookup tables are required because a model cannot be built without the
   * content of every table it references, and are read for nothing else.
   *
   * Each decision task is reported with the choices it states, in order, each of them either an
   * enumeration or a pair of bounds, which kind names as "enumeration" or "bounds"; discretized says
   * whether a bounded one states a discretizer. Which of the two a choice is was settled when the model
   * was built, so a caller drawing a control takes it from here rather than reading the condition again.
   * Nothing here is evaluated, and what a choice may take is asked of the controller.
   */
  describeModel(descriptionJson: string): string;
  /** Parse a BPMN model XML into an input the engine is built from. */
  Input: { new (bpmnXml: string): Input };
  /**
   * Build an engine from an input, the data provider to draw scenarios from (for example
   * {"provider":"static"} or {"provider":"stochastic","seed":1}), the controller driving every run, and a
   * monitor observing it or null for an unobserved run. The input is consumed, so one input builds one
   * engine. A run without a controller would fetch no event, so one is required.
   */
  Engine: { new (input: Input, providerJson: string, controller: Controller, monitor: Monitor | null): Engine };
  Monitor: { new (): Monitor };
  /**
   * Compose a controller of the dispatchers it walks in the order given, as
   * {"evaluator":name?,"dispatchers":[name,...]}.
   *
   * A dispatcher is named by its class, or, for the ones a greedy dispatcher drives, by the candidates
   * class that distinguishes it: "FirstFeasibleExit", "FirstFeasibleEntry", "FirstEnumeratedChoice",
   * "FirstBisectionalChoice", "SequentialEntries", "MessageDeliveries", "CompetingCandidates",
   * "InstantDirectMessage", "TimeWarp", "Metronome" or "Metronome(ms)", and "EnqueuedEvents". The evaluator
   * is "GuidedEvaluator" (the default) or "LocalEvaluator", and every evaluating dispatcher shares it.
   *
   * What a dispatcher settles is settled without the caller; what none of them settles waits for what the
   * caller enqueues, which the one "EnqueuedEvents" dispatches, so its position in the list is the
   * precedence of the caller's decisions. Exactly one is required: without it nothing could be enqueued,
   * and with several the precedence would be undefined. A clock answers every fetch, so "TimeWarp" and
   * "Metronome" come last, after the queue.
   */
  Controller: { new (compositionJson: string): Controller };
}

/** Instantiate the WebAssembly module. The wasm is resolved relative to this module. */
export default function createBPMNOS(): Promise<BPMNOSModule>;
