// Native test for silencing a dispatcher of a composition.
//
// One composition serves several ways of running a model: a run is driven by whichever of its dispatchers
// answer, and that is turned over between fetches rather than by rebuilding anything. The greedy
// composition settles a choice itself; with the dispatcher that settles it silenced, the same controller
// leaves the choice to the caller, and letting it speak again settles the next one. Only dispatching is
// withheld, so the silenced dispatcher goes on observing and is correct the moment it is activated.

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
  std::string modelXml = readFile(fixtureDir + "/DecisionTask_with_enumeration.bpmn");
  std::string instanceCsv =
    "INSTANCE_ID; NODE_ID; INITIALIZATION\n"
    "Instance_1; Process_1;\n"
    "Instance_1; Activity_1; x := -2\n";

  Input input(modelXml);
  input.setInstance(instanceCsv);
  auto monitor = std::make_shared<Monitor>();

  // The composition is built here rather than taken from composition.h, so that the positions this test
  // switches are the ones it states and no reordering elsewhere can move them. The entry and the exit are
  // settled by their dispatchers, so the run reaches the choice; the choice dispatcher is what is silenced;
  // the queue is last and is what the caller would answer through.
  auto evaluator = std::make_shared<Execution::GuidedEvaluator>();
  std::vector<std::unique_ptr<Execution::EventDispatcher>> dispatchers;
  dispatchers.push_back(std::make_unique<Execution::GreedyDispatcher<Execution::FirstFeasibleExit>>(evaluator));
  dispatchers.push_back(std::make_unique<Execution::GreedyDispatcher<Execution::FirstFeasibleEntry>>(evaluator));
  dispatchers.push_back(std::make_unique<Execution::GreedyDispatcher<Execution::FirstEnumeratedChoice>>(evaluator));
  dispatchers.push_back(std::make_unique<EnqueuedEvents>());
  constexpr std::size_t firstEnumeratedChoice = 2;
  constexpr std::size_t enqueuedEvents = 3;
  constexpr std::size_t beyondTheComposition = 4;
  auto controller = std::make_shared<Controller>(std::move(dispatchers));

  Engine engine(std::make_unique<Model::StochasticDataProvider>(input.release(), 0), controller, monitor);

  json log = json::array();
  monitor->addObserver([&](const json& entry) { log.push_back(entry); });

  auto pendingChoice = [&]() -> std::shared_ptr<const Execution::DecisionRequest> {
    for (const auto& weak : controller->getPendingRequests()) {
      auto request = weak.lock();
      if (request && request->type == Execution::Observable::Type::ChoiceRequest) {
        return request;
      }
    }
    return nullptr;
  };

  check(controller->isActive(firstEnumeratedChoice), "every dispatcher speaks in a composition as composed");

  // Silenced, the choice is left to the caller: the engine reaches the decision task and stands at it.
  controller->deactivate(firstEnumeratedChoice);
  check(!controller->isActive(firstEnumeratedChoice), "the dispatcher reports that it is silenced");

  engine.initialize();
  while (engine.advance()) {
    if (pendingChoice()) {
      break;
    }
  }
  check(pendingChoice() != nullptr, "with the choice dispatcher silenced the run waits at the choice");

  // Spoken again, the same dispatcher settles the choice from candidates it kept observing while silent,
  // with nothing rebuilt and no new composition.
  controller->activate(firstEnumeratedChoice);
  check(controller->isActive(firstEnumeratedChoice), "the dispatcher reports that it speaks again");

  unsigned int guard = 0;
  while (engine.advance() && guard++ < 10000) {
  }
  check(guard < 10000, "the run ended rather than standing still");
  check(pendingChoice() == nullptr, "the reactivated dispatcher settled the choice it had left");

  // This composition holds no clock, so a run that never ticks is never reported dead; what says it got
  // through is the token, not the liveness of a state whose scenario is not yet past its instantiation.
  bool completed = false;
  for (const auto& entry : log) {
    if (entry.contains("token") && entry["token"].value("nodeId", std::string()) == "EndEvent_1" &&
        entry["token"].value("state", std::string()) == "DONE") {
      completed = true;
    }
  }
  check(completed, "the token reached the end event");

  // The queue is what the caller says, so a run that never dispatched it would ignore every decision, clock
  // tick and termination it is given.
  bool refusedQueue = false;
  try {
    controller->deactivate(enqueuedEvents);
  }
  catch (const std::exception&) {
    refusedQueue = true;
  }
  check(refusedQueue, "the queue cannot be silenced");

  bool refusedRange = false;
  try {
    controller->deactivate(beyondTheComposition);
  }
  catch (const std::exception&) {
    refusedRange = true;
  }
  check(refusedRange, "a position the composition does not hold is refused");

  std::cerr << "all checks passed\n";
  return 0;
}
