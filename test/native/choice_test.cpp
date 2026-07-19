// Native choice test for the interactive bridge.
//
// The decision task fixture enters an activity that offers a choice from an enumeration and exits it.
// With an interactive controller the entry and the exit are resolved automatically, so the only decision
// left for the caller is the choice. This checks that the engine stops exactly at the choice, that the
// choice is offered with its enumeration keyed by the token's instance and node, and that an enqueued
// value is applied and appears on the completed activity. The instance sets x to minus two, so the
// enumeration offered is the set from the model with x substituted.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Controller.h"
#include "Engine.h"
#include "Input.h"
#include "Monitor.h"

using namespace BPMNOS::WASM;
using namespace BPMNOS;
using json = nlohmann::ordered_json;

static std::string readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "cannot open " << path << "\n";
    std::exit(2);
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

static void check(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
  std::cerr << "ok: " << message << "\n";
}

int main(int argc, char** argv) {
  std::string fixtureDir = (argc > 1) ? argv[1] : "test/fixtures";
  std::string modelXml = readFile(fixtureDir + "/DecisionTask_with_enumeration.bpmn");
  std::string instanceCsv =
    "INSTANCE_ID; NODE_ID; INITIALIZATION\n"
    "Instance_1; Process_1;\n"
    "Instance_1; Activity_1; x := -2\n";

  Input input(modelXml);
  input.setInstance(instanceCsv);
  Monitor monitor;
  Controller controller;
  Engine engine(input.release(), Engine::Config{}, &monitor, &controller);

  json log = json::array();
  monitor.addObserver([&](const json& entry) { log.push_back(entry); });

  engine.run();

  // Entry and exit are resolved automatically, so the only pending decision is the choice.
  auto findChoiceRequest = [&]() -> std::shared_ptr<const Execution::DecisionRequest> {
    for (const auto& weak : controller.getPendingRequests()) {
      auto request = weak.lock();
      if (request && request->type == Execution::Observable::Type::ChoiceRequest) {
        return request;
      }
    }
    return nullptr;
  };

  auto request = findChoiceRequest();
  check(request != nullptr, "the engine stopped at the choice");

  double enqueuedChoice = 0;
  int choiceCount = 0;
  int guard = 0;
  while (request && guard++ < 50) {
    ++choiceCount;
    std::vector<BPMNOS::number> choices;
    for (const auto& [attribute, values] : controller.getChoiceCandidates(request.get())) {
      check(std::holds_alternative<EnumeratedChoice>(values), "the choice offers an enumeration");
      const auto& enumeration = std::get<EnumeratedChoice>(values);
      check(!enumeration.empty(), "the enumeration offers allowed values");
      choices.push_back(enumeration.front());
      enqueuedChoice = static_cast<double>(enumeration.front());
    }
    check(controller.enqueueChoiceDecision(request, choices).has_value(), "enqueueChoiceDecision accepted");
    engine.resume();
    request = findChoiceRequest();
  }
  check(request == nullptr, "no decision is pending after the choice");
  check(choiceCount == 1, "exactly one choice was made");

  bool applied = false;
  for (const auto& entry : log) {
    if (entry.contains("token")) {
      const auto& token = entry["token"];
      if (token.value("nodeId", std::string()) == "Activity_1"
          && token.value("state", std::string()) == "COMPLETED"
          && token.contains("status") && token["status"].contains("choice")
          && token["status"]["choice"].get<double>() == enqueuedChoice) {
        applied = true;
      }
    }
  }
  check(applied, "the enqueued choice was applied on Activity_1 at COMPLETED");

  std::cerr << "ALL PASSED (choice)\n";
  return 0;
}
