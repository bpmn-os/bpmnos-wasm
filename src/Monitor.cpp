#include "Monitor.h"

#include <utility>

namespace BPMNOS::WASM {

Monitor::Monitor() = default;

Monitor::~Monitor() = default;

void Monitor::subscribe(Execution::Engine* engine) {
  using Type = Execution::Observable::Type;
  engine->addSubscriber(
    this,
    Type::Token,
    Type::Event,
    Type::Message,
    Type::EntryRequest,
    Type::ChoiceRequest,
    Type::ExitRequest,
    Type::MessageDeliveryRequest
  );
}

void Monitor::addObserver(std::function<void(const json&)> observer) {
  observers.push_back(std::move(observer));
}

void Monitor::notice(const Execution::Observable* observable) {
  using Type = Execution::Observable::Type;
  json entry;
  switch (observable->getObservableType()) {
    case Type::Token:
      entry = json{ {"token", static_cast<const Execution::Token*>(observable)->jsonify()} };
      break;
    case Type::Event:
      entry = json{ {"event", static_cast<const Execution::Event*>(observable)->jsonify()} };
      break;
    case Type::Message:
      entry = json{ {"message", static_cast<const Execution::Message*>(observable)->jsonify()} };
      break;
    case Type::EntryRequest:
      entry = json{ {"entryRequest", static_cast<const Execution::DecisionRequest*>(observable)->token->jsonify()} };
      break;
    case Type::ChoiceRequest:
      entry = json{ {"choiceRequest", static_cast<const Execution::DecisionRequest*>(observable)->token->jsonify()} };
      break;
    case Type::ExitRequest:
      entry = json{ {"exitRequest", static_cast<const Execution::DecisionRequest*>(observable)->token->jsonify()} };
      break;
    case Type::MessageDeliveryRequest:
      entry = json{ {"messageDeliveryRequest", static_cast<const Execution::DecisionRequest*>(observable)->token->jsonify()} };
      break;
    default:
      return;
  }
  // Forward synchronously to every observer, in the order observers were added, before returning to the
  // engine. The engine notifies in execution order on one thread, so each observer sees that order.
  for (const auto& observer : observers) {
    observer(entry);
  }
}

} // namespace BPMNOS::WASM
