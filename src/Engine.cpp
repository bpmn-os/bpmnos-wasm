#include "Engine.h"

#include <stdexcept>

#include "Controller.h"
#include "Convert.h"
#include "Monitor.h"

namespace BPMNOS::WASM {

namespace {

/**
 * @brief Parses BPMN XML content into a tree the engine's model layer consumes. The bridge holds the
 * model as text and parses it here, so nothing is written to a filesystem.
 *
 * @param bpmnXml The BPMN model XML.
 * @return The parsed model tree.
 */
std::unique_ptr<XML::XMLObject> parseModel(const std::string& bpmnXml) {
  auto* root = XML::XMLObject::createFromString(bpmnXml);
  if (!root) {
    throw std::runtime_error("failed to parse BPMN model");
  }
  return std::unique_ptr<XML::XMLObject>(root);
}

} // namespace

Engine::Engine() = default;
Engine::~Engine() = default;

void Engine::attachMonitor(Monitor* monitorToAttach) {
  monitor = monitorToAttach;
}

void Engine::attachController(Controller* controllerToAttach) {
  controller = controllerToAttach;
}

json Engine::loadModel(const std::string& bpmnXml) {
  return guarded([&] {
    // Parse once here to validate the XML and to discover which lookup tables the model references,
    // so the caller can be asked for exactly those. The text is retained and reparsed on build.
    auto root = parseModel(bpmnXml);
    lookupNames = Model::Model::getLookupTableNames(*root);
    modelXml = bpmnXml;
    haveModel = true;
    built = false;
    return json{ {"ok", true} };
  });
}

json Engine::requiredLookups() {
  return guarded([&] {
    if (!haveModel) {
      throw std::runtime_error("no model loaded");
    }
    return json(lookupNames);
  });
}

json Engine::loadLookupTable(const std::string& name, const std::string& csv) {
  return guarded([&] {
    lookupTables[name] = csv;
    built = false;
    return json{ {"ok", true} };
  });
}

json Engine::loadInstances(const std::string& csv) {
  return guarded([&] {
    instanceData = csv;
    built = false;
    return json{ {"ok", true} };
  });
}

json Engine::configure(const json& config) {
  return guarded([&] {
    if (config.contains("provider")) {
      providerName = config["provider"].get<std::string>();
    }
    if (config.contains("seed")) {
      seed = config["seed"].get<unsigned int>();
    }
    built = false;
    return json{ {"ok", true} };
  });
}

void Engine::build() {
  if (!haveModel) {
    throw std::runtime_error("no model loaded");
  }
  if (instanceData.empty()) {
    throw std::runtime_error("no instance data loaded");
  }
  // Hand the provider the inputs in memory: a freshly parsed model tree, the lookup tables keyed by
  // source name, and the instance CSV. The tree is parsed anew each build so a restart owns its own,
  // while the lookup and instance content is copied from the retained members.
  Model::Input input{ parseModel(modelXml), lookupTables, instanceData };
  if (providerName == "static") {
    dataProvider = std::make_unique<Model::StaticDataProvider>(std::move(input));
  }
  else if (providerName == "expected") {
    dataProvider = std::make_unique<Model::ExpectedValueDataProvider>(std::move(input));
  }
  else if (providerName == "dynamic") {
    dataProvider = std::make_unique<Model::DynamicDataProvider>(std::move(input));
  }
  else if (providerName == "stochastic") {
    dataProvider = std::make_unique<Model::StochasticDataProvider>(std::move(input), seed);
  }
  else {
    throw std::runtime_error("unknown provider: " + providerName);
  }
  scenario = dataProvider->createScenario();
  engine = std::make_unique<Execution::Engine>();
  if (monitor) {
    monitor->subscribe(engine.get());
  }
  // The outcome sentinel observes how execution ends on either path, so a snapshot can report it.
  outcomeSentinel = std::make_unique<Execution::OutcomeSentinel>();
  outcomeSentinel->subscribe(engine.get());
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
  built = true;
}

json Engine::buildSnapshot(json extra) {
  const auto* systemState = engine->getSystemState();
  extra["time"] = toDouble(engine->getCurrentTime());
  extra["log"] = monitor ? monitor->drainLog() : json::array();
  extra["pending"] = controller ? controller->pendingDecisions(systemState) : json::array();
  // The engine considers the system alive while the scenario may still instantiate processes
  // (its completion is time based) or while any instance remains. Formal completion therefore
  // requires simulated time to advance past the last instantiation, which is a clock concern;
  // a caller that only drives decisions observes quiescence as an empty pending set.
  bool alive = systemState && systemState->isAlive();
  extra["alive"] = alive;
  extra["done"] = !alive;
  // On an autonomous run the outcome sentinel reports how execution ended and the system state
  // carries the weighted objective, mirroring what the engine's greedy application prints.
  if (outcomeSentinel) {
    extra["outcome"] = Execution::outcome[(std::size_t)outcomeSentinel->getOutcome()];
    if (systemState) {
      extra["objective"] = toDouble(systemState->getWeightedObjective());
    }
  }
  return extra;
}

json Engine::start(std::optional<double> timeout) {
  return guarded([&] {
    build();
    if (timeout) {
      engine->run(scenario.get(), toNumber(*timeout));
    }
    else {
      engine->run(scenario.get());
    }
    return buildSnapshot(json::object());
  });
}

json Engine::resume(std::optional<double> timeout) {
  return guarded([&] {
    if (!built || !engine) {
      throw std::runtime_error("engine not started");
    }
    if (timeout) {
      engine->resume(toNumber(*timeout));
    }
    else {
      engine->resume();
    }
    return buildSnapshot(json::object());
  });
}

json Engine::snapshot() {
  return guarded([&] {
    if (!built || !engine) {
      return json::object();
    }
    return buildSnapshot(json::object());
  });
}

} // namespace BPMNOS::WASM
