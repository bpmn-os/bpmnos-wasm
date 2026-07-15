// JavaScript bindings for the WebAssembly build.
//
// The bridge exposes three classes to JavaScript through embind: the engine, the controller,
// and the monitor. The caller constructs a monitor and, when it intends to supply decisions, a
// controller, attaches them to an engine, and drives execution. Every value that the C++ side
// expresses as JSON crosses the boundary as a JSON string, so the binding neither marshals nor
// depends on any particular JSON representation on the JavaScript side.
//
// This translation unit is empty unless compiled by Emscripten.

#ifdef __EMSCRIPTEN__

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

std::string engineLoadModel(Engine& engine, const std::string& bpmnXml) {
  return engine.loadModel(bpmnXml).dump();
}
std::string engineLoadLookupTable(Engine& engine, const std::string& name, const std::string& csv) {
  return engine.loadLookupTable(name, csv).dump();
}
std::string engineLoadInstances(Engine& engine, const std::string& csv) {
  return engine.loadInstances(csv).dump();
}
std::string engineConfigure(Engine& engine, const std::string& config) {
  return engine.configure(json::parse(config)).dump();
}
std::string engineStart(Engine& engine) {
  return engine.start().dump();
}
std::string engineResume(Engine& engine) {
  return engine.resume().dump();
}
std::string engineSnapshot(Engine& engine) {
  return engine.snapshot().dump();
}

std::string controllerSubmitDecision(Controller& controller, const std::string& decision) {
  return controller.submitDecision(json::parse(decision)).dump();
}
std::string controllerSubmitTermination(Controller& controller) {
  return controller.submitTermination().dump();
}

std::string monitorDrainLog(Monitor& monitor) {
  return monitor.drainLog().dump();
}

// Registers a JavaScript callback that receives each log entry, as a JSON string, the moment it
// is recorded. On the demo this posts the entry from the worker to the page, so the log is shown
// as it is observed rather than only after the run completes.
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

// The module is a library of embind classes rather than a program, but Emscripten still
// expects an entry point. It runs once when the module instantiates and selects a UTF-8
// locale, so that xerces transcodes model attributes that contain multi-byte characters,
// such as the set membership sign in a choice condition, rather than dropping them.
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
    .function("submitTermination", &controllerSubmitTermination);

  class_<Engine>("Engine")
    .constructor<>()
    .function("attachMonitor", &Engine::attachMonitor, allow_raw_pointers())
    .function("attachController", &Engine::attachController, allow_raw_pointers())
    .function("loadModel", &engineLoadModel)
    .function("loadLookupTable", &engineLoadLookupTable)
    .function("loadInstances", &engineLoadInstances)
    .function("configure", &engineConfigure)
    .function("start", &engineStart)
    .function("resume", &engineResume)
    .function("snapshot", &engineSnapshot);
}

#endif // __EMSCRIPTEN__
