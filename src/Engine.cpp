#include "Engine.h"

#include <stdexcept>
#include <utility>

#include "Controller.h"
#include "Monitor.h"

namespace BPMNOS::WASM {

Engine::Engine(std::shared_ptr<Execution::DataProvider> dataProvider,
               std::shared_ptr<Controller> controller,
               std::shared_ptr<Monitor> monitor)
  : dataProvider(std::move(dataProvider))
  , controller(std::move(controller))
  , monitor(std::move(monitor))
{
  // A run is these three and nothing else, so a missing one is refused here rather than at the first run.
  if (!this->dataProvider) {
    throw std::runtime_error("engine requires a data provider");
  }
  if (!this->controller) {
    throw std::runtime_error("engine requires a controller");
  }
}

Engine::~Engine() = default;

void Engine::initialize(unsigned int scenarioId) {
  // Tear down any previous run before building the next. No observer unsubscribes on destruction, so the
  // engine is replaced freely.
  engine.reset();
  terminated = false;

  // Draw the named scenario; with a stochastic provider a different scenario id is a different sample. A run
  // beginning before its first instance only ticks through empty instants, so it begins where the scenario's
  // data begins, which the data provider states before the engine takes the scenario over.
  auto scenario = dataProvider->createScenario(scenarioId);
  auto startTime = dataProvider->getEarliestInstantiationTime(*scenario);
  engine = std::make_unique<Execution::Engine>(dataProvider->getModel());
  // While the engine waits the caller acts, and while the run is paused the engine waits again and again.
  // Unless the run is paused, the bridge advances time if the caller has let it, the next round processing
  // the clock tick it enqueues.
  engine->wait = [this, sleep = engine->wait]() {
    do {
      if (!paused && clockTickDuration
          && std::chrono::steady_clock::now() - previousClockTick >= *clockTickDuration) {
        previousClockTick = std::chrono::steady_clock::now();
        controller->enqueueClockTickEvent();
        return;
      }
      (wait ? wait : sleep)();
    } while (paused);
  };
  if (monitor) {
    monitor->subscribe(engine.get());
  }
  // The controller arrives complete, its dispatchers already connected to it, so this registers it with the
  // engine and nothing more. What a run settles by itself, and what it waits for, is the composition the
  // controller was built from.
  controller->connect(engine.get());
  // The engine takes ownership of the scenario. The opening clock tick is the stream's first record and states
  // the instant the run begins at.
  engine->initialize(std::move(scenario), startTime);
}

void Engine::setWait(std::function<void()> wait) {
  this->wait = std::move(wait);
}

void Engine::setPaused(bool paused) {
  this->paused = paused;
}

bool Engine::isPaused() const {
  return paused;
}

void Engine::advanceTime(std::chrono::milliseconds clockTickDuration) {
  this->clockTickDuration = clockTickDuration;
}

void Engine::holdTime() {
  clockTickDuration.reset();
}

bool Engine::isAdvancingTime() const {
  return clockTickDuration.has_value();
}

void Engine::run(unsigned int scenarioId) {
  initialize(scenarioId);
  engine->resume();
  terminated = true;
}

void Engine::resume() {
  if (!engine) {
    throw std::runtime_error("engine has not been run");
  }
  // a run ended by a termination the caller enqueued continues, being alive again until it is terminated
  terminated = false;
  engine->resume();
  terminated = true;
}

bool Engine::advance() {
  if (!engine) {
    throw std::runtime_error("engine has not been run");
  }
  bool continuing = engine->advance();
  if (!continuing) {
    terminated = true;
  }
  return continuing;
}

bool Engine::isAlive() const {
  return engine && !terminated;
}

double Engine::getCurrentTime() const {
  if (!engine) {
    return 0.0;
  }
  return static_cast<double>(engine->getCurrentTime());
}

double Engine::getObjective() const {
  if (!engine) {
    return 0.0;
  }
  const auto* systemState = engine->getSystemState();
  return systemState ? static_cast<double>(systemState->getObjective()) : 0.0;
}

} // namespace BPMNOS::WASM
