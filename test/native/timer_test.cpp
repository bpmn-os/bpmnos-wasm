// Native clock tick test for the interactive bridge.
//
// The timer fixture is a start event, an intermediate catch timer that triggers at the value of the
// trigger attribute, and an end event. With an interactive controller and no time handler the engine
// runs to the timer and stops, because it can fetch no event: no decision is pending and time does not
// advance on its own. The caller then advances the clock one tick at a time and resumes, and the process
// terminates once simulated time reaches the trigger. This checks that the engine stops waiting for the
// clock, that each clock tick advances time by one, and that the timer fires and the process completes.

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
  std::string modelXml = readFile(fixtureDir + "/Timer.bpmn");
  std::string instanceCsv =
    "INSTANCE_ID; NODE_ID; INITIALIZATION\n"
    "Instance_1; Process_1; trigger := 3\n";

  Input input(modelXml);
  input.setInstance(instanceCsv);
  Monitor monitor;
  Controller controller;
  Engine engine(input.release(), Engine::Config{}, &monitor, &controller);

  engine.run();
  check(controller.pendingDecisions().empty(), "no decision is pending; the timer waits for the clock");
  check(engine.isAlive(), "the system is alive, waiting for the timer");

  double startTime = engine.getCurrentTime();
  int ticks = 0;
  int guard = 0;
  while (engine.isAlive() && guard++ < 20) {
    controller.submitClockTick();
    double previousTime = engine.getCurrentTime();
    engine.resume();
    check(engine.getCurrentTime() == previousTime + 1, "a clock tick advances time by one");
    ++ticks;
  }

  check(!engine.isAlive(), "the process terminated after the timer fired");
  check(engine.getCurrentTime() - startTime >= 3, "the clock reached the trigger time");

  const json& fullLog = monitor.fullLog();
  bool reachedEnd = false;
  bool sawClockTick = false;
  for (const auto& entry : fullLog) {
    if (entry.contains("event") && entry["event"].value("event", std::string()) == "clocktick") {
      sawClockTick = true;
    }
    if (entry.contains("token") && entry["token"].value("nodeId", std::string()) == "EndEvent_1") {
      reachedEnd = true;
    }
  }
  check(sawClockTick, "the clock ticks appear in the log");
  check(reachedEnd, "the token reached the end event");

  std::cerr << "terminated after " << ticks << " clock ticks, final time "
            << engine.getCurrentTime() << "\n";
  std::cerr << "ALL PASSED\n";
  return 0;
}
