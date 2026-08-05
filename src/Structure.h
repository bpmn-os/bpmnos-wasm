#ifndef BPMNOS_WASM_STRUCTURE_H
#define BPMNOS_WASM_STRUCTURE_H

#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include <bpmn++.h>
#include <bpmnos-model.h>

namespace BPMNOS::WASM {

using json = nlohmann::ordered_json;

/**
 * @brief What a model says, as the engine reads it, for a caller that must know it without running.
 *
 * A model states more than its XML shows, because the engine resolves relations as it builds the tree, and
 * a caller reconstructing such a resolution would restate the engine's semantics in another language. This
 * builds the model once and reports what it resolved. What it reports is a property of the model, so it is
 * the same before a run, during one, and for a run that is merely being replayed.
 *
 * The lookup tables are taken because the engine cannot build a model without the content of every table it
 * references, not because they are read here. Nothing of a run is taken: no instance data, no engine.
 *
 * This is not Input's work. Input assembles what a run is given and hands it to an engine; this answers
 * what the model is.
 *
 * @param bpmnXml The BPMN model XML.
 * @param lookupTables The content of each lookup table the model references, keyed by its source name.
 * @return `{"sequentialPerformers": [{"performer": s, "activities": [s]}], "decisions": [{"node": s,
 *         "choices": [{"attribute": {"id": s, "name": s, "type": s}, "kind": s, "discretized": b}]}]}`, or
 *         `{"error": message}` where the model could not be built. A choice is stated either as an
 *         enumeration or as a pair of bounds, which `kind` names as `"enumeration"` or `"bounds"`, and
 *         `discretized` says whether a bounded one states a discretizer. Nothing here is evaluated: what a
 *         choice may take is a question for an engine standing at the token, and is asked of the
 *         controller.
 */
json describeModel(
  const std::string& bpmnXml,
  const std::unordered_map<std::string, std::string>& lookupTables);

} // namespace BPMNOS::WASM

#endif
