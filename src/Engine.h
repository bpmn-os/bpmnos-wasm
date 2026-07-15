#ifndef BPMNOS_WASM_ENGINE_H
#define BPMNOS_WASM_ENGINE_H

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <bpmn++.h>
#include <bpmnos-model.h>
#include <bpmnos-execution.h>

namespace BPMNOS::WASM {

class Monitor;
class Controller;

using json = nlohmann::ordered_json;

/**
 * @brief Owner of one execution engine and the driver of its lifecycle.
 *
 * This class holds the engine's execution engine together with the data provider and scenario built
 * from the loaded model, lookup tables, and instance data. It always connects a monitor. A controller
 * is optional: when the caller attaches one, that caller supplies the decisions and drives execution by
 * starting the engine, reading the snapshot, submitting a decision, and resuming until execution is done.
 * When no controller is attached, the engine instead runs autonomously, mirroring the engine's own greedy
 * application by connecting a greedy controller with the guided evaluator and the time-warp clock, so that
 * starting the engine runs it to completion and the monitor's log is the whole run.
 *
 * A snapshot is a JSON object carrying the current simulated time, the log entries recorded since the
 * previous snapshot, the currently pending decisions, whether the system is still alive, and whether
 * execution is done.
 */
class Engine {
public:
  Engine();
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  /**
   * @brief Attaches a monitor to observe the run. The caller owns it and keeps it alive for the lifetime
   * of this engine, and attaches it before starting.
   *
   * @param monitor The monitor to attach.
   */
  void attachMonitor(Monitor* monitor);

  /**
   * @brief Attaches a controller to supply the decisions. The caller owns it and keeps it alive for the
   * lifetime of this engine, and attaches it before starting. Omitting it runs the engine autonomously.
   *
   * @param controller The controller to attach.
   */
  void attachController(Controller* controller);

  /**
   * @brief Loads the BPMN model, parsing it and recording the lookup tables it references.
   *
   * @param bpmnXml The BPMN model XML.
   * @return {"ok": true} on success, or {"error": message}.
   */
  json loadModel(const std::string& bpmnXml);

  /**
   * @brief After loadModel, reports the lookup table source names the model references, so the caller can
   * supply each with loadLookupTable.
   *
   * @return A JSON array of the lookup table source names, or {"error": message} before a model is loaded.
   */
  json requiredLookups();

  /**
   * @brief Loads one lookup table's content, keyed by its source name.
   *
   * @param name The lookup table source name.
   * @param csv The lookup table CSV content.
   * @return {"ok": true} on success, or {"error": message}.
   */
  json loadLookupTable(const std::string& name, const std::string& csv);

  /**
   * @brief Loads the instance data.
   *
   * @param csv The instance CSV content.
   * @return {"ok": true} on success, or {"error": message}.
   */
  json loadInstances(const std::string& csv);

  /**
   * @brief Configures the run.
   *
   * @param config {"provider": "static|expected|dynamic|stochastic", "seed": n}, each field optional.
   * @return {"ok": true} on success, or {"error": message}.
   */
  json configure(const json& config);

  /**
   * @brief Builds the scenario and runs the engine from the start.
   *
   * @param timeout A simulated time to stop at; when omitted the engine runs until it can no longer
   * proceed on its own.
   * @return A snapshot, or {"error": message}.
   */
  json start(std::optional<double> timeout = std::nullopt);

  /**
   * @brief Continues a started run.
   *
   * @param timeout A simulated time to stop at; when omitted the engine runs until it can no longer
   * proceed on its own.
   * @return A snapshot, or {"error": message}.
   */
  json resume(std::optional<double> timeout = std::nullopt);

  /**
   * @brief Returns the current snapshot without advancing the engine.
   *
   * @return The current snapshot.
   */
  json snapshot();

private:
  /**
   * @brief Builds the data provider, scenario, and engine from the loaded inputs and wires the observers
   * and dispatchers, autonomously or through the attached controller.
   */
  void build();

  /**
   * @brief Assembles a snapshot from the current engine state.
   *
   * @param extra A JSON object to extend with the snapshot fields.
   * @return The completed snapshot.
   */
  json buildSnapshot(json extra);

  std::string modelXml;                                       ///< retained BPMN XML, reparsed on build
  std::unordered_map<std::string, std::string> lookupTables;  ///< lookup CSV content keyed by source name
  std::vector<std::string> lookupNames;                       ///< lookup sources the model references
  bool haveModel = false;
  std::string instanceData;
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
