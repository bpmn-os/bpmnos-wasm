#include "Controller.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace BPMNOS::WASM {

Controller::Controller(std::vector<std::unique_ptr<Execution::EventDispatcher>> dispatchers)
  : dispatchers(std::move(dispatchers))
{
  // Every dispatcher speaks until it is silenced, so a composition that switches nothing runs as it always
  // did.
  active.assign(this->dispatchers.size(), true);

  // Find the one queue every enqueue writes into. The cast happens here and never again: the list owns the
  // queue, and the raw pointer is how enqueue reaches it without searching. Its position is kept with it,
  // since it is the one dispatcher that may not be silenced.
  for (std::size_t index = 0; index < this->dispatchers.size(); ++index) {
    if (auto* queue = dynamic_cast<EnqueuedEvents*>(this->dispatchers[index].get())) {
      if (enqueued) {
        throw std::runtime_error("controller composed of several EnqueuedEvents");
      }
      enqueued = queue;
      enqueuedIndex = index;
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

std::vector<const Model::Choice*> Controller::getChoices(const Execution::DecisionRequest* request) const {
  std::vector<const Model::Choice*> choices;
  const auto* token = request->token;
  if (!token->node || !token->node->extensionElements) {
    return choices;
  }
  auto* extensionElements = token->node->extensionElements->as<Model::ExtensionElements>();
  if (!extensionElements) {
    return choices;
  }
  for (const auto& choice : extensionElements->choices) {
    choices.push_back(choice.get());
  }
  return choices;
}

std::optional<std::tuple<const Model::Attribute*, std::variant<EnumeratedChoice, BoundedChoice>>>
Controller::getChoiceCandidates(
  const Execution::DecisionRequest* request, const std::vector<BPMNOS::number>& selectedValues) const {
  auto choices = getChoices(request);
  if (selectedValues.size() >= choices.size()) {
    return std::nullopt; // every choice has a value, so there is no next one
  }

  // Copied before anything is applied, so that computing an answer cannot write the run. The data is
  // copied by value: SharedValues holds references, and copying it would share the referents and make this
  // an act rather than a question. The current time is stamped as Engine::process stamps it before it
  // applies a choice, so that a condition reading the timestamp is evaluated against the value the engine
  // will use rather than the one the token was last noticed at.
  const auto* token = request->token;
  BPMNOS::Values status(token->status);
  BPMNOS::Values data(*token->data);
  BPMNOS::Values globals(token->globals);
  if (systemState) {
    status[Model::ExtensionElements::Index::Timestamp] = systemState->currentTime;
  }

  // The choices before the next one are applied in order, exactly as DecisionTask::determineAlternatives
  // applies them, since each is what the one after it is evaluated against.
  for (std::size_t i = 0; i < selectedValues.size(); ++i) {
    choices[i]->attributeRegistry.setValue(choices[i]->attribute, status, data, globals, selectedValues[i]);
  }

  const auto* choice = choices[selectedValues.size()];

  if (!choice->enumeration.empty()) {
    return std::make_tuple(choice->attribute, EnumeratedChoice(choice->getEnumeration(status, data, globals)));
  }

  auto [lower, upper] = choice->getBounds(status, data, globals);

  // A bounded choice need not state a discretizer, and getEnumeration is not asked for one, since it throws
  // where the attribute is a decimal. What it would have used is reported instead.
  std::optional<BPMNOS::number> multipleOf;
  if (choice->multipleOf) {
    if (auto step = choice->multipleOf->execute(status, data, globals); step.has_value()) {
      multipleOf = step.value();
    }
  }

  // Where the model states none, the type may imply one. An integer and a boolean take whole values by
  // construction, and getEnumeration steps them by one; only a decimal is left without a grid at all.
  // Reporting the implied step matters as much as reporting a stated one, since a caller told of no step
  // would offer values between the whole ones, which the choice does not admit.
  if (!multipleOf.has_value()
      && (choice->attribute->type == BPMNOS::INTEGER || choice->attribute->type == BPMNOS::BOOLEAN)) {
    multipleOf = BPMNOS::number(1);
  }

  // What may be selected: the multiples of the discretizer within the bounds, which is the grid the engine
  // admits, counted from zero and not from the lower bound. A caller's control counts its steps from the
  // minimum it is given, so the two agree only once that minimum is itself a multiple. The bounds are
  // reported unchanged beside them, since a caller cannot otherwise tell that a bound is unselectable.
  BPMNOS::number lowest = lower;
  BPMNOS::number highest = upper;

  if (multipleOf.has_value() && (double)multipleOf.value() != 0.0) {
    double delta = std::abs((double)multipleOf.value());
    double first = delta * std::ceil((double)lower / delta);
    double last = delta * std::floor((double)upper / delta);
    if (first > last) {
      return std::make_tuple(choice->attribute, EnumeratedChoice{}); // the grid holds nothing between them
    }
    lowest = first;
    highest = last;
    multipleOf = delta;
  }

  return std::make_tuple(choice->attribute, BoundedChoice{lower, upper, lowest, highest, multipleOf});
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

void Controller::requireIndex(std::size_t index) const {
  if (index >= dispatchers.size()) {
    throw std::runtime_error("controller holds no dispatcher at position " + std::to_string(index));
  }
}

void Controller::activate(std::size_t index) {
  requireIndex(index);
  active[index] = true;
}

void Controller::deactivate(std::size_t index) {
  requireIndex(index);
  if (index == enqueuedIndex) {
    throw std::runtime_error("the queue cannot be silenced");
  }
  active[index] = false;
}

bool Controller::isActive(std::size_t index) const {
  requireIndex(index);
  return active[index];
}

std::shared_ptr<Execution::Event> Controller::dispatchEvent(const Execution::SystemState* systemState) {
  // Walk the composition in order, exactly as the greedy controller walks its own: a decision is dispatched
  // only while feasible, and any other event is forwarded immediately. The queue is one of the dispatchers
  // and needs no case of its own, an enqueued decision being an event rather than a Decision.
  for (std::size_t index = 0; index < dispatchers.size(); ++index) {
    if (!active[index]) {
      // silenced: the walk carries past it, exactly as it carries past one that answers nothing
      continue;
    }
    if (auto event = dispatchers[index]->dispatchEvent(systemState)) {
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
