#ifndef BPMNOS_WASM_MONITOR_H
#define BPMNOS_WASM_MONITOR_H

#include <functional>
#include <vector>

#include <nlohmann/json.hpp>

#include <bpmn++.h>
#include <bpmnos-model.h>
#include <bpmnos-execution.h>

namespace BPMNOS::WASM {

using json = nlohmann::ordered_json;

/**
 * @brief A stateless fan-out of the engine's notifications to any number of observers.
 *
 * The monitor subscribes to an engine and, for each notification, serialises it through the engine's own
 * jsonify and forwards it to every registered observer, in the order the observers were added. It records
 * nothing and holds no log, so a caller that needs history keeps its own; a caller advances no execution
 * from an observer. Several observers may be attached, so the modules of a web application each receive
 * every update independently.
 *
 * It forwards the token, event, and message notifications, and the four decision requests, so an observer
 * learns not only how execution proceeds but also when a decision falls to the caller. A decision request
 * carries the deciding token, tagged by the kind of request. Withdrawal of a token arrives as an ordinary
 * token record whose state is WITHDRAWN.
 */
class Monitor : public Execution::Observer {
public:
  Monitor();
  ~Monitor() override;

  /**
   * @brief Subscribes to the token, event, and message notifications and the four decision requests of
   * the given engine.
   *
   * @param engine The engine to observe.
   */
  void subscribe(Execution::Engine* engine);

  /**
   * @brief Registers an observer invoked with each entry, as JSON, the moment it is recorded. Every
   * registered observer receives every entry, so a caller attaches one per module that needs the stream.
   *
   * @param observer The observer invoked per notification.
   */
  void addObserver(std::function<void(const json&)> observer);

  /**
   * @brief Serialises one notification and forwards it to every observer.
   *
   * @param observable The observed token, event, message, or decision request.
   */
  void notice(const Execution::Observable* observable) override;

private:
  std::vector<std::function<void(const json&)>> observers;  ///< Notified in order for every entry.
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_MONITOR_H
