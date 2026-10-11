// Native test of inputs and objects through the bridge.
//
// The fixture's task assigns the cost of its route, read from a matrix input, and its route is a status
// array of two elements. The input's content is supplied by its source name, the entry of the task is left to
// the caller, and the caller enters it with a status in the shape a token record renders, which changes the
// route. The test checks that the input is reported and read, that the status given replaces the route and
// thereby the cost, and that a status naming no status attribute, or giving a route longer than its fixed
// length, is refused.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "Controller.h"
#include "composition.h"
#include "Convert.h"
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
  std::string modelXml = readFile(fixtureDir + "/Matrix_input.bpmn");
  std::string distanceCsv = "0;5;6\n5;0;7\n6;7;0\n";
  std::string instanceCsv =
    "INSTANCE_ID; NODE_ID; INITIALIZATION\n"
    "Instance_1; Process_1; route := [1, 2]\n";

  Input input(modelXml);
  json required = input.getInputSources();
  check(required.is_array() && required.size() == 1 && required[0] == "distance.csv",
        "getInputSources reports the source of the matrix input");
  input.addInputContent("distance.csv", distanceCsv);
  input.setInstance(instanceCsv);

  // the entries are left to the caller, so that the task is entered with the status given
  auto monitor = std::make_shared<Monitor>();
  auto controller = Test::interactiveController();
  controller->deactivate(2);
  Engine engine(Test::dataProvider(input), controller, monitor);
  Test::Driver driver(engine);

  json log = json::array();
  monitor->addObserver([&](const json& entry) { log.push_back(entry); });

  driver.run();

  auto findEntryRequest = [&]() -> std::shared_ptr<const Execution::DecisionRequest> {
    for (const auto& weak : controller->getPendingRequests()) {
      auto request = weak.lock();
      if (request && request->type == Execution::Observable::Type::EntryRequest) {
        return request;
      }
    }
    return nullptr;
  };

  // every entry before the task's is made without a status
  auto request = findEntryRequest();
  int guard = 0;
  while (request && request->token->node->id != "Task_1" && guard++ < 20) {
    check(controller->enqueueEntryDecision(request, std::nullopt).has_value(), "an entry before the task is accepted");
    driver.proceed();
    request = findEntryRequest();
  }
  check(request && request->token->node->id == "Task_1", "the engine waits for the entry of the task");
  const auto& token = *request->token;

  check(!toStatus(token, json{ {"distance", 1} }).has_value(), "a status naming an input is refused");
  check(!toStatus(token, json{ {"unknown", 1} }).has_value(), "a status naming no attribute is refused");
  check(!toStatus(token, json{ {"route", json::array({1, 2, 3})} }).has_value(),
        "a route longer than its fixed length is refused");
  check(!toStatus(token, json{ {"cost", "far"} }).has_value(), "a text for a decimal is refused");

  auto status = toStatus(token, json{ {"route", json::array({3, 1})} });
  check(status.has_value(), "a status changing the route is accepted");
  check(controller->enqueueEntryDecision(request, std::move(status.value())).has_value(),
        "the entry of the task is accepted with the status");
  driver.proceed();

  // the task completes with the route given and the cost of that route
  bool completed = false;
  for (const auto& entry : log) {
    if (!entry.contains("token")) {
      continue;
    }
    const auto& record = entry["token"];
    if (record.value("nodeId", std::string()) != "Task_1" || record.value("state", std::string()) != "COMPLETED") {
      continue;
    }
    completed = true;
    check(record["status"]["route"] == json::array({3, 1}), "the task completes with the route given");
    check(record["status"]["cost"] == 6.0, "the cost is read from the matrix input for the route given");
  }
  check(completed, "the task completed");

  std::cerr << "ALL PASSED (objects)\n";
  return 0;
}
