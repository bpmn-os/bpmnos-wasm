#ifndef BPMNOS_WASM_INPUT_H
#define BPMNOS_WASM_INPUT_H

#include <string>

#include <nlohmann/json.hpp>

#include <bpmn++.h>
#include <bpmnos-model.h>
#include <bpmnos-execution.h>

namespace BPMNOS::WASM {

using json = nlohmann::ordered_json;

/**
 * @brief Assembles, in memory, the inputs a run needs, parsing the model once and holding the tree.
 *
 * The lookup tables a model references are a property of its parsed tree, so this parses the BPMN XML
 * once, reports the referenced lookup tables through requiredLookupTables, and accumulates each lookup
 * table's content and the instance data. It yields a BPMNOS::Model::Input, consumed when an Engine is
 * constructed. A caller works through this wrapper because it cannot hold a BPMNOS::Model::Input directly:
 * that struct owns the parsed tree as a unique pointer, which does not cross the JavaScript boundary.
 */
class Input {
public:
  /**
   * @brief Parses the BPMN model XML into a tree.
   *
   * @param bpmnXml The BPMN model XML.
   */
  explicit Input(const std::string& bpmnXml);

  /**
   * @brief Reports the lookup table source names the model references, so the caller supplies each.
   *
   * @return A JSON array of the lookup table source names, or {"error": message} on failure.
   */
  json requiredLookupTables() const;

  /**
   * @brief Provides one lookup table's content, keyed by its source name.
   *
   * @param name The lookup table source name.
   * @param csv The lookup table CSV content.
   */
  void addLookupTable(const std::string& name, const std::string& csv);

  /**
   * @brief Provides the instance data.
   *
   * @param csv The instance CSV content.
   */
  void setInstance(const std::string& csv);

  /**
   * @brief Releases ownership of the assembled input, leaving this empty. Called once, when an Engine
   * is built.
   *
   * @return The assembled BPMNOS::Model::Input.
   */
  Model::Input release();

private:
  Model::Input input;  ///< The model tree, lookup table contents, and instance being assembled.
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_INPUT_H
