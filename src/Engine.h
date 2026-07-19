#ifndef BPMNOS_WASM_ENGINE_H
#define BPMNOS_WASM_ENGINE_H

#include <memory>
#include <string>

#include <bpmn++.h>
#include <bpmnos-model.h>
#include <bpmnos-execution.h>

namespace BPMNOS::WASM {

class Monitor;
class Controller;

/**
 * @brief Owner of the data provider and the driver of an execution engine's lifecycle.
 *
 * Construction builds the data provider from the complete input once, so an Engine is valid the moment
 * it is constructed. It always observes through a monitor. A controller is optional: when the caller
 * attaches one, that caller supplies the decisions and drives execution by running the engine, reading
 * the pending decisions from the controller, enqueuing a decision, and resuming until the system is no
 * longer alive. When no controller is attached, the engine instead runs autonomously, mirroring the
 * engine's own greedy application by connecting a greedy controller with the guided evaluator and the
 * time-warp clock, so that a run proceeds to completion.
 *
 * The interface mirrors BPMNOS::Execution::Engine: run starts a named scenario from the beginning,
 * resume continues it, and isAlive reports the liveness of the system state. Running is repeatable: each
 * run draws its scenario from the durable data provider without reparsing the model, so running the same
 * model with a different scenario id is a different stochastic sample. A controller holds decision state
 * across a run, so a controller-driven engine is run once and then advanced by resume; a fresh base seed
 * is a fresh Engine.
 */
class Engine {
public:
  /**
   * @brief The choice of data provider and the seed a run is built with.
   */
  struct Config {
    std::string provider = "stochastic";  ///< Data provider kind: "static", "expected", "dynamic", or "stochastic".
    unsigned int seed = 0;            ///< Base seed for the stochastic provider; each run adds its scenario index.
  };

  /**
   * @brief Builds the data provider from the complete input, ready to run.
   *
   * @param input The assembled model, lookup tables, and instance, moved in.
   * @param config The data provider kind and seed.
   * @param monitor The monitor observing every run. The caller owns it and keeps it alive for the
   * lifetime of this engine.
   * @param controller The controller supplying decisions, or a null pointer to run autonomously. The
   * caller owns it and keeps it alive for the lifetime of this engine.
   */
  Engine(Model::Input&& input, Config config, Monitor* monitor, Controller* controller = nullptr);
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  /**
   * @brief Draws the named scenario and runs a new engine from the beginning, mirroring the execution
   * engine's own run.
   *
   * @param scenarioId The scenario to draw from the data provider. A stochastic provider samples the
   * base seed plus this index, so a different scenario id is a different sample of the same model.
   */
  void run(unsigned int scenarioId = 0);

  /**
   * @brief Continues a run, mirroring the execution engine's own resume.
   */
  void resume();

  /**
   * @brief Reports whether the system state is still alive, mirroring SystemState::isAlive. A run is done
   * once this is false.
   *
   * @return True while the system may still proceed, false once it cannot and before the first run.
   */
  bool isAlive() const;

  /**
   * @brief Reports the current simulated time, mirroring the execution engine's getCurrentTime, as a
   * double for the JavaScript boundary.
   *
   * @return The current simulated time, or zero before the first run.
   */
  double getCurrentTime() const;

private:
  std::unique_ptr<Model::DataProvider> dataProvider;  ///< Built once from the input; reused across runs.
  Config config;                                      ///< The provider kind and base seed.
  Monitor* monitor;                                   ///< Not owned; wired to observe every run.
  Controller* controller;                             ///< Not owned; wired to supply decisions, or null.

  // Per-run state, rebuilt on each run.
  std::unique_ptr<Model::Scenario> scenario;
  std::unique_ptr<Execution::Engine> engine;
  // Autonomous run wiring, used when no caller controller is attached: mirrors the engine's greedy
  // application with the guided evaluator, the greedy controller, and the time-warp clock. The evaluator
  // is declared before the controller so it outlives it.
  std::unique_ptr<Execution::Evaluator> evaluator;
  std::unique_ptr<Execution::GreedyController> greedyController;
  std::unique_ptr<Execution::TimeWarp> timeWarp;
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_ENGINE_H
