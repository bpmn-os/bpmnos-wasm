// JavaScript bindings for the WebAssembly build.
//
// The bridge exposes four classes to JavaScript through embind: the input, the engine, the
// controller, and the monitor. The C++ classes are native to the engine and hold no JSON; this
// translation unit is the entire JSON boundary. It parses the caller's JSON into the native arguments
// the classes take, resolves the caller's identities to the native handles they expect, and serialises
// the native results back to JSON strings.
//
// This translation unit belongs to the WebAssembly build only. It lives under src/wasm and is
// compiled into the module by the Emscripten target, so it is never part of the native build.

#include <clocale>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "Controller.h"
#include "Structure.h"
#include "Convert.h"
#include "Engine.h"
#include "EnqueuedEvents.h"
#include "Input.h"
#include "Monitor.h"

using namespace emscripten;
using namespace BPMNOS;
using namespace BPMNOS::WASM;

namespace {

/**
 * @brief Binds Input::getLookupTableNames, reporting the model's lookup table source names.
 *
 * @param input The input.
 * @return The JSON array of lookup table source names as a string.
 */
std::string inputGetLookupTableNames(Input& input) {
  return input.getLookupTableNames().dump();
}

/**
 * @brief Builds the data provider the caller named, moving the assembled input into it.
 *
 * The names are the four kinds the engine offers; an unknown one is refused rather than defaulted, so a
 * misspelling is an error at construction rather than a run of the wrong flavour.
 *
 * @param input The assembled input, consumed here. The Input is empty afterwards, so one Input builds one
 * provider.
 * @param providerJson {"provider": "static"|"expected"|"dynamic"|"stochastic", "seed": n}, both optional.
 * @return The provider, owned by the engine it is handed to.
 */
std::unique_ptr<Model::DataProvider> createDataProvider(Input& input, const std::string& providerJson) {
  json parsed = json::parse(providerJson);
  std::string provider = parsed.value("provider", std::string("stochastic"));
  if (provider == "static") {
    return std::make_unique<Model::StaticDataProvider>(input.release());
  }
  if (provider == "expected") {
    return std::make_unique<Model::ExpectedValueDataProvider>(input.release());
  }
  if (provider == "dynamic") {
    return std::make_unique<Model::DynamicDataProvider>(input.release());
  }
  if (provider == "stochastic") {
    // The base seed is fixed here; the scenario index a run adds is what varies the sample.
    return std::make_unique<Model::StochasticDataProvider>(input.release(), parsed.value("seed", 0u));
  }
  throw std::runtime_error("unknown provider: " + provider);
}

/**
 * @brief Constructs an Engine, since embind cannot marshal a BPMNOS::Model::Input, which owns the parsed
 * tree, nor a data provider built from it.
 *
 * @param input The assembled input, consumed here.
 * @param providerJson The data provider to build, as documented on createDataProvider.
 * @param controller The controller driving every run.
 * @param monitor The monitor observing every run, or null for an unobserved one.
 * @return The constructed engine, owned by embind.
 */
Engine* createEngine(Input& input, const std::string& providerJson,
                     std::shared_ptr<Controller> controller, std::shared_ptr<Monitor> monitor) {
  return new Engine(createDataProvider(input, providerJson), std::move(controller), std::move(monitor));
}

/**
 * @brief Builds the evaluator the caller named, shared by every dispatcher built against it.
 *
 * @param name The evaluator class: "GuidedEvaluator" or "LocalEvaluator".
 * @return The evaluator.
 */
std::shared_ptr<Execution::Evaluator> createEvaluator(const std::string& name) {
  if (name == "GuidedEvaluator") {
    return std::make_shared<Execution::GuidedEvaluator>();
  }
  if (name == "LocalEvaluator") {
    return std::make_shared<Execution::LocalEvaluator>();
  }
  throw std::runtime_error("unknown evaluator: " + name);
}

/**
 * @brief Builds the dispatcher the caller named.
 *
 * A name is the class of the dispatcher, or, for the ones a GreedyDispatcher drives, the candidates class
 * that distinguishes it. Each of those keeps a share of the evaluator, so the composition needs no other
 * holder of it. Metronome is written with its clock tick duration in milliseconds, as "Metronome(500)", or
 * without it for the engine's own default. An unknown name is refused, since a silently dropped dispatcher
 * is a run that quietly decides something else than the caller composed.
 *
 * @param name The dispatcher to build.
 * @param evaluator The evaluator the evaluating ones are built against.
 * @return The dispatcher.
 */
std::unique_ptr<Execution::EventDispatcher> createDispatcher(
  const std::string& name, const std::shared_ptr<Execution::Evaluator>& evaluator) {
  if (name == "FirstFeasibleExit") {
    return std::make_unique<Execution::GreedyDispatcher<Execution::FirstFeasibleExit>>(evaluator);
  }
  if (name == "FirstFeasibleEntry") {
    return std::make_unique<Execution::GreedyDispatcher<Execution::FirstFeasibleEntry>>(evaluator);
  }
  if (name == "FirstEnumeratedChoice") {
    return std::make_unique<Execution::GreedyDispatcher<Execution::FirstEnumeratedChoice>>(evaluator);
  }
  if (name == "FirstBisectionalChoice") {
    return std::make_unique<Execution::GreedyDispatcher<Execution::FirstBisectionalChoice>>(evaluator);
  }
  if (name == "SequentialEntries") {
    return std::make_unique<Execution::GreedyDispatcher<Execution::SequentialEntries>>(evaluator);
  }
  if (name == "MessageDeliveries") {
    return std::make_unique<Execution::GreedyDispatcher<Execution::MessageDeliveries>>(evaluator);
  }
  if (name == "CompetingCandidates") {
    return std::make_unique<Execution::GreedyDispatcher<Execution::CompetingCandidates>>(evaluator);
  }
  if (name == "InstantDirectMessage") {
    return std::make_unique<Execution::InstantDirectMessage>();
  }
  if (name == "TimeWarp") {
    return std::make_unique<Execution::TimeWarp>();
  }
  if (name == "Metronome") {
    return std::make_unique<Execution::Metronome>();
  }
  if (name.starts_with("Metronome(") && name.back() == ')') {
    // Ticks the clock in step with real time, so a run advances at the pace of a wall clock rather than as
    // fast as it can; the argument is how long a tick lasts.
    return std::make_unique<Execution::Metronome>(
      static_cast<unsigned int>(std::stoul(name.substr(std::string("Metronome(").size()))));
  }
  if (name == "EnqueuedEvents") {
    return std::make_unique<EnqueuedEvents>();
  }
  throw std::runtime_error("unknown dispatcher: " + name);
}

/**
 * @brief Constructs a Controller from the composition the caller named.
 *
 * @param compositionJson {"evaluator": name, "dispatchers": [ name, ... ]}, the evaluator optional and
 * defaulting to the guided one, the dispatchers walked in the order given and holding exactly one
 * "EnqueuedEvents".
 * @return The constructed controller, owned by embind and shared with the engine it drives.
 */
std::shared_ptr<Controller> createController(const std::string& compositionJson) {
  json parsed = json::parse(compositionJson);
  auto evaluator = createEvaluator(parsed.value("evaluator", std::string("GuidedEvaluator")));
  std::vector<std::unique_ptr<Execution::EventDispatcher>> dispatchers;
  for (const auto& name : parsed.at("dispatchers")) {
    dispatchers.push_back(createDispatcher(name.get<std::string>(), evaluator));
  }
  return std::make_shared<Controller>(std::move(dispatchers));
}

/**
 * @brief Maps a decision request kind to the caller's decision-type string.
 *
 * @param type The observable type of the request.
 * @return "entry", "exit", "choice", "messageDelivery", or empty.
 */
std::string decisionType(Execution::Observable::Type type) {
  using Type = Execution::Observable::Type;
  switch (type) {
    case Type::EntryRequest: return "entry";
    case Type::ExitRequest: return "exit";
    case Type::ChoiceRequest: return "choice";
    case Type::MessageDeliveryRequest: return "messageDelivery";
    default: return "";
  }
}

/**
 * @brief Resolves the caller's identity to a pending request of the given kind whose token matches.
 *
 * @param controller The controller.
 * @param instanceId The token's instance identity.
 * @param nodeId The token's node identity.
 * @param type The request kind.
 * @return The matching request, or an expired pointer when none matches.
 */
std::weak_ptr<const Execution::DecisionRequest> resolveRequest(
  Controller& controller, const std::string& instanceId, const std::string& nodeId,
  Execution::Observable::Type type) {
  for (const auto& weak : controller.getPendingRequests()) {
    auto request = weak.lock();
    if (!request || request->type != type) {
      continue;
    }
    auto identity = request->token->jsonify();
    if (identity.value("instanceId", std::string()) == instanceId &&
        identity.value("nodeId", std::string()) == nodeId) {
      return weak;
    }
  }
  return {};
}

/**
 * @brief Translates an enqueue result to the JSON result string.
 *
 * @param result The result of a Controller enqueue.
 * @return {"queued": true} on acceptance, or {"rejected": reason}, as a string.
 */
std::string enqueueResult(const std::expected<void, std::string>& result) {
  return (result ? json{ {"queued", true} } : json{ {"rejected", result.error()} }).dump();
}

/**
 * @brief Binds Controller::getPendingRequests, reporting the pending decisions left for the caller.
 *
 * @param controller The controller.
 * @return A JSON array of {type, instanceId, nodeId} as a string.
 */
std::string controllerGetPendingDecisions(Controller& controller) {
  json out = json::array();
  for (const auto& weak : controller.getPendingRequests()) {
    auto request = weak.lock();
    if (!request) {
      continue;
    }
    auto identity = request->token->jsonify();
    json entry;
    entry["type"] = decisionType(request->type);
    entry["instanceId"] = identity.value("instanceId", json());
    entry["nodeId"] = identity.value("nodeId", json());
    out.push_back(std::move(entry));
  }
  return out.dump();
}

/**
 * @brief Renders a choice number as the JSON value of its attribute's type, so a string choice reads as
 * its label rather than a registry index.
 *
 * @param value The choice number.
 * @param type The attribute's value type.
 * @return The typed JSON value.
 */
json renderChoiceValue(BPMNOS::number value, BPMNOS::ValueType type) {
  switch (type) {
    case BPMNOS::STRING: return BPMNOS::to_string(value, BPMNOS::STRING);
    case BPMNOS::BOOLEAN: return static_cast<bool>(static_cast<int>(static_cast<double>(value)));
    case BPMNOS::INTEGER: return static_cast<int>(static_cast<double>(value));
    default: return static_cast<double>(value);
  }
}

/**
 * @brief Encodes the values a caller has already selected, by the type of the attribute each choice names.
 *
 * The inverse of renderChoiceValue, applied position by position. A position holding no value ends the
 * prefix, since what follows an unmade choice is not made either, and a value beyond the choices the task
 * states is dropped.
 *
 * @param choices The choices of the decision task, in the order they are made.
 * @param selectedValues The values selected so far, as JSON.
 * @return The values as numbers, in the same order.
 */
std::vector<BPMNOS::number> toChoiceValues(
  const std::vector<const Model::Choice*>& choices, const json& selectedValues) {
  std::vector<BPMNOS::number> values;
  if (!selectedValues.is_array()) {
    return values;
  }
  for (std::size_t index = 0; index < choices.size() && index < selectedValues.size(); ++index) {
    const json& selectedValue = selectedValues[index];
    if (selectedValue.is_null()) {
      break;
    }
    if (choices[index]->attribute->type == BPMNOS::STRING) {
      values.push_back(BPMNOS::to_number(selectedValue.get<std::string>(), BPMNOS::STRING));
    }
    else if (selectedValue.is_boolean()) {
      values.push_back(toNumber(selectedValue.get<bool>() ? 1.0 : 0.0));
    }
    else {
      values.push_back(toNumber(selectedValue.get<double>()));
    }
  }
  return values;
}

/**
 * @brief Binds Controller::getChoiceCandidates, reporting the next choice a decision task waits for, given
 * the values selected so far, with its values rendered by its attribute's type.
 *
 * Three answers are distinguished, and a caller acts differently on each. An empty object says the request
 * no longer stands, so the decision has been overtaken and is to be dropped. A complete flag says every
 * choice has a value, so the decision may be submitted. Anything else is the next choice to be made.
 *
 * @param controller The controller.
 * @param instanceId The token's instance identity.
 * @param nodeId The token's node identity.
 * @param selectedValues The values selected so far, as a JSON array string.
 * @return {} | {"complete":true} | {"attribute":s, enumeration | {lowerBound, upperBound, multipleOf?}}.
 */
std::string controllerGetChoiceCandidates(
  Controller& controller, const std::string& instanceId, const std::string& nodeId,
  const std::string& selectedValues) {
  json out = json::object();
  auto request = resolveRequest(controller, instanceId, nodeId, Execution::Observable::Type::ChoiceRequest).lock();
  if (!request) {
    return out.dump();
  }

  auto choices = controller.getChoices(request.get());
  auto candidates = controller.getChoiceCandidates(
    request.get(), toChoiceValues(choices, json::parse(selectedValues)));

  if (!candidates.has_value()) {
    out["complete"] = true;
    return out.dump();
  }

  const auto& [attribute, values] = candidates.value();
  out["attribute"] = attribute->name;
  if (std::holds_alternative<EnumeratedChoice>(values)) {
    json enumeration = json::array();
    for (auto value : std::get<EnumeratedChoice>(values)) {
      enumeration.push_back(renderChoiceValue(value, attribute->type));
    }
    out["enumeration"] = enumeration;
  }
  else {
    // Both pairs are reported. The bounds are what the condition states; the least and the greatest are what
    // may actually be selected, and they differ where a step the engine cannot hold exactly puts its grid
    // beside the values the model states. A caller comparing them is what tells a reader that a bound is
    // not among the values on offer.
    const auto& bounds = std::get<BoundedChoice>(values);
    out["lowerBound"] = renderChoiceValue(bounds.lowerBound, attribute->type);
    out["upperBound"] = renderChoiceValue(bounds.upperBound, attribute->type);
    out["lowest"] = renderChoiceValue(bounds.lowest, attribute->type);
    out["highest"] = renderChoiceValue(bounds.highest, attribute->type);
    if (bounds.multipleOf) {
      out["multipleOf"] = renderChoiceValue(*bounds.multipleOf, attribute->type);
    }
  }
  return out.dump();
}

/**
 * @brief Binds Controller::enqueueEntryDecision, resolving the request and parsing the status.
 *
 * @param controller The controller.
 * @param decisionJson {"instanceId": s, "nodeId": s, "status": [ ... ]?} as a string.
 * @return The JSON result as a string.
 */
std::string controllerEnqueueEntryDecision(Controller& controller, const std::string& decisionJson) {
  json decision = json::parse(decisionJson);
  if (!decision.contains("instanceId") || !decision.contains("nodeId")) {
    return json{ {"rejected", "decision requires instanceId and nodeId"} }.dump();
  }
  auto request = resolveRequest(controller, decision["instanceId"].get<std::string>(),
    decision["nodeId"].get<std::string>(), Execution::Observable::Type::EntryRequest);
  std::optional<BPMNOS::Values> status;
  if (decision.contains("status") && !decision["status"].is_null()) {
    status = toValues(decision["status"]);
  }
  return enqueueResult(controller.enqueueEntryDecision(request, status));
}

/**
 * @brief Binds Controller::enqueueExitDecision, resolving the request and parsing the status.
 *
 * @param controller The controller.
 * @param decisionJson {"instanceId": s, "nodeId": s, "status": [ ... ]?} as a string.
 * @return The JSON result as a string.
 */
std::string controllerEnqueueExitDecision(Controller& controller, const std::string& decisionJson) {
  json decision = json::parse(decisionJson);
  if (!decision.contains("instanceId") || !decision.contains("nodeId")) {
    return json{ {"rejected", "decision requires instanceId and nodeId"} }.dump();
  }
  auto request = resolveRequest(controller, decision["instanceId"].get<std::string>(),
    decision["nodeId"].get<std::string>(), Execution::Observable::Type::ExitRequest);
  std::optional<BPMNOS::Values> status;
  if (decision.contains("status") && !decision["status"].is_null()) {
    status = toValues(decision["status"]);
  }
  return enqueueResult(controller.enqueueExitDecision(request, status));
}

/**
 * @brief Binds Controller::enqueueChoiceDecision, resolving the request and parsing the choices.
 *
 * @param controller The controller.
 * @param decisionJson {"instanceId": s, "nodeId": s, "choices": [ ... ]} as a string.
 * @return The JSON result as a string.
 */
std::string controllerEnqueueChoiceDecision(Controller& controller, const std::string& decisionJson) {
  json decision = json::parse(decisionJson);
  if (!decision.contains("instanceId") || !decision.contains("nodeId")) {
    return json{ {"rejected", "decision requires instanceId and nodeId"} }.dump();
  }
  if (!decision.contains("choices")) {
    return json{ {"rejected", "choice requires choices"} }.dump();
  }
  auto request = resolveRequest(controller, decision["instanceId"].get<std::string>(),
    decision["nodeId"].get<std::string>(), Execution::Observable::Type::ChoiceRequest);
  auto locked = request.lock();
  if (!locked) {
    return json{ {"rejected", "no matching pending decision"} }.dump();
  }
  // The values are encoded by the type of the attribute each choice names, which is read from the choices
  // themselves rather than from their candidates: what a choice may take is an answer for a prefix, and
  // nothing here needs one.
  //
  // They stay positional, where the engine's own record of a choice names its attributes. A decision task
  // is not forbidden to state two choices on the same attribute, and a named object would merge them and
  // lose a value, which a record being read can afford and a decision being made cannot.
  auto choices = controller.getChoices(locked.get());
  return enqueueResult(controller.enqueueChoiceDecision(
    request, toChoiceValues(choices, decision.at("choices"))));
}

/**
 * @brief Binds Controller::enqueueMessageDeliveryDecision, resolving the request and the chosen message by
 * its origin and sender among the request's candidates.
 *
 * @param controller The controller.
 * @param decisionJson {"instanceId": s, "nodeId": s, "origin": s, "sender": s} as a string.
 * @return The JSON result as a string.
 */
std::string controllerEnqueueMessageDeliveryDecision(Controller& controller, const std::string& decisionJson) {
  json decision = json::parse(decisionJson);
  if (!decision.contains("instanceId") || !decision.contains("nodeId")) {
    return json{ {"rejected", "decision requires instanceId and nodeId"} }.dump();
  }
  if (!decision.contains("origin") || !decision.contains("sender")) {
    return json{ {"rejected", "messageDelivery requires origin and sender"} }.dump();
  }
  auto request = resolveRequest(controller, decision["instanceId"].get<std::string>(),
    decision["nodeId"].get<std::string>(), Execution::Observable::Type::MessageDeliveryRequest);
  std::weak_ptr<const Execution::Message> message;
  if (auto locked = request.lock()) {
    std::string origin = decision["origin"].get<std::string>();
    std::string sender = decision["sender"].get<std::string>();
    for (const auto& weak : controller.getMessageCandidates(locked.get())) {
      auto candidate = weak.lock();
      if (!candidate) {
        continue;
      }
      auto content = candidate->jsonify();
      std::string candidateSender =
        content.contains("header") ? content["header"].value("sender", std::string()) : std::string();
      if (content.value("origin", std::string()) == origin && candidateSender == sender) {
        message = weak;
        break;
      }
    }
  }
  return enqueueResult(controller.enqueueMessageDeliveryDecision(request, message));
}

/**
 * @brief Binds Controller::enqueueClockTickEvent.
 *
 * @param controller The controller.
 * @return The JSON result as a string.
 */
std::string controllerEnqueueClockTickEvent(Controller& controller) {
  return enqueueResult(controller.enqueueClockTickEvent());
}

/**
 * @brief Binds Controller::enqueueTerminationEvent.
 *
 * @param controller The controller.
 * @return The JSON result as a string.
 */
std::string controllerEnqueueTerminationEvent(Controller& controller) {
  return enqueueResult(controller.enqueueTerminationEvent());
}

/**
 * @brief Registers a JavaScript observer that receives each entry, as a JSON string, the moment it is
 * recorded. Every registered observer receives every entry, so a caller attaches one per module that
 * needs the stream. On the demo this posts the entry from the worker to the page, so the log is shown as
 * it is observed rather than only after the run completes.
 *
 * @param monitor The monitor.
 * @param observer The JavaScript observer invoked per notification.
 */
void monitorAddObserver(Monitor& monitor, val observer) {
  monitor.addObserver([observer](const json& entry) {
    observer(entry.dump());
  });
}

/**
 * @brief Binds describeModel, reporting what the model resolves.
 *
 * @param descriptionJson {"model": s, "lookupTables": {name: csv}?} as a string.
 * @return {"sequentialPerformers": [{"performer": s, "activities": [s]}]} or {"error": message}, as a
 *         string.
 */
std::string describeModelBinding(const std::string& descriptionJson) {
  return guarded([&] {
    json description = json::parse(descriptionJson);
    if (!description.contains("model")) {
      throw std::runtime_error("description requires a model");
    }
    std::unordered_map<std::string, std::string> lookupTables;
    if (description.contains("lookupTables")) {
      for (const auto& [name, csv] : description["lookupTables"].items()) {
        lookupTables[name] = csv.get<std::string>();
      }
    }
    return describeModel(description["model"].get<std::string>(), lookupTables);
  }).dump();
}

} // namespace

