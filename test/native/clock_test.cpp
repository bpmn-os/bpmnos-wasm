// Native test of the clock and the pausing of a running run.
//
// The timer fixture triggers at the value of the trigger attribute, and the data provider holds time, so a
// run with the interactive composition waits at the timer until time advances. While the engine waits, the
// caller may let the engine advance time, pause the run, or end it, each at any moment of the run.

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
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

  // Time is let advance once the run waits at the timer, and the run then ends without the caller.
  {
    Input input(modelXml);
    input.setInstance(instanceCsv);
    auto controller = Test::interactiveController();
    Engine engine(Test::dataProvider(input), controller, nullptr);
    check(!engine.isAdvancingTime(), "a new engine holds time");

    unsigned int waits = 0;
    double timeWaited = -1;
    engine.setWait([&]() {
      waits++;
      timeWaited = engine.getCurrentTime();
      engine.advanceTime(std::chrono::milliseconds::zero());
    });
    engine.run();

    check(waits == 1, "the run waited once, at the timer, before time was let advance");
    check(timeWaited == 0.0, "time stood still while the engine held it");
    check(engine.getCurrentTime() == 3.0, "the engine advanced time to the trigger once it was let");
    check(!engine.isAlive(), "the run ended without the caller ticking the clock");
  }

  // The run is paused while it waits, and while paused it waits again and again without time advancing;
  // once the caller lets it continue and lets time advance, it ends.
  {
    Input input(modelXml);
    input.setInstance(instanceCsv);
    auto controller = Test::interactiveController();
    Engine engine(Test::dataProvider(input), controller, nullptr);

    unsigned int pausedWaits = 0;
    bool timeStood = true;
    engine.setWait([&]() {
      if (!engine.isPaused()) {
        // the first wait pauses the run
        engine.setPaused(true);
        return;
      }
      pausedWaits++;
      timeStood = timeStood && engine.getCurrentTime() == 0.0;
      if (pausedWaits == 5) {
        engine.setPaused(false);
        engine.advanceTime(std::chrono::milliseconds::zero());
      }
    });
    engine.run();
    check(pausedWaits == 5, "a paused run waited again and again until the caller let it continue");
    check(timeStood, "time stood still while the run was paused");
    check(engine.getCurrentTime() == 3.0, "the run continued once the caller let it, time advancing");
    check(!engine.isAlive(), "the continued run ended once nothing was left");
  }

  // A termination the caller enqueues ends a running run, which resume continues.
  {
    Input input(modelXml);
    input.setInstance(instanceCsv);
    auto controller = Test::interactiveController();
    Engine engine(Test::dataProvider(input), controller, nullptr);

    bool ended = false;
    engine.setWait([&]() {
      if (!ended) {
        ended = true;
        controller->enqueueTerminationEvent();
        return;
      }
      controller->enqueueClockTickEvent();
    });
    engine.run();
    check(!engine.isAlive(), "the termination the caller enqueued ended the run");
    check(engine.getCurrentTime() == 0.0, "the run ended at the timer");

    engine.resume();
    check(engine.getCurrentTime() == 3.0, "resume continued the run, the caller ticking the clock");
    check(!engine.isAlive(), "the continued run ended once nothing was left");
  }

  std::cerr << "all checks passed\n";
  return 0;
}
