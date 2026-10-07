#ifndef BPMNOS_WASM_ENGINE_H
#define BPMNOS_WASM_ENGINE_H

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
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
   * The provider is shared, since every scenario drawn from it holds a share of it, and it outlives the last
   * of them. The controller and the monitor are used by the caller for as long as a run lasts, the one to
   * enqueue what it decides and the other to observe, so both are shared: the engine holds a share for the
   * run and the caller holds its own, and the order in which the caller releases them does not matter.
   *
   * A run without a controller would fetch no event, so one is required. A monitor is not: an unobserved
   * run still reports through isAlive, getCurrentTime, and getObjective, and a caller that wants no
   * stream should not pay for one, the monitor serialising each notification before it looks for observers.
   *
   * @param dataProvider The data provider a scenario is drawn from.
   * @param controller The controller supplying the events a run is driven by.
   * @param monitor The monitor observing every run, or none.
   * @throws std::runtime_error if the provider or the controller is missing.
   */
  Engine(std::shared_ptr<Execution::DataProvider> dataProvider,
         std::shared_ptr<Controller> controller,
         std::shared_ptr<Monitor> monitor);
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  /**
   * @brief Sets what the engine of every run does while it waits, which is whenever a round yields no event.
   *
   * The function hands control to the caller, which may read the system state and enqueue events in it, the
   * next round processing them, but neither advances nor resumes the run. Without one, the engine sleeps for
   * BPMNOS::Execution::Engine::SLEEP. Set before a run.
   *
   * @param wait The function called while the engine waits.
   */
  void setWait(std::function<void()> wait);

  /**
   * @brief Pauses or continues a running run.
   *
   * While paused, the engine waits again and again whenever it waits, so that the run stands still without
   * being terminated; it continues once the caller lets it, from within the function waiting.
   *
   * @param paused Whether the run is paused.
   */
  void setPaused(bool paused);

  /**
   * @brief Reports whether the run is paused.
   *
   * @return True while the run is paused.
   */
  bool isPaused() const;

  /**
   * @brief Lets the bridge advance time while the engine waits, as a data provider holding time does not.
   *
   * Whenever the engine waits, a clock tick is enqueued at once if the duration is zero, and otherwise once
   * the given wall-clock time has passed since the previous one, so that time advances by itself, as fast
   * as the run allows or in step with real time. It may be switched at any moment of a run, like a
   * dispatcher of the controller, and no clock tick is enqueued while the run is paused.
   *
   * @param clockTickDuration The wall-clock time between two clock ticks, zero advancing time at once.
   */
  void advanceTime(std::chrono::milliseconds clockTickDuration);

  /**
   * @brief Stops the bridge from advancing time, so that time advances only by the data provider or by the
   * clock ticks the caller enqueues. This is the state of a new engine.
   */
  void holdTime();

  /**
   * @brief Reports whether the bridge advances time while the engine waits.
   *
   * @return True while it does.
   */
  bool isAdvancingTime() const;

  /**
   * @brief Draws the named scenario and runs a new engine from the beginning, mirroring the execution
   * engine's own run.
   *
   * The run continues until it is terminated, the caller acting while the engine waits.
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
   * @brief Continues a run, mirroring the execution engine's own resume, until it is terminated, the caller
   * acting while the engine waits.
   */
  void resume();

  /**
   * @brief Advances the run until the next event has to be fetched, mirroring the execution engine's own
   * advance.
   *
   * One call performs a single round: it fetches a single event and advances the system state as far as it
   * can without fetching the next, so a caller is never more than one event ahead of what it does with the
   * records produced. A round yielding no event waits.
   *
   * @return True while the run may continue, false once it is terminated.
   */
  bool advance();

  /**
   * @brief Reports whether the run is still alive, which it is from its beginning until a termination event
   * is processed. A run that waits is alive.
   *
   * @return True while the run may continue, false once it is terminated and before the first run.
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
   * @brief Reports the objective value maintained by the run, mirroring the system state's
   * getObjective, as a double for the JavaScript boundary. It is a live running value, valid at
   * any pause, not only at termination.
   *
   * @return The current objective value, or zero before the first run.
   */
  double getObjective() const;

private:
  std::shared_ptr<Execution::DataProvider> dataProvider;  ///< Every run draws its scenario from it.
  std::shared_ptr<Controller> controller;             ///< Shared with the caller, which enqueues into it.
  std::shared_ptr<Monitor> monitor;                   ///< Shared with the caller, which observes through it.
  std::function<void()> wait;                          ///< What the engine of a run does while it waits, its own sleeping if empty.
  bool paused = false;                                 ///< Whether the run is paused, the engine then waiting again and again.
  std::optional<std::chrono::milliseconds> clockTickDuration; ///< The wall-clock time between two clock ticks the bridge enqueues, none while it holds time.
  std::chrono::steady_clock::time_point previousClockTick; ///< The wall-clock time at which the bridge enqueued the previous clock tick.
  bool terminated = false;                             ///< Whether the run has been terminated.

  // Per-run state, rebuilt on each run.
  std::unique_ptr<Execution::Engine> engine;
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_ENGINE_H
