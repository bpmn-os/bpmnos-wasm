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

#include <string>

#include <emscripten/bind.h>

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

} // namespace

EMSCRIPTEN_BINDINGS(bpmnos_wasm) {
  class_<Monitor>("Monitor")
    .constructor<>()
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
