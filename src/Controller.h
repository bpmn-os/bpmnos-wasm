#ifndef BPMNOS_WASM_CONTROLLER_H
#define BPMNOS_WASM_CONTROLLER_H

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include <bpmn++.h>
#include <bpmnos-model.h>
#include <bpmnos-execution.h>

namespace BPMNOS::WASM {

using json = nlohmann::ordered_json;

/// The input side of the boundary.
///
/// The controller is an event dispatcher through which the caller supplies the decisions
/// the engine is waiting for. When connected to an engine, its dispatchEvent is polled by
/// the engine's fetch loop and returns the next queued decision, or a null pointer when
/// nothing is queued, in which case the engine advances no further and returns to the
/// caller. It owns the registry that maps an opaque request identifier, the only reference
/// the caller ever holds, to a weak pointer into engine state, and it revalidates that
/// identifier both when a decision is submitted and again when it is dispatched, so a
/// decision for a withdrawn token can never take effect. It owns no engine; it is connected
/// to one.
///
/// The four decision kinds are named "entry", "exit", "choice", and "messageDelivery". A
/// submitted decision is a JSON object of the form
///   {"requestId": n, "type": "entry|exit|choice",
///    "status": [ ... ]?, "choices": [ ... ]?}
/// where status overrides the token status on entry or exit and is otherwise omitted, and
/// choices supplies one value per choice of a decision task. Message delivery is not yet
/// implemented and is rejected on submission.
class Controller : public Execution::EventDispatcher {
public:
  Controller();
  ~Controller() override;

  /// Returns the next queued decision to the engine, or a null pointer when none is ready.
  /// A queued decision whose handle has expired since submission is discarded here.
  std::shared_ptr<Execution::Event> dispatchEvent(const Execution::SystemState* systemState) override;

  /// Enumerates the decisions the given system state is currently waiting for, assigning a
  /// stable opaque identifier to each and reusing it across calls. Each entry carries the
  /// identifier, the decision type, and the serialised token; choice entries additionally
  /// carry, for every choice of the decision task, either the allowed enumeration or the
  /// lower and upper bounds, depending on how the choice is defined.
  json pendingDecisions(const Execution::SystemState* systemState);

  /// Accepts a decision from the caller. Revalidates the identifier against live state and,
  /// on success, queues the decision for the next advance. Returns {"queued": id} or
  /// {"rejected": reason, "requestId": id}.
  json submitDecision(const json& decision);

  /// Queues a termination event that ends execution at the next advance.
  json submitTermination();

private:
  struct Handle {
    std::weak_ptr<Execution::Token> token;
    std::weak_ptr<Execution::DecisionRequest> request;
    const Execution::DecisionRequest* requestPtr = nullptr;
    std::string type;
  };

  /// Constructs the engine event for a queued decision, revalidating and then consuming its
  /// handle. Returns a null pointer and sets error when the handle is no longer valid.
  std::shared_ptr<Execution::Event> makeEvent(const json& decision, std::string& error);

  std::map<std::uint64_t, Handle> handles;                                ///< identifier to handle
  std::map<const Execution::DecisionRequest*, std::uint64_t> idByRequest; ///< stable identifier reuse
  std::deque<json> queue;                                                 ///< decisions awaiting dispatch
  std::uint64_t nextId = 1;
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_CONTROLLER_H
