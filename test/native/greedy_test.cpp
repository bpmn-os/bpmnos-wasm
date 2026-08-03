// Native greedy test for the bridge.
//
// The greedy composition decides everything itself and advances its own clock, so one call to run carries
// the model to its end without anyone driving it. This is what the bridge lost when the engine stopped
// building a greedy controller of its own for a run without one: the same run is now a composition like
// any other. The timer fixture is used because it cannot finish without a clock, so a run that terminates
// proves the clock is part of the composition rather than of the engine.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Controller.h"
#include "composition.h"
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
  std::string modelXml = readFile(fixtureDir + "/Timer.bpmn");
  std::string instanceCsv =
    "INSTANCE_ID; NODE_ID; INITIALIZATION\n"
    "Instance_1; Process_1; trigger := 3\n";

  Input input(modelXml);
  input.setInstance(instanceCsv);
  auto monitor = std::make_shared<Monitor>();
  auto controller = Test::greedyController();
  Engine engine(std::make_unique<Model::StochasticDataProvider>(input.release(), 0), controller, monitor);

  json log = json::array();
  monitor->addObserver([&](const json& entry) { log.push_back(entry); });

  engine.run();

  check(!engine.isAlive(), "the run ends without the caller enqueuing anything");
  check(engine.getCurrentTime() >= 3.0, "its own clock carried simulated time past the timer's trigger");

  bool completed = false;
  for (const auto& entry : log) {
    if (entry.contains("token") && entry["token"].value("nodeId", std::string()) == "EndEvent_1" &&
        entry["token"].value("state", std::string()) == "DONE") {
      completed = true;
    }
  }
  check(completed, "the token reached the end event");

  // A composition without a queue cannot be driven at all, and one with two would leave the precedence of
  // what the caller decides to whichever was found first, so the controller refuses both.
  bool refusedWithout = false;
  try {
    std::vector<std::unique_ptr<Execution::EventDispatcher>> none;
    none.push_back(std::make_unique<Execution::TimeWarp>());
    Controller composed(std::move(none));
  }
  catch (const std::exception&) {
    refusedWithout = true;
  }
  check(refusedWithout, "a composition without EnqueuedEvents is refused");

  bool refusedTwo = false;
  try {
    std::vector<std::unique_ptr<Execution::EventDispatcher>> two;
    two.push_back(std::make_unique<EnqueuedEvents>());
    two.push_back(std::make_unique<EnqueuedEvents>());
    Controller composed(std::move(two));
  }
  catch (const std::exception&) {
    refusedTwo = true;
  }
  check(refusedTwo, "a composition with two EnqueuedEvents is refused");

  std::cerr << "all checks passed\n";
  return 0;
}