/**
 * @brief The module's entry point. It is a library of embind classes rather than a program, but
 * Emscripten still expects a main; it runs once when the module instantiates and selects a UTF-8 locale,
 * so that xerces transcodes model attributes that contain multi-byte characters, such as the set
 * membership sign in a choice condition, rather than dropping them.
 *
 * @return Zero.
 */
int main() {
  std::setlocale(LC_ALL, "C.UTF-8");
  return 0;
}

EMSCRIPTEN_BINDINGS(bpmnos_wasm) {
  // What the model resolves, which a caller needs whether or not it runs anything.
  emscripten::function("describeModel", &describeModelBinding);

  // The monitor and the controller are shared with the engine they are handed to, and each is constructed
  // one way only: an instance held raw in one place and shared in another is how embind double frees.
  class_<Monitor>("Monitor")
    .smart_ptr_constructor("Monitor", &std::make_shared<Monitor>)
    .function("addObserver", &monitorAddObserver);

  class_<Controller>("Controller")
    .smart_ptr_constructor("Controller", &createController)
    .function("getPendingDecisions", &controllerGetPendingDecisions)
    .function("getChoiceCandidates", &controllerGetChoiceCandidates)
    .function("enqueueEntryDecision", &controllerEnqueueEntryDecision)
    .function("enqueueExitDecision", &controllerEnqueueExitDecision)
    .function("enqueueChoiceDecision", &controllerEnqueueChoiceDecision)
    .function("enqueueMessageDeliveryDecision", &controllerEnqueueMessageDeliveryDecision)
    .function("enqueueClockTickEvent", &controllerEnqueueClockTickEvent)
    .function("enqueueTerminationEvent", &controllerEnqueueTerminationEvent);

  class_<Input>("Input")
    .constructor<std::string>()
    .function("getLookupTableNames", &inputGetLookupTableNames)
    .function("addLookupTable", &Input::addLookupTable)
    .function("setInstance", &Input::setInstance);

  class_<Engine>("Engine")
    .constructor(&createEngine, allow_raw_pointers())
    .function("run", &Engine::run)
    .function("resume", &Engine::resume)
    .function("isAlive", &Engine::isAlive)
    .function("getCurrentTime", &Engine::getCurrentTime)
    .function("getWeightedObjective", &Engine::getWeightedObjective);
}
