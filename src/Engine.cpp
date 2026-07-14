#include "Engine.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "Controller.h"
#include "Convert.h"
#include "Monitor.h"

#include <bpmn++.h>
#include <bpmnos-model.h>
#include <bpmnos-execution.h>

namespace BPMNOS::WASM {

Engine::Engine() {
  static std::atomic<std::uint64_t> counter{ 0 };
  auto dir = std::filesystem::temp_directory_path()
             / ("bpmnos-wasm-" + std::to_string(counter.fetch_add(1)));
  std::filesystem::create_directories(dir);
  workDir = dir.string();
}

Engine::~Engine() {
  std::error_code ignored;
  std::filesystem::remove_all(workDir, ignored);
}

void Engine::attachMonitor(Monitor* monitorToAttach) {
  monitor = monitorToAttach;
}

void Engine::attachController(Controller* controllerToAttach) {
  controller = controllerToAttach;
}

json Engine::loadModel(const std::string& bpmnXml) {
  return guarded([&] {
    modelPath = (std::filesystem::path(workDir) / "model.bpmn").string();
    std::ofstream file(modelPath, std::ios::binary);
    file << bpmnXml;
    file.close();
    if (!file) {
      throw std::runtime_error("failed to write model file");
    }
    haveModel = true;
    built = false;
    return json{ {"ok", true} };
  });
}

json Engine::loadLookupTable(const std::string& name, const std::string& csv) {
  return guarded([&] {
    auto lookupDir = std::filesystem::path(workDir) / "lookup";
    std::filesystem::create_directories(lookupDir);
    std::ofstream file((lookupDir / name), std::ios::binary);
    file << csv;
    file.close();
    if (!file) {
      throw std::runtime_error("failed to write lookup table");
    }
    auto lookup = lookupDir.string();
    if (std::find(folders.begin(), folders.end(), lookup) == folders.end()) {
      folders.push_back(lookup);
    }
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
  if (providerName == "static") {
    dataProvider = std::make_unique<Model::StaticDataProvider>(modelPath, folders, instanceData);
  }
  else if (providerName == "expected") {
    dataProvider = std::make_unique<Model::ExpectedValueDataProvider>(modelPath, folders, instanceData);
  }
  else if (providerName == "dynamic") {
    if (!folders.empty()) {
      throw std::runtime_error("dynamic provider with lookup folders not yet supported");
    }
    dataProvider = std::make_unique<Model::DynamicDataProvider>(modelPath, instanceData);
  }
  else if (providerName == "stochastic") {
    dataProvider = std::make_unique<Model::StochasticDataProvider>(modelPath, folders, instanceData, seed);
  }
  else {
    throw std::runtime_error("unknown provider: " + providerName);
  }
  scenario = dataProvider->createScenario();
  engine = std::make_unique<Execution::Engine>();
  if (monitor) {
    monitor->subscribe(engine.get());
  }
  if (controller) {
    controller->connect(engine.get());
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
