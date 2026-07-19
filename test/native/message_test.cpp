// Native message delivery test for the interactive bridge.
//
// The assignment problem pairs a client that sends a request with a server that waits to receive one.
// The message is not explicitly addressed, so its delivery is not resolved automatically but surfaced
// to the caller as a message delivery decision. This drives one client and one server and checks that
// the engine stops at the delivery, offering the waiting message identified by its origin and sender
// from the header, that enqueuing that identity delivers the message, and that both the sending and
// the receiving task then complete. The message content is derived by the engine from status and data
// and plays no part in the identity the bridge uses.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Controller.h"
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
  std::string modelXml = readFile(fixtureDir + "/Assignment_problem.bpmn");
  std::string costsCsv = readFile(fixtureDir + "/costs.csv");
  std::string instanceCsv =
    "INSTANCE_ID; NODE_ID; INITIALIZATION\n"
    "Client1; ClientProcess;\n"
    "Server1; ServerProcess;\n";

  Input input(modelXml);
  json required = input.requiredLookupTables();
  check(required.is_array() && required.size() == 1 && required[0] == "costs.csv",
        "requiredLookupTables reports the model's lookup table");
  input.addLookupTable("costs.csv", costsCsv);
  input.setInstance(instanceCsv);
  Monitor monitor;
  Controller controller;
  Engine engine(input.release(), Engine::Config{}, &monitor, &controller);

  json log = json::array();
  monitor.addObserver([&](const json& entry) { log.push_back(entry); });

  engine.run();

  auto findMessageRequest = [&]() -> std::shared_ptr<const Execution::DecisionRequest> {
    for (const auto& weak : controller.getPendingRequests()) {
      auto request = weak.lock();
      if (request && request->type == Execution::Observable::Type::MessageDeliveryRequest) {
        return request;
      }
    }
    return nullptr;
  };

  auto request = findMessageRequest();
  check(request != nullptr, "the engine stopped at the message delivery");

  int delivered = 0;
  int guard = 0;
  while (request && guard++ < 50) {
    auto candidates = controller.getMessageCandidates(request.get());
    check(!candidates.empty(), "the delivery offers at least one candidate message");
    check(controller.enqueueMessageDeliveryDecision(request, candidates.front()).has_value(),
          "enqueueMessageDeliveryDecision accepted");
    ++delivered;
    engine.resume();
    request = findMessageRequest();
  }
  check(delivered == 1, "exactly one message was delivered");

  bool sent = false;
  bool received = false;
  for (const auto& entry : log) {
    if (!entry.contains("token")) {
      continue;
    }
    const auto& token = entry["token"];
    if (token.value("state", std::string()) != "COMPLETED") {
      continue;
    }
    std::string nodeId = token.value("nodeId", std::string());
    if (nodeId == "SendRequestTask") sent = true;
    else if (nodeId == "ReceiveRequestTask") received = true;
  }
  check(sent, "the send task completed");
  check(received, "the receive task completed");

  std::cerr << "ALL PASSED (message delivery)\n";
  return 0;
}
