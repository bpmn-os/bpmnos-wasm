#include "Controller.h"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace BPMNOS::WASM {

Controller::Controller(std::vector<std::unique_ptr<Execution::EventDispatcher>> dispatchers)
  : dispatchers(std::move(dispatchers))
{
  // Find the one queue every enqueue writes into. The cast happens here and never again: the list owns the
  // queue, and the raw pointer is how enqueue reaches it without searching.
  for (const auto& dispatcher : this->dispatchers) {
    if (auto* queue = dynamic_cast<EnqueuedEvents*>(dispatcher.get())) {
      if (enqueued) {
        throw std::runtime_error("controller composed of several EnqueuedEvents");
      }
      enqueued = queue;
    }
  }
  if (!enqueued) {
    throw std::runtime_error("controller composed without EnqueuedEvents");
  }

  // A constructed controller is a complete one: its dispatchers are connected to it here rather than when
  // it is connected to an engine, which is called once per run and would append to the same list again.
  for (auto& dispatcher : this->dispatchers) {
    dispatcher->connect(this);
  }
}

Controller::~Controller() = default;

void Controller::notice(const Execution::Observable* observable) {
  // The engine subscribes every controller to the system state and announces each freshly installed one.
  // Caching it here lets the query methods read it without the caller passing it in.
  if (observable->getObservableType() == Execution::Observable::Type::SystemState) {
    systemState = static_cast<const Execution::SystemState*>(observable);
  }
  Execution::Controller::notice(observable);
}

std::vector<std::weak_ptr<const Execution::DecisionRequest>> Controller::getPendingRequests() const {
  std::vector<std::weak_ptr<const Execution::DecisionRequest>> requests;
  if (!systemState) {
    return requests;
  }
  auto add = [&](const auto& list) {
    for (const auto& tuple : list) {
      requests.push_back(std::get<1>(tuple));
    }
  };
  add(systemState->pendingEntryDecisions);
  add(systemState->pendingExitDecisions);
  add(systemState->pendingChoiceDecisions);
  add(systemState->pendingMessageDeliveryDecisions);
  return requests;
}

namespace {

/**
 * @brief Whether the token stands where a sequential performer stands.
 *
 * The model marks the node that performs, whether it carries the role itself or is an ad hoc subprocess
 * performing for its own children, and a token carrying no node stands at the process. A performer is
 * reported while it is busy, which is while the scope it stands in runs.
 *
 * @param token The token.
 * @return Whether the token is a busy performer.
 */
bool isSequentialPerformer(const Execution::Token& token) {
  if (!token.busy()) {
    return false;
  }
  const auto* baseElement = token.node
    ? static_cast<const BPMN::Node*>(token.node)
    : static_cast<const BPMN::Node*>(token.owner->process);
  const auto* extensionElements = baseElement->extensionElements
    ? baseElement->extensionElements->represents<const Model::ExtensionElements>()
    : nullptr;
  return extensionElements && extensionElements->hasSequentialPerformer;
}

/**
 * @brief Reports the performers held by one state machine and by everything running within it.
 *
 * A token owns the state machine of the scope it stands in, and a scope holds the event subprocesses it
 * has triggered, so the descent follows both.
 *
 * @param stateMachine The state machine to descend.
 * @param performers The performers found so far, appended to.
 */
void collectSequentialPerformers(
  const Execution::StateMachine& stateMachine,
  std::vector<Controller::SequentialPerformer>& performers) {
  for (const auto& token : stateMachine.tokens) {
    if (isSequentialPerformer(*token)) {
      Controller::SequentialPerformer performer;
      performer.performer = token;
      if (token->performing) {
        performer.performing = token->performing->weak_from_this();
      }
      // The list prunes what has expired as it is walked, which is a write, and the cached state is held
      // by const pointer: the walk is the engine's own and the constness is the cache's, not the list's.
      auto& waiting = const_cast<Execution::Token&>(*token).pendingSequentialEntries;
      for (const auto& [ candidate ] : waiting) {
        performer.waiting.push_back(candidate);
      }
      performers.push_back(std::move(performer));
    }
    if (token->owned) {
      collectSequentialPerformers(*token->owned, performers);
    }
  }
  if (stateMachine.interruptingEventSubProcess) {
    collectSequentialPerformers(*stateMachine.interruptingEventSubProcess, performers);
  }
  for (const auto& eventSubProcess : stateMachine.nonInterruptingEventSubProcesses) {
    collectSequentialPerformers(*eventSubProcess, performers);
  }
}

} // namespace

std::vector<Controller::SequentialPerformer> Controller::getSequentialPerformers() const {
  std::vector<SequentialPerformer> performers;
  if (!systemState) {
    return performers;
  }
  for (const auto& instance : systemState->instances) {
    collectSequentialPerformers(*instance, performers);
  }
  return performers;
}

