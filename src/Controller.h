#ifndef BPMNOS_WASM_CONTROLLER_H
#define BPMNOS_WASM_CONTROLLER_H

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <bpmn++.h>
#include <bpmnos-model.h>
#include <bpmnos-execution.h>

namespace BPMNOS::WASM {

using json = nlohmann::ordered_json;

/**
 * @brief The input side of the boundary: an interactive controller.
 *
 * Deriving from the engine's controller, it owns the unambiguous auto dispatchers, the feasible exit,
 * the feasible non sequential entry, and the directly addressed message delivery, each evaluated by a
 * guided evaluator, and resolves those without the caller. Everything contested, a choice, the entry of
 * a child of a sequential ad hoc subprocess, and an ambiguous message delivery, it surfaces to the caller
 * through pendingDecisions and resolves through the matching enqueueEntryDecision, enqueueExitDecision,
 * enqueueChoiceDecision, or enqueueMessageDeliveryDecision. The caller advances simulated time with
 * enqueueClockTick and ends execution with enqueueTermination. No time handler is attached, so the engine
 * stops once neither an auto dispatcher nor an enqueued input yields an event, at which point the pending
 * decisions are exactly those left for the caller.
 *
 * A decision is identified by the natural identity of its token, its instance and its node, and a message
 * by its origin and its sender. An enqueued input carries only that identity and is validated by looking it
 * up in the live system state when it is dispatched, so a decision for a token that has since been withdrawn
 * finds no match and is silently dropped rather than acting on stale state.
 */
class Controller : public Execution::Controller {
public:
  Controller();
  ~Controller() override;

  /**
   * @brief Connects the auto dispatchers to this controller, then the controller to the engine.
   *
   * @param mediator The engine to connect to.
   */
  void connect(Execution::Mediator* mediator) override;

  /**
   * @brief Records the current system state as the engine installs it, then forwards the notification.
   * Caching the state lets pendingDecisions read it without the caller holding it, exactly as the
   * engine's own candidate collections do.
   *
   * @param observable The observed notification.
   */
  void notice(const Execution::Observable* observable) override;

  /**
   * @brief Returns the next event to the engine: an auto resolved decision while one is feasible, then a
   * caller supplied decision, clock tick, or termination, and a null pointer when none remains.
   *
   * @param systemState The current system state.
   * @return The next event, or a null pointer when none remains.
   */
  std::shared_ptr<Execution::Event> dispatchEvent(const Execution::SystemState* systemState) override;

  /**
   * @brief Enumerates the decisions the caller must resolve, each carrying its kind and its token's
   * instance and node. A choice additionally carries, for every choice of the decision task, either the
   * allowed enumeration or the lower and upper bounds. A message delivery carries its candidate messages,
   * each with its origin and sender. It reads the system state cached from the latest notification.
   *
   * @return A JSON array of the pending decisions left for the caller.
   */
  json pendingDecisions();

  /**
   * @brief Queues the entry of a waiting token for the next advance.
   *
   * @param decision {"instanceId": s, "nodeId": s, "status": [ ... ]?}.
   * @return {"queued": true} on acceptance, or {"rejected": reason}.
   */
  json enqueueEntryDecision(const json& decision);

  /**
   * @brief Queues the exit of a waiting token for the next advance.
   *
   * @param decision {"instanceId": s, "nodeId": s, "status": [ ... ]?}.
   * @return {"queued": true} on acceptance, or {"rejected": reason}.
   */
  json enqueueExitDecision(const json& decision);

  /**
   * @brief Queues a choice for a waiting token for the next advance.
   *
   * @param decision {"instanceId": s, "nodeId": s, "choices": [ ... ]}, one value per choice of the
   * decision task.
   * @return {"queued": true} on acceptance, or {"rejected": reason}.
   */
  json enqueueChoiceDecision(const json& decision);

  /**
   * @brief Queues the delivery of a message to a waiting token for the next advance.
   *
   * @param decision {"instanceId": s, "nodeId": s, "origin": s, "sender": s}, naming the chosen message
   * by its origin and its sender from the header.
   * @return {"queued": true} on acceptance, or {"rejected": reason}.
   */
  json enqueueMessageDeliveryDecision(const json& decision);

  /**
   * @brief Queues a clock tick that advances simulated time by one unit at the next advance.
   *
   * @return {"queued": "clockTick"}.
   */
  json enqueueClockTick();

  /**
   * @brief Queues a termination event that ends execution at the next advance.
   *
   * @return {"queued": "termination"}.
   */
  json enqueueTermination();

private:
  /// A deferred event builder. An enqueue method resolves the decision's identity to weak references
  /// against the live pending list, then stores a builder that, when the engine fetches, builds the event
  /// if those references are still alive or yields nothing if they have expired.
  using EventBuilder = std::function<std::shared_ptr<Execution::Event>(const Execution::SystemState*)>;

  std::unique_ptr<Execution::Evaluator> evaluator;                         ///< guides the auto dispatchers
  std::vector<std::unique_ptr<Execution::EventDispatcher>> autoDispatchers; ///< tried before the caller queue
  std::deque<EventBuilder> queue;                                          ///< deferred builders awaiting dispatch
  const Execution::SystemState* systemState = nullptr;                     ///< cached from the latest notice
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_CONTROLLER_H
