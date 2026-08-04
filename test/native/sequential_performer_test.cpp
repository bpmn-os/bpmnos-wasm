// Native sequential performer test for the interactive bridge.
//
// A sequential ad-hoc subprocess performs its children one at a time, and the token standing at the node
// that performs them holds what it conducts and what waits for it. This drives two fixtures and checks
// what the bridge reports of that bookkeeping: the plain subprocess, whose two activities queue and whose
// performer is asked afresh once one of them has been through, and the one whose first activity is
// multi-instance, whose instance tokens queue in place of the activity itself.
//
// A performer is occupied on the entry of a child and released on its exit, which are token transitions
// and have nothing to do with the clock. Catching one while it conducts a child therefore needs a child
// that cannot reach its exit within the same fetch: everything between entry and exit is automatic in this
// composition, the engine completing a plain task as soon as its completion status is available and
// `FirstFeasibleExit` taking the exit, so a task of no duration is entered and gone before the caller looks
// again. The second fixture gives its activities a duration, which is one way to block a token; a message
// awaited, a choice, or a composition leaving the exit to the caller would do as well.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
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

/** What a token is called here: the node it stands at, and the instance it belongs to. */
struct Identity {
  std::string instanceId;
  std::string nodeId;
};

static Identity identify(const Execution::Token& token) {
  auto record = token.jsonify();
  return { record.value("instanceId", std::string()), record.value("nodeId", std::string()) };
}

static std::vector<Identity> waitingOf(const Controller::SequentialPerformer& performer) {
  std::vector<Identity> waiting;
  for (const auto& weak : performer.waiting) {
    if (auto token = weak.lock()) {
      waiting.push_back(identify(*token));
    }
  }
  return waiting;
}

static bool holds(const std::vector<Identity>& waiting, const std::string& nodeId) {
  for (const auto& identity : waiting) {
    if (identity.nodeId == nodeId) {
      return true;
    }
  }
  return false;
}

/**
 * The engine's only pending entry request, or nothing where it is asking for none.
 *
 * The caller lets go of what it answers: a request is held by its token and reported through a weak
 * pointer, so one kept alive here would still read as pending after the engine had withdrawn it.
 */
static std::shared_ptr<const Execution::DecisionRequest> entryRequest(const std::shared_ptr<Controller>& controller) {
  for (const auto& weak : controller->getPendingRequests()) {
    auto request = weak.lock();
    if (request && request->type == Execution::Observable::Type::EntryRequest) {
      return request;
    }
  }
  return nullptr;
}

int main(int argc, char** argv) {
  std::string fixtureDir = (argc > 1) ? argv[1] : "test/fixtures";
  std::string instanceCsv =
    "INSTANCE_ID; NODE_ID; INITIALIZATION\n"
    "Instance_1; Process_1;\n";

  // ---------------------------------------------------------------- the plain sequential ad-hoc subprocess
  {
    Input input(readFile(fixtureDir + "/AdHocSubProcess.bpmn"));
    input.setInstance(instanceCsv);
    auto monitor = std::make_shared<Monitor>();
    auto controller = Test::interactiveController();
    Engine engine(std::make_unique<Model::StochasticDataProvider>(input.release(), 0), controller, monitor);

    engine.run();

    auto performers = controller->getSequentialPerformers();
    check(performers.size() == 1, "one performer is reported");

    auto performer = performers.front().performer.lock();
    check(performer != nullptr, "the performer's token is alive");
    check(identify(*performer).nodeId == "AdHocSubProcess_1",
      "the ad-hoc subprocess performs for its own children");
    check(performers.front().performing.expired(), "it is conducting nothing");

    auto waiting = waitingOf(performers.front());
    check(waiting.size() == 2, "both children are waiting");
    check(waiting[0].nodeId == "Activity_1" && waiting[1].nodeId == "Activity_2",
      "and are reported in the order they became ready");

    auto request = entryRequest(controller);
    check(request != nullptr, "the engine asks for an entry");
    std::string entered = identify(*request->token).nodeId;
    check(controller->enqueueEntryDecision(request, std::nullopt).has_value(), "the entry is enqueued");
    // A request is owned by its token and held weakly everywhere else, so withdrawing it is a reset and
    // the pending list prunes itself. That works only while nobody else holds it: a request is locked to
    // be used and let go at once, and one kept across a resume would read back as still pending.
    request.reset();
    engine.resume();

    performers = controller->getSequentialPerformers();
    check(performers.size() == 1, "the performer is still reported");
    check(performers.front().performing.expired(),
      "and conducts nothing again, its child having run through to its exit");

    waiting = waitingOf(performers.front());
    check(waiting.size() == 1 && waiting[0].nodeId != entered, "only the other child waits");

    auto renewed = entryRequest(controller);
    check(renewed != nullptr && identify(*renewed->token).nodeId == waiting[0].nodeId,
      "and it is asked for afresh now that the performer is idle");
  }

  // ------------------------------------------------------- a multi-instance activity within such a subprocess
  {
    Input input(readFile(fixtureDir + "/AdHocSubProcess_multi_instance.bpmn"));
    input.setInstance(instanceCsv);
    auto monitor = std::make_shared<Monitor>();
    auto controller = Test::interactiveController();
    Engine engine(std::make_unique<Model::StochasticDataProvider>(input.release(), 0), controller, monitor);

    engine.run();

    auto performers = controller->getSequentialPerformers();
    check(performers.size() == 1, "one performer is reported for the multi-instance model");

    auto waiting = waitingOf(performers.front());
    check(waiting.size() == 3, "the two instances of the first activity and the second activity wait");
    check(holds(waiting, "Activity_2"), "the plain activity is among them");

    int instances = 0;
    bool main = false;
    for (const auto& identity : waiting) {
      if (identity.nodeId != "Activity_1") {
        continue;
      }
      if (identity.instanceId == "Instance_1") {
        main = true;
      } else if (identity.instanceId.find("Activity_1") != std::string::npos) {
        ++instances;
      }
    }
    check(instances == 2, "each instance token queues under an instance identity of its own");
    check(!main, "and the token of the activity itself queues for nothing, never becoming ready");

    // These activities have a duration, so the engine stops with the entered token still busy rather than
    // running it through to its exit, and the performer is caught holding it.
    auto request = entryRequest(controller);
    check(request != nullptr, "the engine asks for an entry");
    Identity entered = identify(*request->token);
    check(controller->enqueueEntryDecision(request, std::nullopt).has_value(), "the entry is enqueued");
    request.reset();
    engine.resume();

    performers = controller->getSequentialPerformers();
    check(performers.size() == 1, "the performer is still reported");

    auto performing = performers.front().performing.lock();
    check(performing != nullptr, "it is conducting a token");
    check(identify(*performing).instanceId == entered.instanceId
      && identify(*performing).nodeId == entered.nodeId,
      "which is the one that was entered, an instance token where that is what waited");

    waiting = waitingOf(performers.front());
    check(waiting.size() == 2, "the rest still waits");
    check(entryRequest(controller) == nullptr,
      "and is asked for nothing while the performer is busy");
  }

  std::cerr << "ALL PASSED (sequential performer)\n";
  return 0;
}
