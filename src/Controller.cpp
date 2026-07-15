#include "Controller.h"

#include <algorithm>
#include <ranges>
#include <string>

#include "Convert.h"

namespace BPMNOS::WASM {

namespace {

/**
 * @brief Returns the metadata for each choice of a decision task token, so the caller knows which values
 * it may submit. A choice is defined either as an enumeration of allowed values or as a bounded range;
 * the engine's two accessors each assert unless called for the matching kind, so the kind is
 * discriminated on the choice's enumeration before the accessor is called.
 *
 * @param token The decision task token.
 * @return A JSON array of the choice metadata.
 */
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

/**
 * @brief Returns the pool messages that may be delivered to the given waiting token, each with its origin
 * and sender, the identity by which the caller names its choice, and its serialised content.
 *
 * @param token The waiting token.
 * @param systemState The current system state.
 * @return A JSON array of the candidate messages.
 */
json messageCandidates(Execution::Token* token, const Execution::SystemState* systemState) {
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
    auto content = message->jsonify();
    json candidate;
    candidate["origin"] = content.value("origin", json());
    candidate["sender"] = content.contains("header") ? content["header"].value("sender", json()) : json();
    candidate["message"] = std::move(content);
    out.push_back(std::move(candidate));
  }
  return out;
}

/**
 * @brief Returns the waiting token in the given pending list whose instance and node match.
 *
 * @param list The pending decision list to search.
 * @param instanceId The token's instance identity.
 * @param nodeId The token's node identity.
 * @return The matching token, or a null pointer when none matches.
 */
std::shared_ptr<Execution::Token> findWaitingToken(
  const auto& list, const std::string& instanceId, const std::string& nodeId) {
  for (auto& tuple : list) {
    auto token = std::get<0>(tuple).lock();
    if (!token) {
      continue;
    }
    auto identity = token->jsonify();
    if (identity.value("instanceId", std::string()) == instanceId &&
        identity.value("nodeId", std::string()) == nodeId) {
      return token;
    }
  }
  return nullptr;
}

} // namespace

Controller::Controller() {
  // The auto dispatchers mirror the unambiguous half of the greedy controller: the first feasible exit,
  // the first feasible non sequential entry, and the directly addressed message delivery. The contested
  // half, choices and the competing sequential entries and message deliveries, is left to the caller.
  evaluator = std::make_unique<Execution::GuidedEvaluator>();
  autoDispatchers.push_back(
    std::make_unique<Execution::GreedyDispatcher<Execution::FirstFeasibleExit>>(evaluator.get()));
  autoDispatchers.push_back(
    std::make_unique<Execution::GreedyDispatcher<Execution::FirstFeasibleEntry>>(evaluator.get()));
  autoDispatchers.push_back(std::make_unique<Execution::InstantDirectMessage>());
}

Controller::~Controller() = default;

void Controller::connect(Execution::Mediator* mediator) {
  for (auto& dispatcher : autoDispatchers) {
    dispatcher->connect(this);
  }
  Execution::Controller::connect(mediator);
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
      auto identity = token->jsonify();
      json entry;
      entry["type"] = type;
      entry["instanceId"] = identity.value("instanceId", json());
      entry["nodeId"] = identity.value("nodeId", json());
      entry["token"] = identity;
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
  if (!decision.contains("type")) {
    return json{ {"rejected", "decision requires type"} };
  }
  std::string type = decision["type"].get<std::string>();
  if (type != "entry" && type != "exit" && type != "choice" && type != "messageDelivery") {
    return json{ {"rejected", "unknown type: " + type} };
  }
  if (!decision.contains("instanceId") || !decision.contains("nodeId")) {
    return json{ {"rejected", "decision requires instanceId and nodeId"} };
  }
  if (type == "choice" && !decision.contains("choices")) {
    return json{ {"rejected", "choice requires choices"} };
  }
  if (type == "messageDelivery" && (!decision.contains("origin") || !decision.contains("sender"))) {
    return json{ {"rejected", "messageDelivery requires origin and sender"} };
  }
  queue.push_back(decision);
  return json{ {"queued", true} };
}

