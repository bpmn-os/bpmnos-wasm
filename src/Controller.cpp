#include "Controller.h"

#include <algorithm>
#include <ranges>
#include <string>

#include "Convert.h"

namespace BPMNOS::WASM {

namespace {

/// Returns the metadata for each choice of a decision task token, so the caller knows which
/// values it may submit. A choice is defined either as an enumeration of allowed values or
/// as a bounded range; the engine's two accessors are each valid only for their respective
/// kind, so the kind is discriminated here before the accessor is called.
json choiceMetadata(Execution::Token* token) {
  json out = json::array();
  if (!token->node || !token->node->extensionElements) {
    return out;
  }
  auto* extensionElements = token->node->extensionElements->as<Model::ExtensionElements>();
  if (!extensionElements) {
    return out;
  }
  for (const auto& choice : extensionElements->choices) {
    json entry;
    entry["attribute"] = choice->attribute->name;
    if (!choice->enumeration.empty()) {
      auto enumeration = choice->getEnumeration(token->status, *token->data, token->globals);
      json values = json::array();
      for (auto value : enumeration) {
        values.push_back(toDouble(value));
      }
      entry["enumeration"] = values;
    }
    else {
      auto bounds = choice->getBounds(token->status, *token->data, token->globals);
      entry["lowerBound"] = toDouble(bounds.first);
      entry["upperBound"] = toDouble(bounds.second);
    }
    out.push_back(std::move(entry));
  }
  return out;
}

} // namespace

Controller::Controller() = default;
Controller::~Controller() = default;

json Controller::messageCandidates(Execution::Token* token, const Execution::SystemState* systemState) {
  json out = json::array();
  if (!systemState || !token->node || !token->node->extensionElements) {
    return out;
  }
  auto* extensionElements = token->node->extensionElements->as<Model::ExtensionElements>();
  if (!extensionElements) {
    return out;
  }
  const auto* messageDefinition = extensionElements->getMessageDefinition(token->status);
  if (!messageDefinition) {
    return out;
  }
  auto recipientHeader = messageDefinition->getRecipientHeader(
    token->getAttributeRegistry(), token->status, *token->data, token->globals);
  const auto& senders = extensionElements->messageCandidates;
  for (const auto& message : systemState->messages) {
    if (!message || message->state != Execution::Message::State::CREATED) {
      continue;
    }
    if (!std::ranges::contains(senders, message->origin) || !message->matches(recipientHeader)) {
      continue;
    }
    const Execution::Message* messagePtr = message.get();
    std::uint64_t messageId;
    auto found = idByMessage.find(messagePtr);
    if (found != idByMessage.end()) {
      messageId = found->second;
    }
    else {
      messageId = nextId++;
      idByMessage[messagePtr] = messageId;
      messageHandles[messageId] = message->weak_from_this();
    }
    out.push_back(json{ {"messageId", messageId}, {"message", message->jsonify()} });
  }
  return out;
}

json Controller::pendingDecisions(const Execution::SystemState* systemState) {
  json arr = json::array();
  if (!systemState) {
    return arr;
  }
  auto collect = [&](auto& list, const char* type) {
    for (auto& tuple : list) {
      auto token = std::get<0>(tuple).lock();
      auto request = std::get<1>(tuple).lock();
      if (!token || !request) {
        continue;
      }
      const Execution::DecisionRequest* requestPtr = request.get();
      std::uint64_t id;
      auto found = idByRequest.find(requestPtr);
      if (found != idByRequest.end()) {
        id = found->second;
      }
      else {
        id = nextId++;
        idByRequest[requestPtr] = id;
        handles[id] = Handle{ std::get<0>(tuple), std::get<1>(tuple), requestPtr, type };
      }
      json entry;
      entry["requestId"] = id;
      entry["type"] = type;
      entry["token"] = token->jsonify();
      if (std::string(type) == "choice") {
        entry["choices"] = choiceMetadata(token.get());
      }
      else if (std::string(type) == "messageDelivery") {
        entry["candidates"] = messageCandidates(token.get(), systemState);
      }
      arr.push_back(std::move(entry));
    }
  };
  collect(systemState->pendingEntryDecisions, "entry");
  collect(systemState->pendingExitDecisions, "exit");
  collect(systemState->pendingChoiceDecisions, "choice");
  collect(systemState->pendingMessageDeliveryDecisions, "messageDelivery");
  return arr;
}

json Controller::submitDecision(const json& decision) {
  if (!decision.contains("requestId") || !decision.contains("type")) {
    return json{ {"rejected", "decision requires requestId and type"} };
  }
  auto id = decision["requestId"].get<std::uint64_t>();
  std::string type = decision["type"].get<std::string>();
  auto it = handles.find(id);
  if (it == handles.end()) {
    return json{ {"rejected", "unknown"}, {"requestId", id} };
  }
  auto token = it->second.token.lock();
  auto request = it->second.request.lock();
  if (!token || !request) {
    idByRequest.erase(it->second.requestPtr);
    handles.erase(it);
    return json{ {"rejected", "expired"}, {"requestId", id} };
  }
  if (it->second.type != type) {
    return json{ {"rejected", "type mismatch"}, {"requestId", id} };
  }
  if (type == "choice" && !decision.contains("choices")) {
    return json{ {"rejected", "choice requires choices"}, {"requestId", id} };
  }
  if (type == "messageDelivery") {
    if (!decision.contains("messageId")) {
      return json{ {"rejected", "messageDelivery requires messageId"}, {"requestId", id} };
    }
    auto messageId = decision["messageId"].get<std::uint64_t>();
    auto messageIt = messageHandles.find(messageId);
    if (messageIt == messageHandles.end() || messageIt->second.expired()) {
      return json{ {"rejected", "message unknown or expired"}, {"requestId", id}, {"messageId", messageId} };
    }
  }
  queue.push_back(decision);
  return json{ {"queued", id} };
}

json Controller::submitTermination() {
  queue.push_back(json{ {"type", "termination"} });
  return json{ {"queued", "termination"} };
}

std::shared_ptr<Execution::Event> Controller::dispatchEvent(const Execution::SystemState*) {
  while (!queue.empty()) {
    json decision = queue.front();
    queue.pop_front();
    std::string error;
    auto event = makeEvent(decision, error);
    if (event) {
      return event;
    }
    // The handle expired between submission and dispatch; discard and try the next.
  }
  return nullptr;
}

std::shared_ptr<Execution::Event> Controller::makeEvent(const json& decision, std::string& error) {
  std::string type = decision.value("type", std::string());
  if (type == "termination") {
    return std::make_shared<Execution::TerminationEvent>();
  }
  auto id = decision.value("requestId", std::uint64_t{ 0 });
  auto it = handles.find(id);
  if (it == handles.end()) {
    error = "unknown";
    return nullptr;
  }
  auto token = it->second.token.lock();
  auto request = it->second.request.lock();
  if (!token || !request) {
    idByRequest.erase(it->second.requestPtr);
    handles.erase(it);
    error = "expired";
    return nullptr;
  }
  std::shared_ptr<Execution::Event> event;
  if (type == "entry") {
    std::optional<BPMNOS::Values> status;
    if (decision.contains("status") && !decision["status"].is_null()) {
      status = toValues(decision["status"]);
    }
    event = std::make_shared<Execution::EntryEvent>(token.get(), status);
  }
  else if (type == "exit") {
    std::optional<BPMNOS::Values> status;
    if (decision.contains("status") && !decision["status"].is_null()) {
      status = toValues(decision["status"]);
    }
    event = std::make_shared<Execution::ExitEvent>(token.get(), status);
  }
  else if (type == "choice") {
    event = std::make_shared<Execution::ChoiceEvent>(token.get(), toChoiceValues(decision.at("choices")));
  }
  else if (type == "messageDelivery") {
    auto messageId = decision.value("messageId", std::uint64_t{ 0 });
    auto messageIt = messageHandles.find(messageId);
    auto message = (messageIt != messageHandles.end()) ? messageIt->second.lock() : nullptr;
    if (!message) {
      error = "message expired";
      return nullptr;
    }
    event = std::make_shared<Execution::MessageDeliveryEvent>(token.get(), message.get());
    idByMessage.erase(message.get());
    messageHandles.erase(messageId);
  }
  else {
    error = "unsupported type: " + type;
    return nullptr;
  }
  idByRequest.erase(it->second.requestPtr);
  handles.erase(it);
  return event;
}

} // namespace BPMNOS::WASM
