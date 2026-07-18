// Native sequential entry test for the interactive bridge.
//
// The children of a sequential ad-hoc subprocess are entered one at a time, and their entry is the
// decision the caller makes; every other entry, including the entry of the subprocess itself, is
// resolved automatically. This drives the fixture whose subprocess AdHocSubProcess_1 holds two
// activities and checks that the engine stops offering each child's entry, that submitting it enters
// the child, and that once both children have been entered they and the subprocess complete.

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
  std::string modelXml = readFile(fixtureDir + "/AdHocSubProcess.bpmn");
  std::string instanceCsv =
    "INSTANCE_ID; NODE_ID; INITIALIZATION\n"
    "Instance_1; Process_1;\n";

  Input input(modelXml);
  input.setInstance(instanceCsv);
  Monitor monitor;
  Controller controller;
  Engine engine(input.release(), Engine::Config{}, &monitor, &controller);

  engine.run();
  json pending = controller.pendingDecisions();
  check(!pending.empty(), "the engine stopped at a sequential entry");

  int entered = 0;
  int guard = 0;
  while (!pending.empty() && guard++ < 50) {
    const auto& entryRequest = pending[0];
    check(entryRequest["type"] == "entry", "the pending decision is a sequential entry");
    json decision = {
      {"type", "entry"},
      {"instanceId", entryRequest["instanceId"]},
      {"nodeId", entryRequest["nodeId"]},
    };
    check(!controller.submitDecision(decision).contains("rejected"), "submitDecision accepted");
    ++entered;
    engine.resume();
    pending = controller.pendingDecisions();
  }
  check(pending.empty(), "no decision is pending after the sequential entries");
  check(entered == 2, "both ad-hoc children were entered");

  bool completedFirst = false;
  bool completedSecond = false;
  bool completedSubProcess = false;
  for (const auto& entry : monitor.fullLog()) {
    if (!entry.contains("token")) {
      continue;
    }
    const auto& token = entry["token"];
    if (token.value("state", std::string()) != "COMPLETED") {
      continue;
    }
    std::string nodeId = token.value("nodeId", std::string());
    if (nodeId == "Activity_1") completedFirst = true;
    else if (nodeId == "Activity_2") completedSecond = true;
    else if (nodeId == "AdHocSubProcess_1") completedSubProcess = true;
  }
  check(completedFirst && completedSecond, "both ad-hoc children completed");
  check(completedSubProcess, "the ad-hoc subprocess completed");

  std::cerr << "ALL PASSED (sequential entry)\n";
  return 0;
}
