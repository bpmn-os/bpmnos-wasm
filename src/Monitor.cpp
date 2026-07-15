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

void Monitor::onNotice(std::function<void(const json&)> callback) {
  sink = std::move(callback);
}

void Monitor::notice(const Execution::Observable* observable) {
  using Type = Execution::Observable::Type;
  json entry;
  switch (observable->getObservableType()) {
    case Type::Token:
      entry = json{ {"token", static_cast<const Execution::Token*>(observable)->jsonify()} };
      break;
    case Type::Event:
      entry = json{ {"event", static_cast<const Execution::Event*>(observable)->jsonify()} };
      break;
    case Type::Message:
      entry = json{ {"message", static_cast<const Execution::Message*>(observable)->jsonify()} };
      break;
    default:
      return;
  }
  log.push_back(entry);
  // A registered sink observes the entry live, the moment it is recorded, before control returns
  // to the engine. The append-only log is kept regardless, so draining still returns every entry.
  if (sink) {
    sink(entry);
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
