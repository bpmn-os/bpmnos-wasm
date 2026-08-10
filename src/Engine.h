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
 * @brief The driver of an execution engine's lifecycle, run by a controller and watched by a monitor.
 *
 * It is given the parts a run is made of and assembles nothing itself. The data provider arrives built, so
 * the model is parsed once and every run only draws a scenario from it. The controller supplies every event
 * that does not come from the engine, whether a user decides them or a composition of dispatchers settles
 * them without anyone, so there is one path through a run rather than one per mode. A monitor, if there is
 * one, observes it.
 *
 * The interface mirrors BPMNOS::Execution::Engine: run starts a named scenario from the beginning,
 * resume continues it, and isAlive reports the liveness of the system state. Running is repeatable: each
 * run draws its scenario from the durable data provider without reparsing the model, so running the same
 * model with a different scenario id is a different stochastic sample. A controller holds decision state
 * across a run, so an engine is run once and then advanced by resume.
 */
class Engine {
public:
  /**
   * @brief Takes the parts of a run: the built data provider, the controller driving it, and the monitor
   * watching it.
   *
   * The provider is this engine's alone and is owned outright. The controller and the monitor are used by
   * the caller for as long as a run lasts, the one to enqueue what it decides and the other to observe, so
   * both are shared: the engine holds a share for the run and the caller holds its own, and the order in
   * which the caller releases them does not matter.
   *
   * A run without a controller would fetch no event, so one is required. A monitor is not: an unobserved
   * run still reports through isAlive, getCurrentTime, and getWeightedObjective, and a caller that wants no
   * stream should not pay for one, the monitor serialising each notification before it looks for observers.
   *
   * @param dataProvider The data provider a scenario is drawn from, moved in.
   * @param controller The controller supplying the events a run is driven by.
   * @param monitor The monitor observing every run, or none.
   * @throws std::runtime_error if the provider or the controller is missing.
   */
  Engine(std::unique_ptr<Model::DataProvider> dataProvider,
         std::shared_ptr<Controller> controller,
         std::shared_ptr<Monitor> monitor);
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
   * @brief Draws the named scenario and prepares a new engine without advancing it, mirroring the
   * execution engine's own initialize.
   *
   * The run begins at the scenario's earliest instantiation time, and the clock tick that opens it is the
   * first record of the stream. Nothing further is processed, so a caller that drives the engine itself
   * calls this once and then advance repeatedly; run is this followed by resume.
   *
   * @param scenarioId The scenario to draw from the data provider.
   */
  void initialize(unsigned int scenarioId = 0);

  /**
   * @brief Continues a run, mirroring the execution engine's own resume.
   */
  void resume();

  /**
   * @brief Advances the run until the next event has to be fetched, mirroring the execution engine's own
   * advance.
   *
   * One call fetches a single event and advances the system state as far as it can without fetching the
   * next, so a caller is never more than one event ahead of what it does with the records produced.
   *
   * @return True if an event was processed and the run may continue, false once it cannot.
   */
  bool advance();

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

  /**
   * @brief Reports the total weighted objective value accumulated so far, mirroring the system state's
   * getWeightedObjective, as a double for the JavaScript boundary. It is a live running value, valid at
   * any pause, not only at termination.
   *
   * @return The current objective value, or zero before the first run.
   */
  double getWeightedObjective() const;

private:
  std::unique_ptr<Model::DataProvider> dataProvider;  ///< Owned outright; every run draws from it.
  std::shared_ptr<Controller> controller;             ///< Shared with the caller, which enqueues into it.
  std::shared_ptr<Monitor> monitor;                   ///< Shared with the caller, which observes through it.

  // Per-run state, rebuilt on each run.
  std::unique_ptr<Model::Scenario> scenario;
  std::unique_ptr<Execution::Engine> engine;
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_ENGINE_H
