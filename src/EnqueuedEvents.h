#ifndef BPMNOS_WASM_ENQUEUEDEVENTS_H
#define BPMNOS_WASM_ENQUEUEDEVENTS_H

#include <deque>
#include <memory>

#include <bpmn++.h>
#include <bpmnos-model.h>
#include <bpmnos-execution.h>

namespace BPMNOS::WASM {

/**
 * @brief The dispatcher of the events the caller has queued.
 *
 * What comes from outside the engine is dispatched like anything else: this is an event dispatcher among
 * the dispatchers a controller is composed of, so its position in that composition is its precedence.
 * Whatever precedes it settles what it settles, and what follows it speaks only when neither the engine's
 * dispatchers nor the queue had anything to say.
 *
 * Who fills the queue is outside the engine and outside this class: a user deciding, or a replay pushing
 * what a log recorded. An event is built where it is queued, so a decision refers to the request it answers
 * and becomes void when that request is withdrawn; such an event is dropped rather than dispatched, because
 * the engine requires a dispatched event to be live.
 */
class EnqueuedEvents : public Execution::EventDispatcher {
public:
  /**
   * @brief Appends an event to the queue, to be dispatched at the next fetch that reaches this dispatcher.
   *
   * @param event The event, built by the caller against the state it answers.
   */
  void enqueue(std::shared_ptr<Execution::Event> event);

  /**
   * @brief Returns the first queued event that has not expired, dropping every expired one before it.
   *
   * @param systemState The current system state, which the queue does not read.
   * @return The next live event, or a null pointer when the queue holds none.
   */
  std::shared_ptr<Execution::Event> dispatchEvent(const Execution::SystemState* systemState) override;

private:
  std::deque<std::shared_ptr<Execution::Event>> queue;  ///< Built events awaiting dispatch, in order.
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_ENQUEUEDEVENTS_H
