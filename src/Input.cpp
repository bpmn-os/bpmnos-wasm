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
  model = std::unique_ptr<XML::XMLObject>(root);
}

json Input::getLookupTableNames() const {
  return guarded([&] {
    if (!model) {
      throw std::runtime_error("the model has been built");
    }
    return json(Model::Model::getLookupTableNames(*model));
  });
}

void Input::addLookupTable(const std::string& name, const std::string& csv) {
  lookupTables[name] = csv;
}

void Input::setInstance(const std::string& csv) {
  instance = csv;
}

std::shared_ptr<const Model::Model> Input::buildModel() {
  if (!model) {
    throw std::runtime_error("the model has been built");
  }
  return std::make_shared<const Model::Model>(std::move(model), std::move(lookupTables));
}

const std::string& Input::getInstance() const {
  return instance;
}

} // namespace BPMNOS::WASM
