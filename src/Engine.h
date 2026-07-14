#ifndef BPMNOS_WASM_ENGINE_H
#define BPMNOS_WASM_ENGINE_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// The heavy engine headers are kept out of this interface; only the source file includes
// them. Forward declarations suffice because the owned engine objects are held through
// unique pointers whose destructor is emitted in the source file.
namespace BPMNOS {
namespace Model { class DataProvider; class Scenario; }
namespace Execution { class Engine; }
} // namespace BPMNOS

namespace BPMNOS::WASM {

class Monitor;
class Controller;

using json = nlohmann::ordered_json;

/// Owner of one execution engine and the driver of its lifecycle.
///
/// This class holds the engine's execution engine together with the data provider and
/// scenario built from the loaded model, lookup tables, and instance data. It connects a
/// monitor, and, when the caller intends to supply decisions, a controller, both of which
/// the caller constructs and owns. It exposes the loading calls and the advancing calls; a
/// caller drives execution by starting the engine, then reading the snapshot, submitting a
/// decision to the controller, and resuming, until execution is done. Advancing simulated
/// time by a clock tick is deliberately not offered yet; where that operation belongs is an
/// open question and the current controlled tests do not require it.
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
  Monitor* monitor = nullptr;         ///< not owned
  Controller* controller = nullptr;   ///< not owned
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_ENGINE_H
