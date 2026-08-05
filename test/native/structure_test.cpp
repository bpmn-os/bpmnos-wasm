// Native model description test.
//
// A caller that replays a run rather than reading the engine still needs what the model resolves: which
// nodes perform sequentially, and which activities each of them performs. This drives two fixtures, one
// whose ad hoc subprocess performs for its own children and one whose performer is the enclosing process,
// so that both ends of the resolution are covered.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "Structure.h"

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

int main(int argc, char** argv) {
  std::string fixtureDir = (argc > 1) ? argv[1] : "test/fixtures";

  // an ad hoc subprocess performing for its own children
  {
    auto described = describeModel(readFile(fixtureDir + "/AdHocSubProcess.bpmn"), {});
    check(!described.contains("error"), "the model is described");

    const auto& performers = described["sequentialPerformers"];
    check(performers.size() == 1, "one performer is reported");
    check(performers[0]["performer"] == "AdHocSubProcess_1", "which is the subprocess itself");
    check(performers[0]["activities"].size() == 2, "performing both of its activities");
    check(performers[0]["activities"][0] == "Activity_1" && performers[0]["activities"][1] == "Activity_2",
      "named as the model names them");
  }

  // a process carrying the sequential performer of the ad hoc subprocess within it
  {
    auto described = describeModel(readFile(fixtureDir + "/SequentialPerformer.bpmn"), {});
    check(!described.contains("error"), "the model with an explicit performer is described");

    const auto& performers = described["sequentialPerformers"];
    check(performers.size() == 1, "one performer is reported");
    check(performers[0]["performer"] == "Process_1", "which is the process carrying the role");
    check(performers[0]["activities"].size() == 2,
      "performing the activities of the ad hoc subprocess it stands over");
  }

  // the choices a decision task states, which the model settled when it built each of them
  {
    auto described = describeModel(readFile(fixtureDir + "/DecisionTask_with_dependent_choices.bpmn"), {});
    check(!described.contains("error"), "the model with a decision task is described");

    const auto& decisions = described["decisions"];
    check(decisions.size() == 1, "one decision task is reported");
    check(decisions[0]["node"] == "Activity_1", "named as the model names it");

    const auto& choices = decisions[0]["choices"];
    check(choices.size() == 2, "stating both of its choices, in the order it states them");
    check(choices[0]["attribute"]["name"] == "base", "the first of the base");
    check(choices[0]["attribute"]["type"] == "integer", "whose type is reported with it");
    check(!choices[0]["attribute"]["id"].get<std::string>().empty(),
      "as is the identifier it is resolved by");
    check(choices[0]["kind"] == "enumeration", "and which is stated as an enumeration");
    check(choices[0]["discretized"] == false, "an enumeration having no discretizer");
    check(choices[1]["attribute"]["name"] == "level", "the second of the level");
    check(choices[1]["kind"] == "bounds", "which is stated as a pair of bounds");
    check(choices[1]["discretized"] == true, "and states a discretizer");
  }

  // a bounded choice stating no discretizer is a pair of bounds all the same
  {
    auto described = describeModel(readFile(fixtureDir + "/DecisionTask_with_implicit_step.bpmn"), {});
    const auto& choices = described["decisions"][0]["choices"];
    check(choices.size() == 2, "both choices are reported");
    check(choices[0]["kind"] == "bounds" && choices[1]["kind"] == "bounds", "both stated as bounds");
    check(choices[0]["discretized"] == false && choices[1]["discretized"] == false,
      "neither stating a discretizer");
    check(choices[0]["attribute"]["type"] == "integer" && choices[1]["attribute"]["type"] == "decimal",
      "and their types are what the model declares");
  }

  // a model with nothing sequential about it
  {
    auto described = describeModel(readFile(fixtureDir + "/Timer.bpmn"), {});
    check(!described.contains("error"), "a model without a sequential ad hoc subprocess is described");
    check(described["sequentialPerformers"].empty(), "and reports no performer");
    check(described["decisions"].empty(), "and no decision task");
  }

  // a model that cannot be built says so rather than throwing
  {
    auto described = describeModel("not a model", {});
    check(described.contains("error"), "a model that cannot be parsed is reported as an error");
  }

  std::cerr << "ALL PASSED (model description)\n";
  return 0;
}
