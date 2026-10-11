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
 * The inputs a model declares, its lookup tables, matrices and objects, are a property of its parsed tree, so
 * this parses the BPMN XML once, reports the sources of the inputs through getInputSources, and accumulates
 * the content of each input and the instance data. When an Engine is constructed, the model is built from
 * the tree and the contents of the inputs, and the data provider from the model and the instance data. A
 * caller works through this wrapper because the parsed tree is held as a unique pointer, which does not cross
 * the JavaScript boundary.
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
   * @brief Reports the sources of the inputs the model declares, so that the caller supplies the content of
   * each.
   *
   * @return A JSON array of the source names, or {"error": message} on failure.
   */
  json getInputSources() const;

  /**
   * @brief Provides the content of one input, keyed by its source name.
   *
   * @param source The source name of the input.
   * @param content The content: CSV for a lookup or a matrix, JSON for an object.
   */
  void addInputContent(const std::string& source, const std::string& content);

  /**
   * @brief Provides the instance data.
   *
   * @param csv The instance CSV content.
   */
  void setInstance(const std::string& csv);

  /**
   * @brief Builds the model from the parsed tree and the contents of the inputs, leaving this without a tree. Called
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
  std::unordered_map<std::string, std::string> inputContents; ///< The contents of the inputs keyed by source name.
  std::string instance;                                       ///< The instance CSV content.
};

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_INPUT_H
