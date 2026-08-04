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

  // a model with nothing sequential about it
  {
    auto described = describeModel(readFile(fixtureDir + "/Timer.bpmn"), {});
    check(!described.contains("error"), "a model without a sequential ad hoc subprocess is described");
    check(described["sequentialPerformers"].empty(), "and reports no performer");
  }

  // a model that cannot be built says so rather than throwing
  {
    auto described = describeModel("not a model", {});
    check(described.contains("error"), "a model that cannot be parsed is reported as an error");
  }

  std::cerr << "ALL PASSED (model description)\n";
  return 0;
}
