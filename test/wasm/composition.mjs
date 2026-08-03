// The compositions the WebAssembly tests drive, stated once.
//
// A controller is the dispatchers it is given, walked in the order given, so a composition is what
// distinguishes one way of running a model from another.

/**
 * What is unambiguous resolves itself, everything else waits for the caller: the choice, the entry of a
 * child of a sequential ad hoc subprocess and the ambiguous message delivery reach the queue, and, no clock
 * being composed in, time advances only by a tick the caller enqueues.
 */
export const interactive = JSON.stringify({
  dispatchers: [ 'FirstFeasibleExit', 'FirstFeasibleEntry', 'InstantDirectMessage', 'EnqueuedEvents' ]
});

/**
 * Every decision settles itself and the clock advances on its own, so a run needs nothing from the caller.
 * The queue precedes the clock, which answers every fetch and would otherwise be the end of the walk.
 */
export const greedy = JSON.stringify({
  dispatchers: [
    'FirstFeasibleExit', 'FirstFeasibleEntry', 'InstantDirectMessage',
    'FirstEnumeratedChoice', 'CompetingCandidates', 'EnqueuedEvents', 'TimeWarp'
  ]
});
