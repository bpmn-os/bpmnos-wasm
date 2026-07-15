#ifndef BPMNOS_WASM_CONTROLLER_H
#define BPMNOS_WASM_CONTROLLER_H

#include <deque>
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
 * through pendingDecisions and applies from submitDecision. The caller advances simulated time with
 * submitClockTick and ends execution with submitTermination. No time handler is attached, so the engine
 * stops once neither an auto dispatcher nor a submitted input yields an event, at which point the pending
 * decisions are exactly those left for the caller.
 *
 * A decision is identified by the natural identity of its token, its instance and its node, and a message
 * by its origin and its sender. A submission carries only that identity and is validated by looking it up
 * in the live system state when it is dispatched, so a decision for a token that has since been withdrawn
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
   * each with its origin and sender.
   *
   * @param systemState The current system state.
   * @return A JSON array of the pending decisions left for the caller.
   */
  json pendingDecisions(const Execution::SystemState* systemState);

  /**
   * @brief Accepts a decision from the caller and queues it for the next advance. The decision is
   * {"type": "entry|exit|choice|messageDelivery", "instanceId": s, "nodeId": s, "status": [ ... ]?,
   * "choices": [ ... ]?, "origin": s?, "sender": s?}.
   *
   * @param decision The decision to queue.
   * @return {"queued": true} on acceptance, or {"rejected": reason}.
   */
  json submitDecision(const json& decision);

  /**
   * @brief Queues a clock tick that advances simulated time by one unit at the next advance.
   *
   * @return {"queued": "clockTick"}.
   */
  json submitClockTick();

  /**
   * @brief Queues a termination event that ends execution at the next advance.
   *
   * @return {"queued": "termination"}.
   */
  json submitTermination();

private:
  /**
   * @brief Builds the engine event for a queued input, locating the token by its identity in the live
   * state.
   *
   * @param decision The queued input.
   * @param systemState The current system state.
   * @param error Set to a reason when no matching pending decision or message is found.
   * @return The engine event, or a null pointer when no match is found.
   */
  std::shared_ptr<Execution::Event> makeUserEvent(
    const json& decision, const Execution::SystemState* systemState, std::string& error);

  std::unique_ptr<Execution::Evaluator> evaluator;                         ///< guides the auto dispatchers
  std::vector<std::unique_ptr<Execution::EventDispatcher>> autoDispatchers; ///< tried before the caller queue
  std::deque<json> queue;                                                  ///< caller inputs awaiting dispatch
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_CONTROLLER_H
