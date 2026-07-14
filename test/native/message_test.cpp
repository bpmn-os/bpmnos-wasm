// Native message delivery test for the interactive bridge.
//
// It drives the simple messaging fixture, in which one instance throws a message and another
// waits to catch it, through the run, stop, and resume model with no clock. The driver supplies
// whatever decision the engine is waiting for, and delivers a message only once a candidate for
// it exists, which is the point of the test: the waiting token's pending decision must offer the
// thrown message as a candidate, and delivering it must let both instances complete their events.

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

static bool logHas(const json& fullLog, const std::string& key,
                   const std::string& field, const std::string& value) {
  for (const auto& entry : fullLog) {
    if (entry.contains(key) && entry[key].value(field, std::string()) == value) {
      return true;
    }
  }
  return false;
}

int main(int argc, char** argv) {
  std::string fixtureDir = (argc > 1) ? argv[1] : "test/fixtures";
  std::string modelXml = readFile(fixtureDir + "/Simple_messaging.bpmn");
  std::string instanceCsv =
    "INSTANCE_ID; NODE_ID; INITIALIZATION\n"
    "Instance_1; Process_1; timestamp := 0\n"
    "Instance_2; Process_2; timestamp := 0\n";

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

  json fullLog = json::array();
  auto absorb = [&](const json& snapshot) {
    if (snapshot.contains("log")) {
      for (const auto& entry : snapshot["log"]) {
        fullLog.push_back(entry);
      }
    }
  };
  absorb(state);

  bool madeDelivery = false;
  bool sawCandidate = false;
  int guard = 0;
  while (guard++ < 100) {
    const auto& pending = state["pending"];
    if (pending.empty()) {
      break;
    }
    bool acted = false;
    for (const auto& decisionRequest : pending) {
      std::string type = decisionRequest["type"];
      json decision = { {"requestId", decisionRequest["requestId"]}, {"type", type} };
      if (type == "choice") {
        json choices = json::array();
        for (const auto& choice : decisionRequest["choices"]) {
          choices.push_back(choice["enumeration"][0]);
        }
        decision["choices"] = choices;
      }
      else if (type == "messageDelivery") {
        if (decisionRequest["candidates"].empty()) {
          continue; // no message to deliver yet; drive the sender first
        }
        sawCandidate = true;
        decision["messageId"] = decisionRequest["candidates"][0]["messageId"];
      }
      json accepted = controller.submitDecision(decision);
      if (accepted.contains("rejected")) {
        continue;
      }
      if (type == "messageDelivery") {
        madeDelivery = true;
      }
      state = engine.resume();
      check(!state.contains("error"), "resume");
      absorb(state);
      acted = true;
      break;
    }
    if (!acted) {
      break;
    }
  }

  std::cerr << "full log: " << fullLog.dump() << "\n";
  check(sawCandidate, "the waiting token was offered the thrown message as a candidate");
  check(madeDelivery, "a message delivery decision was driven");
  check(logHas(fullLog, "message", "state", "DELIVERED"), "the message was delivered");
  check(state["pending"].empty(), "the engine quiesced with no pending decision");

  std::cerr << "ALL PASSED\n";
  return 0;
}
