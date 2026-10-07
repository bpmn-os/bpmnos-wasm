// The compositions the native tests drive, stated once.
//
// A controller is the dispatchers it is given, walked in the order given, so a composition is what
// distinguishes one way of running a model from another. The two here are the ones the bridge exists for:
// a run the caller drives, and a run that drives itself.

#ifndef BPMNOS_WASM_TEST_COMPOSITION_H
#define BPMNOS_WASM_TEST_COMPOSITION_H

#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#include "Controller.h"
#include "Engine.h"
#include "EnqueuedEvents.h"
#include "Input.h"

namespace BPMNOS::WASM::Test {

/**
 * @brief The interactive composition: what is unambiguous resolves itself, everything else waits for the
 * caller.
 *
 * The first feasible exit, the first feasible non sequential entry and the directly addressed message
 * delivery are settled by their dispatchers. No dispatcher offers a choice, the entry of a child of a
 * sequential ad hoc subprocess, or an ambiguous message delivery, so those reach the queue. No dispatcher
 * advances time, so with a data provider holding time it advances only by a tick the caller enqueues.
 *
 * The queue comes first. A position is a precedence, and the queue is what the caller says while everything
 * behind it is what the run settles for itself, so an answer the caller gives is dispatched before anything
 * automatic decides something else, and a termination stops the run when it is given rather than when the
 * run happens to have nothing left to settle. It costs nothing at the fetches where it is empty.
 */
inline std::shared_ptr<Controller> interactiveController() {
  auto evaluator = std::make_shared<Execution::GuidedEvaluator>();
  std::vector<std::unique_ptr<Execution::EventDispatcher>> dispatchers;
  dispatchers.push_back(std::make_unique<EnqueuedEvents>());
  dispatchers.push_back(
    std::make_unique<Execution::GreedyDispatcher<Execution::FirstFeasibleExit>>(evaluator));
  dispatchers.push_back(
    std::make_unique<Execution::GreedyDispatcher<Execution::FirstFeasibleEntry>>(evaluator));
  dispatchers.push_back(std::make_unique<Execution::InstantDirectMessage>());
  return std::make_shared<Controller>(std::move(dispatchers));
}

/**
 * @brief The greedy composition: every decision settles itself.
 *
 * It is the engine's greedy application, dispatcher for dispatcher, with the queue first. A position is a
 * precedence: the queue is what the caller says, so ahead of the deciders a termination ends the run when it
 * is given rather than at the first fetch where nothing else has anything to say, and it costs nothing at the
 * fetches where it is empty. No dispatcher advances time; a greedy run lets the engine advance it.
 */
inline std::shared_ptr<Controller> greedyController() {
  auto evaluator = std::make_shared<Execution::GuidedEvaluator>();
  std::vector<std::unique_ptr<Execution::EventDispatcher>> dispatchers;
  dispatchers.push_back(std::make_unique<EnqueuedEvents>());
  dispatchers.push_back(
    std::make_unique<Execution::GreedyDispatcher<Execution::FirstFeasibleExit>>(evaluator));
  dispatchers.push_back(
    std::make_unique<Execution::GreedyDispatcher<Execution::FirstFeasibleEntry>>(evaluator));
  dispatchers.push_back(std::make_unique<Execution::InstantDirectMessage>());
  dispatchers.push_back(
    std::make_unique<Execution::GreedyDispatcher<Execution::FirstEnumeratedChoice>>(evaluator));
  dispatchers.push_back(
    std::make_unique<Execution::GreedyDispatcher<Execution::CompetingCandidates>>(evaluator));
  return std::make_shared<Controller>(std::move(dispatchers));
}

/**
 * @brief The data provider of a test: the stochastic data provider at seed zero, holding time, so that time
 * advances only by the engine or by clock ticks the caller enqueues.
 */
inline std::shared_ptr<Execution::DataProvider> dataProvider(Input& input) {
  return std::make_shared<Execution::StochasticDataProvider>(
    input.buildModel(), input.getInstance(), 0, std::chrono::milliseconds::max());
}

/**
 * @brief Drives a run as far as it goes without the caller, round by round, stopping once the engine waits or
 * the run is terminated, so that a test inspects what is pending and enqueues its answer in between.
 */
class Driver {
public:
  explicit Driver(Engine& engine)
    : engine(engine)
  {
    // the engine waits only when a round yields no event, which is when the run needs the caller
    engine.setWait([this]() { waited = true; });
  }

  /// @brief Draws the named scenario and drives the run until it waits or is terminated.
  void run(unsigned int scenarioId = 0) {
    engine.initialize(scenarioId);
    proceed();
  }

  /// @brief Drives the run until it waits or is terminated.
  void proceed() {
    waited = false;
    while (!waited && engine.advance()) {
    }
  }

private:
  Engine& engine;
  bool waited = false;
};

} // namespace BPMNOS::WASM::Test

#endif // BPMNOS_WASM_TEST_COMPOSITION_H
