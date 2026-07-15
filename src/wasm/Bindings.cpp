// JavaScript bindings for the WebAssembly build.
//
// The bridge exposes three classes to JavaScript through embind: the engine, the controller,
// and the monitor. The caller constructs a monitor and, when it intends to supply decisions, a
// controller, attaches them to an engine, and drives execution. Every value that the C++ side
// expresses as JSON crosses the boundary as a JSON string, so the binding neither marshals nor
// depends on any particular JSON representation on the JavaScript side.
//
// This translation unit belongs to the WebAssembly build only. It lives under src/wasm and is
// compiled into the module by the Emscripten target, so it is never part of the native build.

#include <clocale>
#include <string>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "Controller.h"
#include "Engine.h"
#include "Monitor.h"

using namespace emscripten;
using namespace BPMNOS::WASM;

namespace {

/**
 * @brief Binds Engine::loadModel, loading the BPMN model XML.
 *
 * @param engine The engine.
 * @param bpmnXml The BPMN model XML.
 * @return The JSON result as a string.
 */
std::string engineLoadModel(Engine& engine, const std::string& bpmnXml) {
  return engine.loadModel(bpmnXml).dump();
}

/**
 * @brief Binds Engine::requiredLookups, reporting the model's lookup table source names.
 *
 * @param engine The engine.
 * @return The JSON array of lookup table source names as a string.
 */
std::string engineRequiredLookups(Engine& engine) {
  return engine.requiredLookups().dump();
}

/**
 * @brief Binds Engine::loadLookupTable, loading one lookup table's content.
 *
 * @param engine The engine.
 * @param name The lookup table source name.
 * @param csv The lookup table CSV content.
 * @return The JSON result as a string.
 */
std::string engineLoadLookupTable(Engine& engine, const std::string& name, const std::string& csv) {
  return engine.loadLookupTable(name, csv).dump();
}

/**
 * @brief Binds Engine::loadInstances, loading the instance data.
 *
 * @param engine The engine.
 * @param csv The instance CSV content.
 * @return The JSON result as a string.
 */
std::string engineLoadInstances(Engine& engine, const std::string& csv) {
  return engine.loadInstances(csv).dump();
}

/**
 * @brief Binds Engine::configure, parsing the configuration JSON string.
 *
 * @param engine The engine.
 * @param config The configuration as a JSON string.
 * @return The JSON result as a string.
 */
std::string engineConfigure(Engine& engine, const std::string& config) {
  return engine.configure(json::parse(config)).dump();
}

/**
 * @brief Binds Engine::start, running the engine from the start.
 *
 * @param engine The engine.
 * @return The snapshot as a JSON string.
 */
std::string engineStart(Engine& engine) {
  return engine.start().dump();
}

/**
 * @brief Binds Engine::resume, continuing a started run.
 *
 * @param engine The engine.
 * @return The snapshot as a JSON string.
 */
std::string engineResume(Engine& engine) {
  return engine.resume().dump();
}

/**
 * @brief Binds Engine::snapshot, returning the current snapshot without advancing.
 *
 * @param engine The engine.
 * @return The snapshot as a JSON string.
 */
std::string engineSnapshot(Engine& engine) {
  return engine.snapshot().dump();
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
 * @brief Binds Monitor::drainLog, returning the log entries since the previous drain.
 *
 * @param monitor The monitor.
 * @return The JSON array of log entries as a string.
 */
std::string monitorDrainLog(Monitor& monitor) {
  return monitor.drainLog().dump();
}

/**
 * @brief Registers a JavaScript callback that receives each log entry, as a JSON string, the moment it
 * is recorded. On the demo this posts the entry from the worker to the page, so the log is shown as it
 * is observed rather than only after the run completes.
 *
 * @param monitor The monitor.
 * @param callback The JavaScript callback, or null or undefined to remove the sink.
 */
void monitorOnNotice(Monitor& monitor, val callback) {
  if (callback.isNull() || callback.isUndefined()) {
    monitor.onNotice(nullptr);
    return;
  }
  monitor.onNotice([callback](const json& entry) {
    callback(entry.dump());
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
    .function("onNotice", &monitorOnNotice)
    .function("drainLog", &monitorDrainLog);

  class_<Controller>("Controller")
    .constructor<>()
    .function("submitDecision", &controllerSubmitDecision)
    .function("submitClockTick", &controllerSubmitClockTick)
    .function("submitTermination", &controllerSubmitTermination);

  class_<Engine>("Engine")
    .constructor<>()
    .function("attachMonitor", &Engine::attachMonitor, allow_raw_pointers())
    .function("attachController", &Engine::attachController, allow_raw_pointers())
    .function("loadModel", &engineLoadModel)
    .function("requiredLookups", &engineRequiredLookups)
    .function("loadLookupTable", &engineLoadLookupTable)
    .function("loadInstances", &engineLoadInstances)
    .function("configure", &engineConfigure)
    .function("start", &engineStart)
    .function("resume", &engineResume)
    .function("snapshot", &engineSnapshot);
}
