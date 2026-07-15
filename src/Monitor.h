#ifndef BPMNOS_WASM_MONITOR_H
#define BPMNOS_WASM_MONITOR_H

#include <cstddef>
#include <functional>

#include <nlohmann/json.hpp>

#include <bpmn++.h>
#include <bpmnos-model.h>
#include <bpmnos-execution.h>

namespace BPMNOS::WASM {

using json = nlohmann::ordered_json;

/// A passive sink for the engine's notifications.
///
/// The monitor subscribes to the token, event, and message observables of an engine and
/// records each, serialised through the engine's own jsonify, into an append-only log. It
/// owns neither an engine nor a controller and never advances execution, so it may be
/// attached on its own to any engine, including one driven autonomously by the engine's
/// own controller or one merely replaying recorded behaviour. Withdrawal of a token
/// arrives here as an ordinary token record whose state is WITHDRAWN.
class Monitor : public Execution::Observer {
public:
  Monitor();
  ~Monitor() override;

  /// Subscribes to the token, event, and message notifications of the given engine.
  void subscribe(Execution::Engine* engine);

  /// Registers a callback invoked with each entry the moment it is recorded, in addition to the
  /// append-only log. A caller uses this to observe notifications live rather than by draining
  /// after the fact. Passing an empty callback removes the sink.
  void onNotice(std::function<void(const json&)> callback);

  /// Records one notification, appending its serialised form to the log.
  void notice(const Execution::Observable* observable) override;

  /// Returns the log entries recorded since the previous drain and marks them consumed.
  json drainLog();

  /// Returns the entire log recorded so far without consuming any of it.
  const json& fullLog() const { return log; }

private:
  json log;              ///< Append-only array of {"token"|"event"|"message": payload}.
  std::size_t drained;   ///< Number of entries already returned by drainLog.
  std::function<void(const json&)> sink;  ///< Optional live callback, invoked per recorded entry.
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_MONITOR_H
