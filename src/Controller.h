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

/**
 * @brief A choice between bounds: what the condition states, and what may actually be selected.
 *
 * The two differ, and a caller needs both. The bounds are the condition's, with strictness resolved and the
 * interval narrowed to the attribute's type. What may be selected are the multiples of the discretizer
 * within them, and the least and the greatest of those need not be the bounds themselves: a step the engine
 * cannot hold exactly puts its grid slightly beside the values the model states, so a bound may fall between
 * two selectable values and not be one. A caller reporting that to a reader compares the two.
 */
struct BoundedChoice {
  BPMNOS::number lowerBound;   ///< as the condition states it
  BPMNOS::number upperBound;   ///< as the condition states it
  BPMNOS::number lowest;       ///< least selectable value, on the discretizer's grid
  BPMNOS::number highest;      ///< greatest selectable value, on the discretizer's grid
  /// The step, stated or implied by the type, at the precision it was evaluated at. A step is not a value
  /// an attribute holds and is not rounded to one: the values it admits are the multiples of the step the
  /// model states, each rounded as it is taken, so a caller given a rounded step computes a different grid.
  std::optional<double> multipleOf;
};

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
   * @brief Returns the attribute and the candidate values of the next choice of the request's decision
   * task, given the values already chosen for the choices before it.
   *
   * A decision task states its choices in order, and what a later one admits depends on the earlier ones:
   * the engine writes each chosen value into the status before it evaluates the next condition. So the
   * candidates are not a property of the model but an answer for a prefix of values, and this reports the
   * answer for one prefix at a time. Given as many values as the task has choices, there is no next choice
   * and nothing is returned.
   *
   * The status, the data and the globals are copied before anything is applied, the data by value and not
   * by the references SharedValues holds, so that computing an answer cannot write the run. The current
   * time is stamped onto the copied status first, as the engine stamps it before it applies a choice, so
   * that a condition reading the timestamp is evaluated against the value the engine will use.
   *
   * The bounds are those of Choice::getBounds, moved to the first and the last multiple of the discretizer
   * within them. That function has already resolved strictness, by moving a strict bound inward by the
   * smallest representable increment, and has already narrowed the interval to the integers within it for
   * every type but the decimal; both are the engine's to decide and neither is recomputed here. The move to
   * the discretizer is this bridge's, because the values admitted are its multiples counted from zero while
   * a caller's control counts its steps from the minimum it is given, and the two agree only where that
   * minimum is a multiple. Where a bounded choice states no discretizer, which a model may do although it
   * should not, the bounds are reported unmoved and no discretizer is reported with them.
   *
   * @param request The choice decision request.
   * @param selectedValues The values already chosen, in the order the choices are made.
   * @return The attribute and the candidates of the next choice, or nothing where every choice has a value.
   */
  std::optional<std::tuple<const Model::Attribute*, std::variant<EnumeratedChoice, BoundedChoice>>>
  getChoiceCandidates(const Execution::DecisionRequest* request, const std::vector<BPMNOS::number>& selectedValues) const;

  /**
   * @brief Returns the choices of the request's decision task, in the order they are made.
   *
   * The bridge needs them to encode a caller's values by each attribute's type, which it must be able to do
   * without asking for candidates it has no other use for.
   *
   * @param request The choice decision request.
   * @return The choices, empty where the token stands at no decision task.
   */
  std::vector<const Model::Choice*> getChoices(const Execution::DecisionRequest* request) const;

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

  /**
   * @brief Lets a dispatcher of the composition speak again.
   *
   * @param index Its position in the composition, as it was given.
   * @throws std::runtime_error if the composition has no dispatcher at that position.
   */
  void activate(std::size_t index);

  /**
   * @brief Silences a dispatcher of the composition, so that the walk carries past it.
   *
   * What a silenced dispatcher would have settled is left to whatever follows it, and to the caller where
   * nothing does, which is how one composition serves several ways of running a model: the run is driven by
   * whichever of its dispatchers answer, and that is turned over between fetches without rebuilding
   * anything. Only dispatching is withheld, so a silenced dispatcher goes on observing and is correct at the
   * moment it speaks again.
   *
   * The queue cannot be silenced, for the reason a composition without one is refused: a run that dispatches
   * nothing the caller enqueues ignores every decision, clock tick and termination it is given.
   *
   * @param index Its position in the composition, as it was given.
   * @throws std::runtime_error if the composition has no dispatcher at that position, or if it is the queue.
   */
  void deactivate(std::size_t index);

  /**
   * @brief Reports whether a dispatcher of the composition speaks.
   *
   * @param index Its position in the composition, as it was given.
   * @return True while it dispatches, false while it is silenced.
   * @throws std::runtime_error if the composition has no dispatcher at that position.
   */
  bool isActive(std::size_t index) const;

private:
  /// Refuses a position the composition does not hold.
  void requireIndex(std::size_t index) const;

  std::vector<std::unique_ptr<Execution::EventDispatcher>> dispatchers;     ///< walked in order on each fetch
  std::vector<bool> active;                                                 ///< whether each of them dispatches
  EnqueuedEvents* enqueued = nullptr;                                       ///< the queue among them, owned by the list
  std::size_t enqueuedIndex = 0;                                            ///< its position, which cannot be silenced
  const Execution::SystemState* systemState = nullptr;                      ///< cached from the latest notice
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_CONTROLLER_H
