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
    check((double)withBaseTwo.lowest == 2.0, "a base of two admits from two");
    check((double)withBaseTwo.highest == 6.0, "a base of two admits up to six");
    check(withBaseTwo.multipleOf.has_value() && (double)*withBaseTwo.multipleOf == 2.0,
      "the discretizer is two");

    auto withBaseFive = boundsFor({ 5 });
    check((double)withBaseFive.lowest == 6.0,
      "a base of five admits from six, the first multiple of the discretizer at or above the bound");
    check((double)withBaseFive.highest == 8.0,
      "a base of five admits up to eight, the last multiple at or below the bound");
    check((double)withBaseFive.lowerBound == 5.0,
      "and the bound itself is reported beside it, five being unselectable");
    check((double)withBaseFive.upperBound == 9.0, "as is the upper bound, nine");

    check(!dependentController->getChoiceCandidates(dependent.get(), { 2, 4 }).has_value(),
      "nothing is offered once every choice has a value");
  }

  // A bounded choice need not state a discretizer. Where the attribute is an integer or a boolean the type
  // implies one, since such a choice takes whole values and getEnumeration steps them by one; where it is a
  // decimal nothing implies one, and the caller is told there is no step rather than a wrong one.
  {
    std::string implicitXml = readFile(fixtureDir + "/DecisionTask_with_implicit_step.bpmn");
    std::string implicitCsv =
      "INSTANCE_ID; NODE_ID; INITIALIZATION\n"
      "Instance_1; Process_1;\n";

    Input implicitInput(implicitXml);
    implicitInput.setInstance(implicitCsv);
    auto implicitMonitor = std::make_shared<Monitor>();
    auto implicitController = Test::interactiveController();
    Engine implicitEngine(
      std::make_unique<Model::StochasticDataProvider>(implicitInput.release(), 0),
      implicitController, implicitMonitor);

    implicitEngine.run();

    std::shared_ptr<const Execution::DecisionRequest> implicit;
    for (const auto& weak : implicitController->getPendingRequests()) {
      auto candidate = weak.lock();
      if (candidate && candidate->type == Execution::Observable::Type::ChoiceRequest) {
        implicit = candidate;
      }
    }
    check(implicit != nullptr, "the engine stopped at the choice stating no discretizer");

    auto whole = implicitController->getChoiceCandidates(implicit.get(), {});
    check(whole.has_value(), "the integer choice is offered");
    const auto& wholeBounds = std::get<BoundedChoice>(std::get<1>(whole.value()));
    check(wholeBounds.multipleOf.has_value(), "an integer states a step even where the model does not");
    check((double)*wholeBounds.multipleOf == 1.0, "and the step the type implies is one");
    check((double)wholeBounds.lowest == 1.0 && (double)wholeBounds.highest == 5.0,
      "with the bounds unchanged, a step of one moving neither");
    check((double)wholeBounds.lowest == (double)wholeBounds.lowerBound
      && (double)wholeBounds.highest == (double)wholeBounds.upperBound,
      "so every bound is selectable and a reader is told of no imprecision");

    auto fraction = implicitController->getChoiceCandidates(implicit.get(), { 3 });
    check(fraction.has_value(), "the decimal choice is offered");
    const auto& fractionBounds = std::get<BoundedChoice>(std::get<1>(fraction.value()));
    check(!fractionBounds.multipleOf.has_value(),
      "a decimal states no step where the model states none, nothing implying one");
  }

  // A step no number can hold exactly. The grid is the multiples of the step the model states, each rounded
  // only as it is taken, so a third of one is 0.333333 and four of them are 1.333333 rather than 1.333332.
  // Reporting a rounded step instead would move the whole grid: its multiples fall a millionth below the
  // thirds and further below with every multiple, so the first at or above one would be 1.333332 and the
  // last at or below ten would be 9.99999, and neither bound would be selectable although both are.
  {
    std::string fractionalXml = readFile(fixtureDir + "/DecisionTask_with_fractional_step.bpmn");
    std::string fractionalCsv =
      "INSTANCE_ID; NODE_ID; INITIALIZATION\n"
      "Instance_1; Process_1;\n";

    Input fractionalInput(fractionalXml);
    fractionalInput.setInstance(fractionalCsv);
    auto fractionalMonitor = std::make_shared<Monitor>();
    auto fractionalController = Test::interactiveController();
    Engine fractionalEngine(
      std::make_unique<Model::StochasticDataProvider>(fractionalInput.release(), 0),
      fractionalController, fractionalMonitor);

    fractionalEngine.run();

    std::shared_ptr<const Execution::DecisionRequest> fractional;
    for (const auto& weak : fractionalController->getPendingRequests()) {
      auto candidate = weak.lock();
      if (candidate && candidate->type == Execution::Observable::Type::ChoiceRequest) {
        fractional = candidate;
      }
    }
    check(fractional != nullptr, "the engine stopped at the choice discretized by a third");

    auto thirds = fractionalController->getChoiceCandidates(fractional.get(), {});
    check(thirds.has_value(), "the choice is offered");
    const auto& grid = std::get<BoundedChoice>(std::get<1>(thirds.value()));

    check(grid.multipleOf.has_value(), "the step is reported");
    check(*grid.multipleOf > 0.3333333 && *grid.multipleOf < 0.3333334,
      "at the precision it was evaluated at, and not rounded to what a value may hold");

    check((double)grid.lowest == 1.0, "one is three thirds, so the lower bound is itself selectable");
    check((double)grid.highest == 10.0, "and ten is thirty thirds, so the upper bound is too");

    // what the engine will actually accept, which is the set this answer has to describe
    auto admitted = fractionalController->getChoices(fractional.get()).front()->getEnumeration(
      BPMNOS::Values{}, BPMNOS::Values{}, BPMNOS::Values{});
    check(!admitted.empty(), "the engine admits values");
    check((double)admitted.front() == (double)grid.lowest, "the least reported is the least admitted");
    check((double)admitted.back() == (double)grid.highest, "and the greatest reported the greatest admitted");
  }

  std::cerr << "ALL PASSED (choice)\n";
  return 0;
}
