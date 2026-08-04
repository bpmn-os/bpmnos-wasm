#include "Structure.h"

#include <map>
#include <memory>
#include <vector>

#include "Convert.h"

namespace BPMNOS::WASM {
namespace {

/**
 * @brief Collects, per performer node, the activities it performs, descending the scope.
 *
 * A sequential ad hoc subprocess names the node that performs for it, which the model resolved when it was
 * built, and the activities it performs are that subprocess's own children. One node may perform for
 * several subprocesses, so what is collected is a map rather than a list.
 *
 * @param scope The scope to descend.
 * @param performers The activities found so far, keyed by the node performing them.
 */
void collect(const BPMN::Scope& scope, std::map<std::string, std::vector<std::string>>& performers) {
  for (const auto& child : scope.childNodes) {
    if (auto* adHocSubProcess = child->represents<Model::SequentialAdHocSubProcess>()) {
      auto& activities = performers[adHocSubProcess->performer->id];
      for (const auto& candidate : adHocSubProcess->childNodes) {
        if (candidate->represents<BPMN::Activity>()) {
          activities.push_back(candidate->id);
        }
      }
    }
    if (auto* childScope = child->represents<BPMN::Scope>()) {
      collect(*childScope, performers);
    }
  }
}

} // namespace

json describeModel(
  const std::string& bpmnXml,
  const std::unordered_map<std::string, std::string>& lookupTables) {
  return guarded([&] {
    auto* root = XML::XMLObject::createFromString(bpmnXml);
    if (!root) {
      throw std::runtime_error("failed to parse BPMN model");
    }
    // The model is built here because what is reported is what building resolves; the lookup tables are
    // needed for that build and for nothing else this call does.
    Model::Model model(std::unique_ptr<XML::XMLObject>(root), lookupTables);

    std::map<std::string, std::vector<std::string>> performers;
    for (const auto& process : model.processes) {
      collect(*process, performers);
    }

    json sequentialPerformers = json::array();
    for (const auto& [performer, activities] : performers) {
      sequentialPerformers.push_back(json{ {"performer", performer}, {"activities", activities} });
    }

    return json{ {"sequentialPerformers", std::move(sequentialPerformers)} };
  });
}

} // namespace BPMNOS::WASM
