#ifndef BPMNOS_WASM_ENGINE_H
#define BPMNOS_WASM_ENGINE_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <bpmn++.h>
#include <bpmnos-model.h>
#include <bpmnos-execution.h>

namespace BPMNOS::WASM {

class Monitor;
class Controller;

using json = nlohmann::ordered_json;

/// Owner of one execution engine and the driver of its lifecycle.
///
/// This class holds the engine's execution engine together with the data provider and
/// scenario built from the loaded model, lookup tables, and instance data. It always connects a
/// monitor. A controller is optional: when the caller attaches one, that caller supplies the
/// decisions and drives execution by starting the engine, reading the snapshot, submitting a
/// decision, and resuming until execution is done. When no controller is attached, the engine
/// instead runs autonomously, mirroring the engine's own greedy application by connecting a
/// greedy controller with the guided evaluator and the time-warp clock, so that starting the
/// engine runs it to completion and the monitor's log is the whole run.
///
/// A snapshot is a JSON object carrying the current simulated time, the log entries recorded
/// since the previous snapshot, the currently pending decisions, whether the system is still
/// alive, and whether execution is done.
class Engine {
public:
  Engine();
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Wiring. The caller owns the monitor and controller and keeps them alive for the
  // lifetime of this engine. Attach before starting.
  void attachMonitor(Monitor* monitor);
  void attachController(Controller* controller);

  // Inputs. Each returns {"ok": true} on success or {"error": message} on failure.
  json loadModel(const std::string& bpmnXml);
  json loadLookupTable(const std::string& name, const std::string& csv);
  json loadInstances(const std::string& csv);
  json configure(const json& config); // {"provider": "static|expected|dynamic|stochastic", "seed": n}

  // Driving. Each returns a snapshot or {"error": message}. A timeout is a simulated time;
  // when omitted the engine runs until it can no longer proceed on its own.
  json start(std::optional<double> timeout = std::nullopt);
  json resume(std::optional<double> timeout = std::nullopt);

  // Observation. Returns the current snapshot without advancing the engine.
  json snapshot();

private:
  void build();
  json buildSnapshot(json extra);

  std::string workDir;
  std::string modelPath;
  bool haveModel = false;
  std::string instanceData;
  std::vector<std::string> folders;
  std::string providerName = "static";
  unsigned int seed = 0;
  bool built = false;

  std::unique_ptr<Model::DataProvider> dataProvider;
  std::unique_ptr<Model::Scenario> scenario;
  std::unique_ptr<Execution::Engine> engine;
  // Autonomous run wiring, used when no caller controller is attached: mirrors the engine's greedy
  // application with the guided evaluator, the greedy controller, the time-warp clock, and an
  // outcome sentinel. The evaluator is declared before the controller so it outlives it.
  std::unique_ptr<Execution::Evaluator> evaluator;
  std::unique_ptr<Execution::GreedyController> greedyController;
  std::unique_ptr<Execution::TimeWarp> timeWarp;
  std::unique_ptr<Execution::OutcomeSentinel> outcomeSentinel;
  Monitor* monitor = nullptr;         ///< not owned
  Controller* controller = nullptr;   ///< not owned
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_ENGINE_H
