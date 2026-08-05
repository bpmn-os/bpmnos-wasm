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

/** The name the caller knows a value type by, which is the name the moddle extension states it under. */
const char* typeName(BPMNOS::ValueType type) {
  switch (type) {
    case BPMNOS::BOOLEAN: return "boolean";
    case BPMNOS::INTEGER: return "integer";
    case BPMNOS::DECIMAL: return "decimal";
    case BPMNOS::STRING: return "string";
    case BPMNOS::COLLECTION: return "collection";
  }
  return "";
}

/**
 * @brief Collects, per decision task, the choices it states, descending the scope.
 *
 * A choice is stated as an enumeration or as a pair of bounds, and which of the two it is has been settled
 * by `Choice::Choice` when the model was built: a caller reading the condition again would restate the
 * engine's own grammar in another language, and would be wrong wherever the two readings differed. So the
 * kind is taken from what the choice holds rather than from the text it was built from.
 *
 * Nothing is evaluated. What a choice may take depends on the status, the data and the globals and is a
 * question for an engine standing at the token; what a task states is a property of the model, and that is
 * all that is reported here.
 *
 * @param scope The scope to descend.
 * @param decisions The decision tasks found so far, in the order the scope states them.
 */
void collect(const BPMN::Scope& scope, json& decisions) {
  for (const auto& child : scope.childNodes) {
    if (child->represents<Model::DecisionTask>() && child->extensionElements) {
      if (auto* extensionElements = child->extensionElements->as<Model::ExtensionElements>()) {
        json choices = json::array();
        for (const auto& choice : extensionElements->choices) {
          choices.push_back(json{
            { "attribute", json{
              { "id", choice->attribute->id },
              { "name", choice->attribute->name },
              { "type", typeName(choice->attribute->type) }
            } },
            { "kind", choice->enumeration.empty() ? "bounds" : "enumeration" },
            { "discretized", choice->enumeration.empty() && choice->multipleOf != nullptr }
          });
        }
        decisions.push_back(json{ { "node", child->id }, { "choices", std::move(choices) } });
      }
    }
    if (auto* childScope = child->represents<BPMN::Scope>()) {
      collect(*childScope, decisions);
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
    json decisions = json::array();
    for (const auto& process : model.processes) {
      collect(*process, performers);
      collect(*process, decisions);
    }

    json sequentialPerformers = json::array();
    for (const auto& [performer, activities] : performers) {
      sequentialPerformers.push_back(json{ {"performer", performer}, {"activities", activities} });
    }

    return json{
      {"sequentialPerformers", std::move(sequentialPerformers)},
      {"decisions", std::move(decisions)}
    };
  });
}

} // namespace BPMNOS::WASM