json Controller::submitClockTick() {
  queue.push_back(json{ {"type", "clockTick"} });
  return json{ {"queued", "clockTick"} };
}

json Controller::submitTermination() {
  queue.push_back(json{ {"type", "termination"} });
  return json{ {"queued", "termination"} };
}

std::shared_ptr<Execution::Event> Controller::dispatchEvent(const Execution::SystemState* systemState) {
  // Auto resolve the unambiguous decisions in priority order, exactly as the greedy controller does: a
  // decision is dispatched only while feasible, and any other event is forwarded immediately.
  for (auto& dispatcher : autoDispatchers) {
    if (auto event = dispatcher->dispatchEvent(systemState)) {
      if (auto decision = std::dynamic_pointer_cast<Execution::Decision>(event)) {
        if (decision->reward().has_value()) {
          return event;
        }
      }
      else {
        return event;
      }
    }
  }
  // Then apply the caller's inputs, dropping any whose token is no longer waiting.
  while (!queue.empty()) {
    json decision = queue.front();
    queue.pop_front();
    std::string error;
    if (auto event = makeUserEvent(decision, systemState, error)) {
      return event;
    }
    // The token was withdrawn or the message consumed between submission and dispatch; try the next.
  }
  return nullptr;
}

std::shared_ptr<Execution::Event> Controller::makeUserEvent(
  const json& decision, const Execution::SystemState* systemState, std::string& error) {
  std::string type = decision.value("type", std::string());
  if (type == "clockTick") {
    return std::make_shared<Execution::ClockTickEvent>(systemState);
  }
  if (type == "termination") {
    return std::make_shared<Execution::TerminationEvent>();
  }

  std::string instanceId = decision.value("instanceId", std::string());
  std::string nodeId = decision.value("nodeId", std::string());
  std::shared_ptr<Execution::Token> token;
  if (type == "entry") {
    token = findWaitingToken(systemState->pendingEntryDecisions, instanceId, nodeId);
  }
  else if (type == "exit") {
    token = findWaitingToken(systemState->pendingExitDecisions, instanceId, nodeId);
  }
  else if (type == "choice") {
    token = findWaitingToken(systemState->pendingChoiceDecisions, instanceId, nodeId);
  }
  else if (type == "messageDelivery") {
    token = findWaitingToken(systemState->pendingMessageDeliveryDecisions, instanceId, nodeId);
  }
  else {
    error = "unsupported type: " + type;
    return nullptr;
  }
  if (!token) {
    error = "no matching pending decision";
    return nullptr;
  }

  if (type == "entry" || type == "exit") {
    std::optional<BPMNOS::Values> status;
    if (decision.contains("status") && !decision["status"].is_null()) {
      status = toValues(decision["status"]);
    }
    if (type == "entry") {
      return std::make_shared<Execution::EntryEvent>(token.get(), status);
    }
    return std::make_shared<Execution::ExitEvent>(token.get(), status);
  }
  if (type == "choice") {
    return std::make_shared<Execution::ChoiceEvent>(token.get(), toChoiceValues(decision.at("choices")));
  }
  // messageDelivery: find the chosen message by its origin and sender among the created pool messages.
  std::string origin = decision.value("origin", std::string());
  std::string sender = decision.value("sender", std::string());
  for (const auto& message : systemState->messages) {
    if (!message || message->state != Execution::Message::State::CREATED) {
      continue;
    }
    auto content = message->jsonify();
    std::string messageSender =
      content.contains("header") ? content["header"].value("sender", std::string()) : std::string();
    if (content.value("origin", std::string()) == origin && messageSender == sender) {
      return std::make_shared<Execution::MessageDeliveryEvent>(token.get(), message.get());
    }
  }
  error = "no matching message";
  return nullptr;
}

} // namespace BPMNOS::WASM
