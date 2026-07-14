#ifndef BPMNOS_WASM_CONVERT_H
#define BPMNOS_WASM_CONVERT_H

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

/// Converts a JSON double to the engine's fixed-point number.
inline BPMNOS::number toNumber(double value) {
  return static_cast<BPMNOS::number>(value);
}

/// Converts the engine's fixed-point number to a double for JSON.
inline double toDouble(BPMNOS::number value) {
  return static_cast<double>(value);
}

/// Converts a JSON array of numbers or nulls into a status or data value vector.
inline BPMNOS::Values toValues(const json& array) {
  BPMNOS::Values values;
  for (const auto& element : array) {
    if (element.is_null()) {
      values.push_back(std::optional<BPMNOS::number>{});
    }
    else {
      values.push_back(std::optional<BPMNOS::number>{ toNumber(element.get<double>()) });
    }
  }
  return values;
}

/// Converts a JSON array of numbers into a vector of choice values.
inline std::vector<BPMNOS::number> toChoiceValues(const json& array) {
  std::vector<BPMNOS::number> values;
  for (const auto& element : array) {
    values.push_back(toNumber(element.get<double>()));
  }
  return values;
}

/// Runs the given function and returns its JSON result, converting any thrown
/// exception into {"error": message} so the boundary never propagates a C++ throw.
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
