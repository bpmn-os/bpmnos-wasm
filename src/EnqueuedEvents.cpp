#include "EnqueuedEvents.h"

#include <utility>

namespace BPMNOS::WASM {

void EnqueuedEvents::enqueue(std::shared_ptr<Execution::Event> event) {
  queue.push_back(std::move(event));
}

std::shared_ptr<Execution::Event> EnqueuedEvents::dispatchEvent([[maybe_unused]] const Execution::SystemState* systemState) {
  // An event is built when it is queued, against the request or the message it answers, and the engine
  // requires a dispatched event to be live. What has expired since is therefore void and dropped, which is
  // right for a decision whose request was withdrawn and for a replayed decision that no longer fits alike.
  while (!queue.empty()) {
    auto event = std::move(queue.front());
    queue.pop_front();
    if (!event->expired()) {
      return event;
    }
  }
  return nullptr;
}

} // namespace BPMNOS::WASM
