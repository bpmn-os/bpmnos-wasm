// The compositions the native tests drive, stated once.
//
// A controller is the dispatchers it is given, walked in the order given, so a composition is what
// distinguishes one way of running a model from another. The two here are the ones the bridge exists for:
// a run the caller drives, and a run that drives itself.

#ifndef BPMNOS_WASM_TEST_COMPOSITION_H
#define BPMNOS_WASM_TEST_COMPOSITION_H

#include <memory>
#include <utility>
#include <vector>

#include "Controller.h"
#include "EnqueuedEvents.h"

namespace BPMNOS::WASM::Test {

/**
 * @brief The interactive composition: what is unambiguous resolves itself, everything else waits for the
 * caller.
 *
 * The first feasible exit, the first feasible non sequential entry and the directly addressed message
 * delivery are settled by their dispatchers. No dispatcher offers a choice, the entry of a child of a
 * sequential ad hoc subprocess, or an ambiguous message delivery, so those reach the queue, which is last
 * because nothing follows it. No clock is composed in, so time advances only by a tick the caller enqueues.
 */
inline std::shared_ptr<Controller> interactiveController() {
  auto evaluator = std::make_shared<Execution::GuidedEvaluator>();
  std::vector<std::unique_ptr<Execution::EventDispatcher>> dispatchers;
  dispatchers.push_back(
    std::make_unique<Execution::GreedyDispatcher<Execution::FirstFeasibleExit>>(evaluator));
  dispatchers.push_back(
    std::make_unique<Execution::GreedyDispatcher<Execution::FirstFeasibleEntry>>(evaluator));
  dispatchers.push_back(std::make_unique<Execution::InstantDirectMessage>());
  dispatchers.push_back(std::make_unique<EnqueuedEvents>());
  return std::make_shared<Controller>(std::move(dispatchers));
}

/**
 * @brief The greedy composition: every decision settles itself and the clock advances on its own.
 *
 * It is the engine's greedy application, dispatcher for dispatcher, with the queue before the clock.
 * TimeWarp answers every fetch, so anything behind it would never be reached; ahead of it, the queue is
 * what a caller ends such a run through.
 */
inline std::shared_ptr<Controller> greedyController() {
  auto evaluator = std::make_shared<Execution::GuidedEvaluator>();
  std::vector<std::unique_ptr<Execution::EventDispatcher>> dispatchers;
  dispatchers.push_back(
    std::make_unique<Execution::GreedyDispatcher<Execution::FirstFeasibleExit>>(evaluator));
  dispatchers.push_back(
    std::make_unique<Execution::GreedyDispatcher<Execution::FirstFeasibleEntry>>(evaluator));
  dispatchers.push_back(std::make_unique<Execution::InstantDirectMessage>());
  dispatchers.push_back(
    std::make_unique<Execution::GreedyDispatcher<Execution::FirstEnumeratedChoice>>(evaluator));
  dispatchers.push_back(
    std::make_unique<Execution::GreedyDispatcher<Execution::CompetingCandidates>>(evaluator));
  dispatchers.push_back(std::make_unique<EnqueuedEvents>());
  dispatchers.push_back(std::make_unique<Execution::TimeWarp>());
  return std::make_shared<Controller>(std::move(dispatchers));
}

} // namespace BPMNOS::WASM::Test

#endif // BPMNOS_WASM_TEST_COMPOSITION_H
