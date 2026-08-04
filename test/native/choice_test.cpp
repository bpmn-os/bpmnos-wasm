// Native choice test for the interactive bridge.
//
// The decision task fixture enters an activity that offers a choice from an enumeration and exits it.
// With an interactive controller the entry and the exit are resolved automatically, so the only decision
// left for the caller is the choice. This checks that the engine stops exactly at the choice, that the
// choice is offered with its enumeration keyed by the token's instance and node, and that an enqueued
// value is applied and appears on the completed activity. The instance sets x to minus two, so the
// enumeration offered is the set from the model with x substituted.

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
  auto controller = Test::interactiveController();
  Engine engine(std::make_unique<Model::StochasticDataProvider>(input.release(), 0), controller, monitor);

  json log = json::array();
  monitor->addObserver([&](const json& entry) { log.push_back(entry); });

  engine.run();

  // Entry and exit are resolved automatically, so the only pending decision is the choice.
  auto findChoiceRequest = [&]() -> std::shared_ptr<const Execution::DecisionRequest> {
    for (const auto& weak : controller->getPendingRequests()) {
      auto request = weak.lock();
      if (request && request->type == Execution::Observable::Type::ChoiceRequest) {
        return request;
      }
    }
    return nullptr;
  };

  auto request = findChoiceRequest();
  check(request != nullptr, "the engine stopped at the choice");

  double enqueuedChoice = 0;
  int choiceCount = 0;
  int guard = 0;
  while (request && guard++ < 50) {
    ++choiceCount;

    // The candidates are asked for one choice at a time, each against the values already selected, since
    // what a later choice admits depends on the earlier ones. Nothing is returned once every choice has a
    // value, which is what ends the walk.
    std::vector<BPMNOS::number> choices;
    while (auto candidates = controller->getChoiceCandidates(request.get(), choices)) {
      const auto& [attribute, values] = candidates.value();
      check(std::holds_alternative<EnumeratedChoice>(values), "the choice offers an enumeration");
      const auto& enumeration = std::get<EnumeratedChoice>(values);
      check(!enumeration.empty(), "the enumeration offers allowed values");
      choices.push_back(enumeration.front());
      enqueuedChoice = static_cast<double>(enumeration.front());
    }
    check(choices.size() == controller->getChoices(request.get()).size(),
      "the walk ends with a value for every choice");
    check(controller->enqueueChoiceDecision(request, choices).has_value(), "enqueueChoiceDecision accepted");
    engine.resume();
    request = findChoiceRequest();
  }
  check(request == nullptr, "no decision is pending after the choice");
  check(choiceCount == 1, "exactly one choice was made");

  bool applied = false;
  for (const auto& entry : log) {
    if (entry.contains("token")) {
      const auto& token = entry["token"];
      if (token.value("nodeId", std::string()) == "Activity_1"
          && token.value("state", std::string()) == "COMPLETED"
          && token.contains("status") && token["status"].contains("choice")
          && token["status"]["choice"].get<double>() == enqueuedChoice) {
        applied = true;
      }
    }
  }
  check(applied, "the enqueued choice was applied on Activity_1 at COMPLETED");

  // A decision task whose second choice depends on the first. Its bounds are `base <= level <= base + 4`
  // with a discretizer of two, so choosing a base of two admits the even numbers from two to six, and
  // choosing five admits six and eight — a different set, and one whose lower bound is not the bound the
  // model states, since the admitted values are the multiples of the discretizer and five is not one.
  {
    std::string dependentXml = readFile(fixtureDir + "/DecisionTask_with_dependent_choices.bpmn");
    std::string dependentCsv =
      "INSTANCE_ID; NODE_ID; INITIALIZATION\n"
      "Instance_1; Process_1;\n";

    Input dependentInput(dependentXml);
    dependentInput.setInstance(dependentCsv);
    auto dependentMonitor = std::make_shared<Monitor>();
    auto dependentController = Test::interactiveController();
    Engine dependentEngine(
      std::make_unique<Model::StochasticDataProvider>(dependentInput.release(), 0),
      dependentController, dependentMonitor);

    dependentEngine.run();

    std::shared_ptr<const Execution::DecisionRequest> dependent;
    for (const auto& weak : dependentController->getPendingRequests()) {
      auto candidate = weak.lock();
      if (candidate && candidate->type == Execution::Observable::Type::ChoiceRequest) {
        dependent = candidate;
      }
    }
    check(dependent != nullptr, "the engine stopped at the dependent choice");
    check(dependentController->getChoices(dependent.get()).size() == 2, "the task states two choices");

    auto boundsFor = [&](std::vector<BPMNOS::number> selectedValues) {
      auto candidates = dependentController->getChoiceCandidates(dependent.get(), selectedValues);
      check(candidates.has_value(), "a second choice is offered");
      const auto& [attribute, values] = candidates.value();
      check(attribute->name == "level", "the second choice is of the level");
      check(std::holds_alternative<BoundedChoice>(values), "the second choice is bounded");
      return std::get<BoundedChoice>(values);
    };

    auto first = dependentController->getChoiceCandidates(dependent.get(), {});
    check(first.has_value(), "a first choice is offered");
    check(std::get<0>(first.value())->name == "base", "the first choice is of the base");

    auto withBaseTwo = boundsFor({ 2 });
    check((double)std::get<0>(withBaseTwo) == 2.0, "a base of two admits from two");
    check((double)std::get<1>(withBaseTwo) == 6.0, "a base of two admits up to six");
    check(std::get<2>(withBaseTwo).has_value() && (double)*std::get<2>(withBaseTwo) == 2.0,
      "the discretizer is two");

    auto withBaseFive = boundsFor({ 5 });
    check((double)std::get<0>(withBaseFive) == 6.0,
      "a base of five admits from six, the first multiple of the discretizer at or above the bound");
    check((double)std::get<1>(withBaseFive) == 8.0,
      "a base of five admits up to eight, the last multiple at or below the bound");

    check(!dependentController->getChoiceCandidates(dependent.get(), { 2, 4 }).has_value(),
      "nothing is offered once every choice has a value");
  }

  std::cerr << "ALL PASSED (choice)\n";
  return 0;
}
