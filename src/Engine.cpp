#include "Engine.h"

#include <stdexcept>
#include <utility>

#include "Controller.h"
#include "Monitor.h"

namespace BPMNOS::WASM {

Engine::Engine(std::unique_ptr<Model::DataProvider> dataProvider,
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
  scenario.reset();

  // Draw the named scenario; with a stochastic provider a different scenario id is a different sample.
  scenario = dataProvider->createScenario(scenarioId);
  engine = std::make_unique<Execution::Engine>();
  if (monitor) {
    monitor->subscribe(engine.get());
  }
  // The controller arrives complete, its dispatchers already connected to it, so this registers it with the
  // engine and nothing more. What a run settles by itself, and what it waits for, is the composition the
  // controller was built from.
  controller->connect(engine.get());
  // A run beginning before its first instance only ticks through empty instants, so it begins where the
  // scenario's data begins. The opening clock tick is the stream's first record and states that instant.
  engine->initialize(scenario.get(), scenario->getEarliestInstantiationTime());
}

void Engine::run(unsigned int scenarioId) {
  initialize(scenarioId);
  engine->resume();
}

void Engine::resume() {
  if (!engine) {
    throw std::runtime_error("engine has not been run");
  }
  engine->resume();
}

bool Engine::advance() {
  if (!engine) {
    throw std::runtime_error("engine has not been run");
  }
  return engine->advance();
}

bool Engine::isAlive() const {
  if (!engine) {
    return false;
  }
  const auto* systemState = engine->getSystemState();
  return systemState && systemState->isAlive();
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
