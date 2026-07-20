#ifndef BPMNOS_WASM_CONTROLLER_H
#define BPMNOS_WASM_CONTROLLER_H

#include <deque>
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

namespace BPMNOS::WASM {

using EnumeratedChoice = std::vector<BPMNOS::number>;
using BoundedChoice = std::tuple<BPMNOS::number, BPMNOS::number, std::optional<BPMNOS::number>>; // LB,UP,multipleOf

/**
 * @brief The input side of the boundary: an interactive controller, in the engine's native vocabulary.
 *
 * Deriving from the engine's controller, it owns the unambiguous auto dispatchers, the first feasible exit,
 * the first feasible non sequential entry, and the directly addressed message delivery, each evaluated by a
 * guided evaluator, and resolves those without the caller. Everything contested, a choice, the entry of a
 * child of a sequential ad hoc subprocess, and an ambiguous message delivery, it surfaces to the caller.
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
  Controller();
  ~Controller() override;

  /**
   * @brief Connects the auto dispatchers to this controller, then the controller to the engine.
   *
   * @param mediator The engine to connect to.
   */
  void connect(Execution::Mediator* mediator) override;

  /**
   * @brief Records the current system state as the engine installs it, then forwards the notification, so
   * the query methods read it without the caller holding it.
   *
   * @param observable The observed notification.
   */
  void notice(const Execution::Observable* observable) override;

  /**
   * @brief Returns the next event to the engine: an auto resolved decision while one is feasible, then a
   * caller enqueued event that has not expired, and a null pointer when none remains.
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
  std::unique_ptr<Execution::Evaluator> evaluator;                         ///< guides the auto dispatchers
  std::vector<std::unique_ptr<Execution::EventDispatcher>> autoDispatchers; ///< tried before the caller queue
  std::deque<std::shared_ptr<Execution::Event>> queue;                     ///< built events awaiting dispatch
  const Execution::SystemState* systemState = nullptr;                     ///< cached from the latest notice
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_CONTROLLER_H
