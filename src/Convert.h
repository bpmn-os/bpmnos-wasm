#ifndef BPMNOS_WASM_CONVERT_H
#define BPMNOS_WASM_CONVERT_H

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <bpmn++.h>
#include <bpmnos-model.h>
#include <bpmnos-execution.h>

// Small conversions between the engine's fixed-point numeric type and JSON doubles,
// and a guard that turns any thrown exception into a structured error object so that an
// engine throw becomes a value the caller can inspect rather than a crash of the module.

namespace BPMNOS::WASM {

using json = nlohmann::ordered_json;

/**
 * @brief Converts a JSON double to the engine's fixed-point number.
 *
 * @param value The JSON double.
 * @return The equivalent fixed-point number.
 */
inline BPMNOS::number toNumber(double value) {
  return static_cast<BPMNOS::number>(value);
}

/**
 * @brief Converts the engine's fixed-point number to a double for JSON.
 *
 * @param value The fixed-point number.
 * @return The equivalent double.
 */
inline double toDouble(BPMNOS::number value) {
  return static_cast<double>(value);
}

/**
 * @brief Returns the status of a token with the values a caller gives, in the shape in which a token record
 * renders a status.
 *
 * The given status is a JSON object keyed by the names of status attributes. A scalar attribute takes a
 * number, a truth value for a boolean, or a text for a string, as the record renders it, and null makes it
 * undefined. An object attribute takes the JSON the record renders it as, which is fitted to the attribute as
 * a whole assignment is: padded with undefined values at a fixed dimension, rejected if longer, and taking
 * the length at an open one; null makes every value undefined and keeps the lengths. An attribute the given
 * status does not name keeps the value the token holds.
 *
 * @param token The token whose status is given.
 * @param given The status given.
 * @return The status, or the reason the given status is refused.
 */
inline std::expected<BPMNOS::Status, std::string> toStatus(const Execution::Token& token, const json& given) {
  if (!given.is_object()) {
    return std::unexpected("a status must be an object keyed by the names of status attributes");
  }
  const auto& registry = token.getAttributeRegistry();
  BPMNOS::Status status = token.status;
  for (const auto& [name, value] : given.items()) {
    const Model::Attribute* attribute = registry.contains(name) ? registry[name] : nullptr;
    if (!attribute || attribute->category != Model::Attribute::Category::STATUS) {
      return std::unexpected("'" + name + "' is no status attribute of the token");
    }
    if (attribute->isObject()) {
      auto& object = status.objects[attribute->index];
      if (value.is_null()) {
        auto undefined = std::make_shared<BPMNOS::Object>();
        undefined->layout = object->layout;
        undefined->values.resize(object->values.size());
        object = std::move(undefined);
        continue;
      }
      try {
        // the target schema of a whole assignment, built from the effective schema of the attribute, which the data
        // provider gives, and the layout of the object the token holds
        const auto& effectiveSchema = token.owner->systemState->scenario->dataProvider->getSchema(attribute);
        object = std::make_shared<BPMNOS::Object>(BPMNOS::Schema(effectiveSchema, *object->layout), nlohmann::json::parse(value.dump()));
      }
      catch (const std::exception& error) {
        return std::unexpected("'" + name + "' cannot take the value given: " + std::string(error.what()));
      }
      continue;
    }
    auto& slot = status.attributes[attribute->index];
    auto type = std::get<BPMNOS::ValueType>(attribute->type);
    if (value.is_null()) {
      slot = std::nullopt;
    }
    else if (type == BPMNOS::BOOLEAN && value.is_boolean()) {
      slot = BPMNOS::number(value.get<bool>() ? 1 : 0);
    }
    else if (type == BPMNOS::STRING && value.is_string()) {
      slot = BPMNOS::to_number(value.get<std::string>(), BPMNOS::STRING);
    }
    else if ((type == BPMNOS::INTEGER || type == BPMNOS::DECIMAL) && value.is_number()) {
      slot = toNumber(value.get<double>());
    }
    else {
      return std::unexpected("'" + name + "' cannot take the value given");
    }
  }
  return status;
}

/**
 * @brief Converts a JSON array of numbers into a vector of choice values.
 *
 * @param array The JSON array of numbers.
 * @return The vector of choice values.
 */
inline std::vector<BPMNOS::number> toChoiceValues(const json& array) {
  std::vector<BPMNOS::number> values;
  for (const auto& element : array) {
    values.push_back(toNumber(element.get<double>()));
  }
  return values;
}

/**
 * @brief Runs the given function and returns its JSON result, converting any thrown exception into
 * {"error": message} so the boundary never propagates a C++ throw.
 *
 * @param function The function to run.
 * @return The function's JSON result, or {"error": message} when it throws.
 */
template <typename Function>
inline json guarded(Function&& function) {
  try {
    return function();
  }
  catch (const std::exception& error) {
    return json{ {"error", std::string(error.what())} };
  }
  catch (...) {
    return json{ {"error", "unknown error"} };
  }
}

} // namespace BPMNOS::WASM

#endif // BPMNOS_WASM_CONVERT_H
