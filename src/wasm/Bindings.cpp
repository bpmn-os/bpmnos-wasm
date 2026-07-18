// JavaScript bindings for the WebAssembly build.
//
// The bridge exposes four classes to JavaScript through embind: the input, the engine, the
// controller, and the monitor. The caller assembles an input, constructs a monitor and, when it
// intends to supply decisions, a controller, and constructs an engine from the input. Every value
// that the C++ side expresses as JSON crosses the boundary as a JSON string, so the binding neither
// marshals nor depends on any particular JSON representation on the JavaScript side.
//
// This translation unit belongs to the WebAssembly build only. It lives under src/wasm and is
// compiled into the module by the Emscripten target, so it is never part of the native build.

#include <clocale>
#include <string>
#include <utility>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "Controller.h"
#include "Engine.h"
#include "Input.h"
#include "Monitor.h"

using namespace emscripten;
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
 * @brief Binds Controller::pendingDecisions, reporting the decisions left for the caller.
 *
 * @param controller The controller.
 * @return The JSON array of pending decisions as a string.
 */
std::string controllerPendingDecisions(Controller& controller) {
  return controller.pendingDecisions().dump();
}

/**
 * @brief Binds Controller::submitDecision, parsing the decision JSON string.
 *
 * @param controller The controller.
 * @param decision The decision as a JSON string.
 * @return The JSON result as a string.
 */
std::string controllerSubmitDecision(Controller& controller, const std::string& decision) {
  return controller.submitDecision(json::parse(decision)).dump();
}

/**
 * @brief Binds Controller::submitClockTick, queuing a clock tick.
 *
 * @param controller The controller.
 * @return The JSON result as a string.
 */
std::string controllerSubmitClockTick(Controller& controller) {
  return controller.submitClockTick().dump();
}

/**
 * @brief Binds Controller::submitTermination, queuing a termination.
 *
 * @param controller The controller.
 * @return The JSON result as a string.
 */
std::string controllerSubmitTermination(Controller& controller) {
  return controller.submitTermination().dump();
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
    .function("pendingDecisions", &controllerPendingDecisions)
    .function("submitDecision", &controllerSubmitDecision)
    .function("submitClockTick", &controllerSubmitClockTick)
    .function("submitTermination", &controllerSubmitTermination);

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
