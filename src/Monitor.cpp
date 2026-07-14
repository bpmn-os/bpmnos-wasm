#include "Monitor.h"

namespace BPMNOS::WASM {

Monitor::Monitor()
  : log(json::array())
  , drained(0)
{
}

Monitor::~Monitor() = default;

void Monitor::subscribe(Execution::Engine* engine) {
  engine->addSubscriber(
    this,
    Execution::Observable::Type::Token,
    Execution::Observable::Type::Event,
    Execution::Observable::Type::Message
  );
}

void Monitor::notice(const Execution::Observable* observable) {
  using Type = Execution::Observable::Type;
  switch (observable->getObservableType()) {
    case Type::Token:
      log.push_back(json{ {"token", static_cast<const Execution::Token*>(observable)->jsonify()} });
      break;
    case Type::Event:
      log.push_back(json{ {"event", static_cast<const Execution::Event*>(observable)->jsonify()} });
      break;
    case Type::Message:
      log.push_back(json{ {"message", static_cast<const Execution::Message*>(observable)->jsonify()} });
      break;
    default:
      break;
  }
}

json Monitor::drainLog() {
  json delta = json::array();
  for (std::size_t index = drained; index < log.size(); ++index) {
    delta.push_back(log[index]);
  }
  drained = log.size();
  return delta;
}

} // namespace BPMNOS::WASM
