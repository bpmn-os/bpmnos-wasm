// Native choice test for the interactive bridge.
//
// The decision task fixture enters an activity that offers a choice from an enumeration and exits it.
// With an interactive controller the entry and the exit are resolved automatically, so the only decision
// left for the caller is the choice. This checks that the engine stops exactly at the choice, that the
// choice is offered with its enumeration keyed by the token's instance and node, and that a submitted
// value is applied and appears on the completed activity. The instance sets x to minus two, so the
// enumeration offered is the set from the model with x substituted.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "Controller.h"
#include "Engine.h"
#include "Input.h"
#include "Monitor.h"

using namespace BPMNOS::WASM;
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

  engine.run();
  json pending = controller.pendingDecisions();
  check(!pending.empty(), "the engine stopped at the choice");

  double submittedChoice = 0;
  int choiceCount = 0;
  int guard = 0;
  while (!pending.empty() && guard++ < 50) {
    // Entry and exit are automatic here, so every pending decision is a choice.
    for (const auto& decision : pending) {
      check(decision["type"] == "choice", "the only pending decision is a choice");
    }
    const auto& choiceRequest = pending[0];
    ++choiceCount;
    json choices = json::array();
    for (const auto& choice : choiceRequest["choices"]) {
      check(choice.contains("enumeration") && !choice["enumeration"].empty(),
            "the choice offers an enumeration of allowed values");
      double value = choice["enumeration"][0].get<double>();
      choices.push_back(value);
      submittedChoice = value;
    }
    json decision = {
      {"type", "choice"},
      {"instanceId", choiceRequest["instanceId"]},
      {"nodeId", choiceRequest["nodeId"]},
      {"choices", choices},
    };
    check(!controller.submitDecision(decision).contains("rejected"), "submitDecision accepted");
    engine.resume();
    pending = controller.pendingDecisions();
  }
  check(pending.empty(), "no decision is pending after the choice");
  check(choiceCount == 1, "exactly one choice was made");

  bool applied = false;
  for (const auto& entry : monitor.fullLog()) {
    if (entry.contains("token")) {
      const auto& token = entry["token"];
      if (token.value("nodeId", std::string()) == "Activity_1"
          && token.value("state", std::string()) == "COMPLETED"
          && token.contains("status") && token["status"].contains("choice")
          && token["status"]["choice"].get<double>() == submittedChoice) {
        applied = true;
      }
    }
  }
  check(applied, "the submitted choice was applied on Activity_1 at COMPLETED");

  std::cerr << "ALL PASSED (choice)\n";
  return 0;
}
