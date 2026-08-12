// Native test for driving the bridge one event at a time.
//
// A caller that draws what a run produces must not be left behind by it: the greedy composition decides
// everything itself and advances its own clock, so one call to run carries a model to its end before
// anything can be drawn. initialize prepares a run without advancing it, and advance carries it forward by
// a single fetch, so the caller is never more than one event ahead of the records it has received.
//
// What is checked is that this changes nothing but the pacing. The same model, driven event by event,
// produces the record stream, the liveness, the time and the objective that one call to run produces.
// The timer fixture is used because it cannot finish without a clock, so the comparison covers a run whose
// events include clock ticks rather than only token movement.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

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

  // The reference: one call, the whole run. A run is repeatable on the same engine and draws the same
  // scenario from the same provider, so the second run below is the same run driven differently.
  engine.run();
  json expected = log;
  bool expectedAlive = engine.isAlive();
  double expectedTime = engine.getCurrentTime();
  double expectedObjective = engine.getObjective();
  check(!expected.empty(), "the reference run produced records");

  log = json::array();
  engine.initialize();

  // initialize advances time to the run's first instant, and instantiation is what reaching an instant
  // causes, so it produces the opening tick and the instances due at it, and stops before the first fetch.
  check(!log.empty() && log[0].contains("event") && log[0]["event"].value("event", std::string()) == "clocktick",
        "the stream opens with the clock tick that begins the run");
  check(log.size() < expected.size(), "initialize does not carry the run to its end");
  bool prefix = true;
  for (std::size_t i = 0; prefix && i < log.size(); ++i) {
    prefix = (log[i] == expected[i]);
  }
  check(prefix, "what initialize produced is the beginning of the same stream");
  check(engine.isAlive(), "the run is alive before it has been advanced");

  unsigned int advances = 0;
  while (engine.advance()) {
    ++advances;
  }

  check(advances > 0, "the run was carried forward one event at a time");
  check(log == expected, "advancing event by event produces the record stream that run produces");
  check(engine.isAlive() == expectedAlive, "it ends in the same liveness");
  check(engine.getCurrentTime() == expectedTime, "it ends at the same time");
  check(engine.getObjective() == expectedObjective, "it ends with the same objective");

  // Neither entry can be used before there is an engine to drive.
  bool refusedAdvance = false;
  try {
    Engine unstarted(std::make_unique<Model::StochasticDataProvider>(Input(modelXml).release(), 0),
                     Test::greedyController(), nullptr);
    unstarted.advance();
  }
  catch (const std::exception&) {
    refusedAdvance = true;
  }
  check(refusedAdvance, "advancing an engine that has not been initialized is refused");

  std::cerr << "all checks passed\n";
  return 0;
}
