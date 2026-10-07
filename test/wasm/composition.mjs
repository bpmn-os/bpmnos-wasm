// The composition the WebAssembly tests drive, stated once.
//
// A controller is the dispatchers it is given, walked in the order given, and a run is driven by whichever
// of them answer. That is a property the controller turns over between fetches, so one composition serves
// several ways of running a model and nothing is rebuilt when one becomes another.
//
// A position is a precedence. `EnqueuedEvents` comes first because it is what the caller says while
// everything behind it is what the run settles for itself: ahead of the deciders, a termination ends the run
// when it is given rather than at the first fetch where none of them has anything to say, and what the
// caller answers is dispatched before anything automatic settles something else. It costs nothing at the
// fetches where it is empty. No dispatcher advances time: the data provider of every test holds it, and
// a greedy run lets the engine advance it.
export const dispatchers = [
  'EnqueuedEvents',
  'FirstFeasibleExit', 'FirstFeasibleEntry', 'InstantDirectMessage',
  'FirstEnumeratedChoice', 'CompetingCandidates'
];

/** Every decision settles itself, so a run whose engine advances time needs nothing from the caller. */
export const greedy = JSON.stringify({ dispatchers });

/**
 * The positions only a greedy run lets speak: the two that decide, because otherwise the caller decides.
 * Silence them and the choice, the entry of a child of a sequential ad hoc subprocess and the ambiguous
 * message delivery reach the queue. Read from the composition rather than written down, so reordering it
 * moves them.
 */
export const greedyOnly = [ 'FirstEnumeratedChoice', 'CompetingCandidates' ]
  .map(name => dispatchers.indexOf(name));

/**
 * Silences what only a greedy run adds, leaving the controller interactive: what is unambiguous still
 * resolves itself and everything else waits for the caller.
 */
export function makeInteractive(controller) {
  for (const index of greedyOnly) {
    controller.deactivate(index);
  }
  return controller;
}

/** The data provider of every test: the static data provider holding time. */
export const held = JSON.stringify({ provider: 'static', clockTickDuration: 'hold' });

/**
 * Drives a run as far as it goes without the caller, round by round, stopping once a round produces no
 * record, which is a round in which the engine waited, or once the run is terminated, so that a test inspects
 * what is pending and enqueues its answer in between. Every event the engine processes is a record of the
 * monitor, so a round without a record is one without an event.
 */
export class Driver {
  constructor(engine, monitor) {
    this.engine = engine;
    this.records = 0;
    monitor.addObserver(() => { this.records += 1; });
  }

  /** Draws the named scenario and drives the run until it waits or is terminated. */
  async run(scenarioId = 0) {
    this.engine.initialize(scenarioId);
    await this.proceed();
  }

  /** Drives the run until it waits or is terminated. */
  async proceed() {
    for (;;) {
      const before = this.records;
      if (!(await this.engine.advance()) || this.records === before) {
        return;
      }
    }
  }
}
