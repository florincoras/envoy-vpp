#include "vcl/vcl_event.h"

#include "common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

VclEvent::VclEvent(Dispatcher& dispatcher, VclIoHandle& io_handle, FileReadyCb cb)
    : cb_(cb), io_handle_(io_handle) {
  activation_cb_ = dispatcher.createSchedulableCallback([this]() {
    ASSERT(injected_activation_events_ != 0);
    mergeInjectedEventsAndRunCb(0);
  });
}

VclEvent::~VclEvent() {}

void VclEvent::activate(uint32_t events) {
  // events is not empty.
  ASSERT(events != 0);
  // Only supported event types are set.
  ASSERT((events & (FileReadyType::Read | FileReadyType::Write | FileReadyType::Closed)) == events);

  cb_(events);

  // Schedule the activation callback so it runs as part of the next loop iteration if it is not
  // already scheduled.
  if (injected_activation_events_ == 0) {
    ASSERT(!activation_cb_->enabled());
    activation_cb_->scheduleCallbackNextIteration();
  }
  ASSERT(activation_cb_->enabled());

  // Merge new events with pending injected events.
  injected_activation_events_ |= events;
}

void VclEvent::setEnabled(uint32_t events) { io_handle_.updateEvents(events); }

void VclEvent::mergeInjectedEventsAndRunCb(uint32_t events) {
  if (injected_activation_events_ != 0) {
    events |= injected_activation_events_;
    injected_activation_events_ = 0;
    activation_cb_->cancel();
  }
  cb_(events);
}

void VclEvent::unregisterEventIfEmulatedEdge(uint32_t) {}

void VclEvent::registerEventIfEmulatedEdge(uint32_t) {}

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy