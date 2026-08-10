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
// fetches where it is empty. `TimeWarp` is last because a clock answers every fetch, so nothing behind it
// would ever be reached.
export const dispatchers = [
  'EnqueuedEvents',
  'FirstFeasibleExit', 'FirstFeasibleEntry', 'InstantDirectMessage',
  'FirstEnumeratedChoice', 'CompetingCandidates', 'TimeWarp'
];

/** Every decision settles itself and the clock advances on its own, so a run needs nothing from the caller. */
export const greedy = JSON.stringify({ dispatchers });

/**
 * The positions only a greedy run lets speak: the two that decide, because otherwise the caller decides, and
 * the clock, because otherwise the caller ticks it and the engine stands still until it does. Silence them
 * and the choice, the entry of a child of a sequential ad hoc subprocess and the ambiguous message delivery
 * reach the queue. Read from the composition rather than written down, so reordering it moves them.
 */
export const greedyOnly = [ 'FirstEnumeratedChoice', 'CompetingCandidates', 'TimeWarp' ]
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
