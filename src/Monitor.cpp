#include "Monitor.h"

#include <utility>

namespace BPMNOS::WASM {
namespace {

/**
 * @brief What a token waiting for a message accepts, as the model states it.
 *
 * A caller replaying the records rather than reading the engine cannot ask which messages a token may
 * receive, since the answer changes whenever a message is created and the request is not raised again. The
 * criterion does not change while the token waits, so it is carried here and the caller matches a message
 * against it as the engine does: the message's origin is among the senders, its header holds the same keys,
 * and where both sides state a value the values are equal.
 *
 * @param token The token awaiting a message delivery.
 * @return {"senders": [ s ], "recipientHeader": { key: value | null }}, empty where the node declares no
 *         message definition for the token's status.
 */
json deliveryCriterion(const Execution::Token& token) {
  json criterion;
  if (!token.node || !token.node->extensionElements) {
    return criterion;
  }
  auto* extensionElements = token.node->extensionElements->as<Model::ExtensionElements>();
  if (!extensionElements) {
    return criterion;
  }
  const auto* messageDefinition = extensionElements->getMessageDefinition(token.status);
  if (!messageDefinition) {
    return criterion;
  }

  criterion["senders"] = json::array();
  for (const auto* sender : extensionElements->messageCandidates) {
    criterion["senders"].push_back(sender->id);
  }

  auto header = messageDefinition->getRecipientHeader(
    token.getAttributeRegistry(), token.status, *token.data, token.globals);
  criterion["recipientHeader"] = json::object();
  size_t i = 0;
  for (const auto& [key, type] : messageDefinition->header) {
    // rendered as a message's own header is, so that the two are compared as they are read
    if (i < header.size() && header[i].has_value() && type.has_value()) {
      criterion["recipientHeader"][key] = BPMNOS::to_string(header[i].value(), type.value());
    }
    else {
      criterion["recipientHeader"][key] = nullptr;
    }
    ++i;
  }

  return criterion;
}

} // namespace

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
    case Type::MessageDeliveryRequest: {
      const auto* token = static_cast<const Execution::DecisionRequest*>(observable)->token;
      auto record = token->jsonify();
      record.update(deliveryCriterion(*token));
      entry = json{ {"messageDeliveryRequest", std::move(record)} };
      break;
    }
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
