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
#include <string>
#include <utility>
#include <vector>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "Controller.h"
#include "Convert.h"
#include "Engine.h"
#include "Input.h"
#include "Monitor.h"

using namespace emscripten;
using namespace BPMNOS;
using namespace BPMNOS::WASM;

namespace {

/**
 * @brief Binds Input::requiredLookupTables, reporting the model's lookup table source names.
 *
 * @param input The input.
 * @return The JSON array of lookup table source names as a string.
 */
std::string inputRequiredLookupTables(Input& input) {
  return input.requiredLookupTables().dump();
}

/**
 * @brief Constructs an Engine, since embind cannot marshal a BPMNOS::Model::Input, which owns the parsed
 * tree. It parses the configuration JSON, moves the assembled input out of the JavaScript-held Input, and
 * hands it to the engine. The Input is empty afterwards, so one Input builds one Engine.
 *
 * @param input The assembled input, consumed here.
 * @param configJson The configuration as a JSON string: {"provider": ..., "seed": n}, each field optional.
 * @param monitor The monitor observing every run.
 * @param controller The controller supplying decisions, or null to run autonomously.
 * @return The constructed engine, owned by embind.
 */
Engine* createEngine(Input& input, const std::string& configJson, Monitor* monitor, Controller* controller) {
  json parsed = json::parse(configJson);
  Engine::Config config;
  if (parsed.contains("provider")) {
    config.provider = parsed["provider"].get<std::string>();
  }
  if (parsed.contains("seed")) {
    config.seed = parsed["seed"].get<unsigned int>();
  }
  return new Engine(input.release(), std::move(config), monitor, controller);
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
 * @brief Binds Controller::getChoiceCandidates, reporting the candidate values of a choice request, each
 * rendered by its attribute's type.
 *
 * @param controller The controller.
 * @param instanceId The token's instance identity.
 * @param nodeId The token's node identity.
 * @return A JSON array of {attribute, enumeration | {lowerBound, upperBound, multipleOf?}} as a string.
 */
std::string controllerGetChoiceCandidates(
  Controller& controller, const std::string& instanceId, const std::string& nodeId) {
  json out = json::array();
  auto request = resolveRequest(controller, instanceId, nodeId, Execution::Observable::Type::ChoiceRequest).lock();
  if (!request) {
    return out.dump();
  }
  for (const auto& [attribute, values] : controller.getChoiceCandidates(request.get())) {
    json entry;
    entry["attribute"] = attribute->name;
    if (std::holds_alternative<EnumeratedChoice>(values)) {
      json enumeration = json::array();
      for (auto value : std::get<EnumeratedChoice>(values)) {
        enumeration.push_back(renderChoiceValue(value, attribute->type));
      }
      entry["enumeration"] = enumeration;
    }
    else {
      const auto& bounds = std::get<BoundedChoice>(values);
      entry["lowerBound"] = renderChoiceValue(std::get<0>(bounds), attribute->type);
      entry["upperBound"] = renderChoiceValue(std::get<1>(bounds), attribute->type);
      if (std::get<2>(bounds)) {
        entry["multipleOf"] = renderChoiceValue(*std::get<2>(bounds), attribute->type);
      }
    }
    out.push_back(std::move(entry));
  }
  return out.dump();
}

/**
 * @brief Binds Controller::getMessageCandidates, reporting the messages deliverable to a delivery request.
 *
 * @param controller The controller.
 * @param instanceId The token's instance identity.
 * @param nodeId The token's node identity.
 * @return A JSON array of {origin, sender, message} as a string.
 */
std::string controllerGetMessageCandidates(
  Controller& controller, const std::string& instanceId, const std::string& nodeId) {
  json out = json::array();
  auto request =
    resolveRequest(controller, instanceId, nodeId, Execution::Observable::Type::MessageDeliveryRequest).lock();
  if (!request) {
    return out.dump();
  }
  for (const auto& weak : controller.getMessageCandidates(request.get())) {
    auto message = weak.lock();
    if (!message) {
      continue;
    }
    auto content = message->jsonify();
    json candidate;
    candidate["origin"] = content.value("origin", json());
    candidate["sender"] = content.contains("header") ? content["header"].value("sender", json()) : json();
    candidate["message"] = std::move(content);
    out.push_back(std::move(candidate));
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
  // Encode each chosen value to a number by its attribute's type, the inverse of renderChoiceValue.
  auto candidates = controller.getChoiceCandidates(locked.get());
  const json& choicesJson = decision.at("choices");
  std::vector<BPMNOS::number> choices;
  for (std::size_t index = 0; index < candidates.size() && index < choicesJson.size(); ++index) {
    const auto* attribute = std::get<0>(candidates[index]);
    const json& value = choicesJson[index];
    if (attribute->type == BPMNOS::STRING) {
      choices.push_back(BPMNOS::to_number(value.get<std::string>(), BPMNOS::STRING));
    }
    else {
      choices.push_back(toNumber(value.get<double>()));
    }
  }
  return enqueueResult(controller.enqueueChoiceDecision(request, std::move(choices)));
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
 * @brief Binds Controller::enqueueClockTick.
 *
 * @param controller The controller.
 * @return The JSON result as a string.
 */
std::string controllerEnqueueClockTick(Controller& controller) {
  return enqueueResult(controller.enqueueClockTick());
}

/**
 * @brief Binds Controller::enqueueTermination.
 *
 * @param controller The controller.
 * @return The JSON result as a string.
 */
std::string controllerEnqueueTermination(Controller& controller) {
  return enqueueResult(controller.enqueueTermination());
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
  class_<Monitor>("Monitor")
    .constructor<>()
    .function("addObserver", &monitorAddObserver);

  class_<Controller>("Controller")
    .constructor<>()
    .function("getPendingDecisions", &controllerGetPendingDecisions)
    .function("getChoiceCandidates", &controllerGetChoiceCandidates)
    .function("getMessageCandidates", &controllerGetMessageCandidates)
    .function("enqueueEntryDecision", &controllerEnqueueEntryDecision)
    .function("enqueueExitDecision", &controllerEnqueueExitDecision)
    .function("enqueueChoiceDecision", &controllerEnqueueChoiceDecision)
    .function("enqueueMessageDeliveryDecision", &controllerEnqueueMessageDeliveryDecision)
    .function("enqueueClockTick", &controllerEnqueueClockTick)
    .function("enqueueTermination", &controllerEnqueueTermination);

  class_<Input>("Input")
    .constructor<std::string>()
    .function("requiredLookupTables", &inputRequiredLookupTables)
    .function("addLookupTable", &Input::addLookupTable)
    .function("setInstance", &Input::setInstance);

  class_<Engine>("Engine")
    .constructor(&createEngine, allow_raw_pointers())
    .function("run", &Engine::run)
    .function("resume", &Engine::resume)
    .function("isAlive", &Engine::isAlive)
    .function("getCurrentTime", &Engine::getCurrentTime);
}
