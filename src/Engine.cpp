#include "Engine.h"

#include <stdexcept>
#include <utility>

#include "Controller.h"
#include "Monitor.h"

namespace BPMNOS::WASM {

Engine::Engine(Model::Input&& input, Config config, Monitor* monitor, Controller* controller)
  : config(std::move(config))
  , monitor(monitor)
  , controller(controller)
{
  // Build the data provider once from the complete input. The provider owns the model the input parsed,
  // so every run reuses that parse and only draws a fresh scenario. The stochastic provider fixes its
  // base seed here; the scenario index a run adds is what varies the sample.
  const std::string& provider = this->config.provider;
  if (provider == "static") {
    dataProvider = std::make_unique<Model::StaticDataProvider>(std::move(input));
  }
  else if (provider == "expected") {
    dataProvider = std::make_unique<Model::ExpectedValueDataProvider>(std::move(input));
  }
  else if (provider == "dynamic") {
    dataProvider = std::make_unique<Model::DynamicDataProvider>(std::move(input));
  }
  else if (provider == "stochastic") {
    dataProvider = std::make_unique<Model::StochasticDataProvider>(std::move(input), this->config.seed);
  }
  else {
    throw std::runtime_error("unknown provider: " + provider);
  }
}

Engine::~Engine() = default;

void Engine::run(unsigned int scenarioId) {
  // Tear down any previous run before building the next. No observer unsubscribes on destruction, so the
  // engine is replaced freely; the wiring is released before the engine only for tidiness.
  timeWarp.reset();
  greedyController.reset();
  evaluator.reset();
  engine.reset();
  scenario.reset();

  // Draw the named scenario; with a stochastic provider a different scenario id is a different sample.
  scenario = dataProvider->createScenario(scenarioId);
  engine = std::make_unique<Execution::Engine>();
  if (monitor) {
    monitor->subscribe(engine.get());
  }
  if (controller) {
    // A caller-supplied controller drives the contested decisions and the clock. It auto-resolves the
    // unambiguous decisions itself, and no time handler is attached, so time advances only by a clock
    // tick the caller supplies.
    controller->connect(engine.get());
  }
  else {
    // No controller: run autonomously, replicating the engine's greedy application.
    evaluator = std::make_unique<Execution::GuidedEvaluator>();
    greedyController = std::make_unique<Execution::GreedyController>(evaluator.get());
    greedyController->connect(engine.get());
    timeWarp = std::make_unique<Execution::TimeWarp>();
    timeWarp->connect(engine.get());
  }
  engine->run(scenario.get());
}

void Engine::resume() {
  if (!engine) {
    throw std::runtime_error("engine has not been run");
  }
  engine->resume();
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

double Engine::getWeightedObjective() const {
  if (!engine) {
    return 0.0;
  }
  const auto* systemState = engine->getSystemState();
  return systemState ? static_cast<double>(systemState->getWeightedObjective()) : 0.0;
}

} // namespace BPMNOS::WASM
