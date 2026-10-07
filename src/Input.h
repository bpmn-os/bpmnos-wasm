#ifndef BPMNOS_WASM_INPUT_H
#define BPMNOS_WASM_INPUT_H

#include <memory>
#include <string>
#include <unordered_map>

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
 * once, reports the referenced lookup tables through getLookupTableNames, and accumulates each lookup
 * table's content and the instance data. When an Engine is constructed, the model is built from the tree
 * and the lookup tables, and the data provider from the model and the instance data. A caller works through
 * this wrapper because the parsed tree is held as a unique pointer, which does not cross the JavaScript
 * boundary.
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
  json getLookupTableNames() const;

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
   * @brief Builds the model from the parsed tree and the lookup tables, leaving this without a tree. Called
   * once, when the data provider of an Engine is built.
   *
   * @return The model, shared by the data provider and every engine of a run.
   * @throws std::runtime_error if the model has been built before.
   */
  std::shared_ptr<const Model::Model> buildModel();

  /**
   * @brief Reports the instance data.
   *
   * @return The instance CSV content.
   */
  const std::string& getInstance() const;

private:
  std::unique_ptr<XML::XMLObject> model;                      ///< The parsed model tree, until the model is built.
  std::unordered_map<std::string, std::string> lookupTables;  ///< The lookup table contents keyed by source name.
  std::string instance;                                       ///< The instance CSV content.
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_INPUT_H
