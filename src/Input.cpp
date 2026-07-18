#include "Input.h"

#include <stdexcept>
#include <utility>

#include "Convert.h"

namespace BPMNOS::WASM {

Input::Input(const std::string& bpmnXml) {
  // Parse once here, so the tree is held for the lifetime of this input and reused when the engine is
  // built. Nothing is written to a filesystem; the model crosses the boundary as text and is parsed
  // with the engine's own parser.
  auto* root = XML::XMLObject::createFromString(bpmnXml);
  if (!root) {
    throw std::runtime_error("failed to parse BPMN model");
  }
  input.model = std::unique_ptr<XML::XMLObject>(root);
}

json Input::requiredLookupTables() const {
  return guarded([&] {
    return json(Model::Model::getLookupTableNames(*input.model));
  });
}

void Input::addLookupTable(const std::string& name, const std::string& csv) {
  input.lookupTables[name] = csv;
}

void Input::setInstance(const std::string& csv) {
  input.instance = csv;
}

Model::Input Input::release() {
  return std::move(input);
}

} // namespace BPMNOS::WASM