std::vector<std::tuple<const Model::Attribute*, std::variant<EnumeratedChoice, BoundedChoice>>>
Controller::getChoiceCandidates(const Execution::DecisionRequest* request) const {
  std::vector<std::tuple<const Model::Attribute*, std::variant<EnumeratedChoice, BoundedChoice>>> candidates;
  const auto* token = request->token;
  if (!token->node || !token->node->extensionElements) {
    return candidates;
  }
  auto* extensionElements = token->node->extensionElements->as<Model::ExtensionElements>();
  if (!extensionElements) {
    return candidates;
  }
  for (const auto& choice : extensionElements->choices) {
    if (!choice->enumeration.empty()) {
      candidates.emplace_back(
        choice->attribute, choice->getEnumeration(token->status, *token->data, token->globals));
    }
    else {
      auto bounds = choice->getBounds(token->status, *token->data, token->globals);
      std::optional<BPMNOS::number> multipleOf;
      if (choice->multipleOf) {
        auto step = choice->multipleOf->execute(token->status, *token->data, token->globals);
        if (step.has_value()) {
          multipleOf = step.value();
        }
      }
      candidates.emplace_back(choice->attribute, BoundedChoice{bounds.first, bounds.second, multipleOf});
    }
  }
  return candidates;
}

std::vector<std::weak_ptr<const Execution::Message>> Controller::getMessageCandidates(
  const Execution::DecisionRequest* request) const {
  std::vector<std::weak_ptr<const Execution::Message>> candidates;
  const auto* token = request->token;
  if (!systemState || !token->node || !token->node->extensionElements) {
    return candidates;
  }
  auto* extensionElements = token->node->extensionElements->as<Model::ExtensionElements>();
  if (!extensionElements) {
    return candidates;
  }
  const auto* messageDefinition = extensionElements->getMessageDefinition(token->status);
  if (!messageDefinition) {
    return candidates;
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
    candidates.push_back(message);
  }
  return candidates;
}

std::expected<void, std::string> Controller::enqueueEntryDecision(
  std::weak_ptr<const Execution::DecisionRequest> request, std::optional<BPMNOS::Values> status) {
  auto locked = request.lock();
  if (!locked) {
    return std::unexpected("no matching pending decision");
  }
  enqueued->enqueue(std::make_shared<Execution::EntryEvent>(locked->token, std::move(status)));
  return {};
}

std::expected<void, std::string> Controller::enqueueExitDecision(
  std::weak_ptr<const Execution::DecisionRequest> request, std::optional<BPMNOS::Values> status) {
  auto locked = request.lock();
  if (!locked) {
    return std::unexpected("no matching pending decision");
  }
  enqueued->enqueue(std::make_shared<Execution::ExitEvent>(locked->token, std::move(status)));
  return {};
}

std::expected<void, std::string> Controller::enqueueChoiceDecision(
  std::weak_ptr<const Execution::DecisionRequest> request, std::vector<BPMNOS::number> choices) {
  auto locked = request.lock();
  if (!locked) {
    return std::unexpected("no matching pending decision");
  }
  enqueued->enqueue(std::make_shared<Execution::ChoiceEvent>(locked->token, std::move(choices)));
  return {};
}

std::expected<void, std::string> Controller::enqueueMessageDeliveryDecision(
  std::weak_ptr<const Execution::DecisionRequest> request, std::weak_ptr<const Execution::Message> message) {
  auto lockedRequest = request.lock();
  if (!lockedRequest) {
    return std::unexpected("no matching pending decision");
  }
  auto lockedMessage = message.lock();
  if (!lockedMessage) {
    return std::unexpected("no matching message");
  }
  enqueued->enqueue(
    std::make_shared<Execution::MessageDeliveryEvent>(lockedRequest->token, lockedMessage.get()));
  return {};
}

std::expected<void, std::string> Controller::enqueueClockTickEvent() {
  if (!systemState) {
    return std::unexpected("no system state");
  }
  enqueued->enqueue(std::make_shared<Execution::ClockTickEvent>(systemState));
  return {};
}

std::expected<void, std::string> Controller::enqueueTerminationEvent() {
  enqueued->enqueue(std::make_shared<Execution::TerminationEvent>());
  return {};
}

std::shared_ptr<Execution::Event> Controller::dispatchEvent(const Execution::SystemState* systemState) {
  // Walk the composition in order, exactly as the greedy controller walks its own: a decision is dispatched
  // only while feasible, and any other event is forwarded immediately. The queue is one of the dispatchers
  // and needs no case of its own, an enqueued decision being an event rather than a Decision.
  for (auto& dispatcher : dispatchers) {
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
  return nullptr;
}

} // namespace BPMNOS::WASM
