// Native drive test for the interactive bridge.
//
// It drives the decision-task fixture through the run, stop, and resume model with no clock,
// supplying the entry, choice, and exit decisions itself, and checks three things: that the
// engine stops at each decision and reports it as pending, that a submitted choice value is
// applied and appears in the log, and that re-submitting a consumed request identifier is
// rejected rather than crashing. The instance sets x to minus two, so the enumeration offered
// for the choice is the set from the model with x substituted.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "Controller.h"
#include "Engine.h"
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

  Engine engine;
  Monitor monitor;
  Controller controller;
  engine.attachMonitor(&monitor);
  engine.attachController(&controller);

  check(!engine.loadModel(modelXml).contains("error"), "loadModel");
  check(!engine.loadInstances(instanceCsv).contains("error"), "loadInstances");
  engine.configure(json{ {"provider", "static"} });

  json state = engine.start();
  check(!state.contains("error"), "start");
  std::cerr << "start snapshot: " << state.dump() << "\n";

  json fullLog = json::array();
  auto absorb = [&](const json& snapshot) {
    if (snapshot.contains("log")) {
      for (const auto& entry : snapshot["log"]) {
        fullLog.push_back(entry);
      }
    }
  };
  absorb(state);

  // Drive the engine while it is waiting for a decision. This timer-less fixture quiesces
  // once entry, choice, and exit have been supplied; reaching a formally terminal state would
  // additionally require advancing time past the instantiation horizon, which is a clock
  // concern and is not exercised here.
  double submittedChoice = 0;
  int entryCount = 0;
  int choiceCount = 0;
  int exitCount = 0;
  int guard = 0;
  while (!state["pending"].empty() && guard++ < 50) {
    const auto& decisionRequest = state["pending"][0];
    std::string type = decisionRequest["type"];
    json decision = { {"requestId", decisionRequest["requestId"]}, {"type", type} };
    if (type == "entry") {
      ++entryCount;
    }
    else if (type == "exit") {
      ++exitCount;
    }
    else if (type == "choice") {
      ++choiceCount;
      json choices = json::array();
      for (const auto& choice : decisionRequest["choices"]) {
        check(choice.contains("enumeration") && !choice["enumeration"].empty(),
              "the choice offers an enumeration of allowed values");
        double value = choice["enumeration"][0].get<double>();
        choices.push_back(value);
        submittedChoice = value;
      }
      decision["choices"] = choices;
    }
    json accepted = controller.submitDecision(decision);
    check(!accepted.contains("rejected"), "submitDecision accepted (" + type + ")");
    state = engine.resume();
    check(!state.contains("error"), "resume");
    absorb(state);
  }
  check(state["pending"].empty(), "the engine quiesced with no pending decision");
  check(entryCount == 1 && choiceCount == 1 && exitCount == 1,
        "entry, choice, and exit were each driven exactly once");

  bool applied = false;
  for (const auto& entry : fullLog) {
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

  json stale = controller.submitDecision(json{ {"requestId", 1}, {"type", "entry"} });
  check(stale.contains("rejected"), "re-submitting a consumed request is rejected, not a crash");

  std::cerr << "ALL PASSED\n";
  return 0;
}
