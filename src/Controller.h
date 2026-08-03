#ifndef BPMNOS_WASM_CONTROLLER_H
#define BPMNOS_WASM_CONTROLLER_H

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include <bpmn++.h>
#include <bpmnos-model.h>
#include <bpmnos-execution.h>

#include "EnqueuedEvents.h"

namespace BPMNOS::WASM {

using EnumeratedChoice = std::vector<BPMNOS::number>;
using BoundedChoice = std::tuple<BPMNOS::number, BPMNOS::number, std::optional<BPMNOS::number>>; // LB,UP,multipleOf

/**
 * @brief The input side of the boundary: an interactive controller, in the engine's native vocabulary.
 *
 * Deriving from the engine's controller, it is composed of the dispatchers it is given, which it owns and
 * walks in order on every fetch. What a dispatcher settles is settled without the caller;
 * what none of them settles waits for what the caller enqueues, which is dispatched by the EnqueuedEvents
 * among them, so the position of that queue in the composition is its precedence. A composition of the
 * first feasible exit, the first feasible non sequential entry, the directly addressed message delivery and
 * the queue therefore leaves the caller the choice, the entry of a child of a sequential ad hoc subprocess
 * and the ambiguous message delivery, while a composition that adds the remaining greedy dispatchers before
 * the queue leaves the caller nothing but the clock and the termination.
 *
 * The interface is native and holds no JSON: it surfaces the pending decision requests, and, per request,
 * the candidate choices or messages, and it accepts a decision as the request and its payload. The bridge's
 * bindings do all serialisation to and from JSON, and resolve the caller's identities to the native handles
 * this class takes. Each enqueue builds the engine event at once; the engine dispatches it only while it has
 * not expired, so a decision whose token, request, or message has since been withdrawn is void and silently
 * dropped.
 */
class Controller : public Execution::Controller {
public:
  /**
   * @brief Composes a controller of the dispatchers it walks, wired and complete.
   *
   * They are connected to this controller here rather than when the controller is connected to an engine,
   * so a constructed controller is a complete one and no dispatcher is connected a second time when a run
   * is repeated. Whichever of them evaluates keeps the evaluator it was built against, so this class names
   * no evaluator.
   *
   * Exactly one of them is the EnqueuedEvents every enqueue writes into, and its position among the others
   * is the precedence of what the caller decides. A composition without one could not be driven at all, and
   * one with several would leave that precedence to whichever was found first, so both are refused here
   * rather than discovered at the first enqueue.
   *
   * @param dispatchers The dispatchers, in the order they are to be walked, moved in.
   * @throws std::runtime_error if the dispatchers do not hold exactly one EnqueuedEvents.
   */
  Controller(std::vector<std::unique_ptr<Execution::EventDispatcher>> dispatchers);
  ~Controller() override;

  /**
   * @brief Records the current system state as the engine installs it, then forwards the notification, so
   * the query methods read it without the caller holding it.
   *
   * @param observable The observed notification.
   */
  void notice(const Execution::Observable* observable) override;

  /**
   * @brief Returns the next event to the engine by walking the dispatchers in order: a decision while it is
   * feasible, any other event at once, and a null pointer when no dispatcher offers one.
   *
   * @param systemState The current system state.
   * @return The next event, or a null pointer when none remains.
   */
  std::shared_ptr<Execution::Event> dispatchEvent(const Execution::SystemState* systemState) override;

  /**
   * @brief Returns the decision requests currently pending for the caller, read from the cached system
   * state. Each request carries its kind and its token.
   *
   * @return The pending decision requests, some possibly already expired.
   */
  std::vector<std::weak_ptr<const Execution::DecisionRequest>> getPendingRequests() const;

  /**
   * @brief Returns, per choice of the request's decision task, the attribute and the candidate values the
   * caller may pick: an enumeration of raw numbers, or the bounds. The bridge renders the numbers by the
   * attribute's type.
   *
   * @param request The choice decision request.
   * @return One {attribute, enumeration | bounds} per choice.
   */
  std::vector<std::tuple<const Model::Attribute*, std::variant<EnumeratedChoice, BoundedChoice>>>
  getChoiceCandidates(const Execution::DecisionRequest* request) const;

  /**
   * @brief Returns the created pool messages that may be delivered to the request's waiting token, whose
   * sender node is a candidate and whose header matches the recipient.
   *
   * @param request The message delivery decision request.
   * @return The candidate messages, some possibly already expired.
   */
  std::vector<std::weak_ptr<const Execution::Message>> getMessageCandidates(const Execution::DecisionRequest* request) const;

  /**
   * @brief Enqueues the entry of the request's token, optionally with a status, for the next advance.
   *
   * @param request The entry decision request.
   * @param status The status to enter with, if any.
   * @return Nothing on acceptance, or a reason when the request has expired.
   */
  std::expected<void, std::string> enqueueEntryDecision(std::weak_ptr<const Execution::DecisionRequest> request, std::optional<BPMNOS::Values> status);

  /**
   * @brief Enqueues the exit of the request's token, optionally with a status, for the next advance.
   *
   * @param request The exit decision request.
   * @param status The status to exit with, if any.
   * @return Nothing on acceptance, or a reason when the request has expired.
   */
  std::expected<void, std::string> enqueueExitDecision(std::weak_ptr<const Execution::DecisionRequest> request, std::optional<BPMNOS::Values> status);

  /**
   * @brief Enqueues a choice for the request's token for the next advance.
   *
   * @param request The choice decision request.
   * @param choices One value per choice of the decision task.
   * @return Nothing on acceptance, or a reason when the request has expired.
   */
  std::expected<void, std::string> enqueueChoiceDecision(std::weak_ptr<const Execution::DecisionRequest> request, std::vector<BPMNOS::number> choices);

  /**
   * @brief Enqueues the delivery of a message to the request's token for the next advance.
   *
   * @param request The message delivery decision request.
   * @param message The message to deliver.
   * @return Nothing on acceptance, or a reason when the request or the message has expired.
   */
  std::expected<void, std::string> enqueueMessageDeliveryDecision(std::weak_ptr<const Execution::DecisionRequest> request, std::weak_ptr<const Execution::Message> message);

  /**
   * @brief Enqueues a clock tick that advances simulated time by one unit at the next advance.
   *
   * @return Nothing on acceptance, or a reason when no system state is installed.
   */
  std::expected<void, std::string> enqueueClockTickEvent();

  /**
   * @brief Enqueues a termination that ends execution at the next advance.
   *
   * @return Nothing.
   */
  std::expected<void, std::string> enqueueTerminationEvent();

private:
  std::vector<std::unique_ptr<Execution::EventDispatcher>> dispatchers;     ///< walked in order on each fetch
  EnqueuedEvents* enqueued = nullptr;                                       ///< the queue among them, owned by the list
  const Execution::SystemState* systemState = nullptr;                      ///< cached from the latest notice
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_CONTROLLER_H
