#pragma once

#include <cstdint>

#include "envoy/event/file_event.h"

#include "common/event/dispatcher_impl.h"
#include "common/event/event_impl_base.h"

#include "vcl/vcl_io_handle.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

using namespace Envoy::Event;
using namespace Envoy::Network;

class VclEvent : public FileEvent {
public:
  VclEvent(Dispatcher& dispatcher, VclIoHandle& io_handle, FileReadyCb cb);
  ~VclEvent() override;

  // Event::FileEvent
  void activate(uint32_t events) override;
  void setEnabled(uint32_t events) override;

private:
  void mergeInjectedEventsAndRunCb(uint32_t events);

  FileReadyCb cb_;
  VclIoHandle& io_handle_;

  // Injected FileReadyType events that were scheduled by recent calls to activate() and are pending
  // delivery.
  uint32_t injected_activation_events_{};
  // Used to schedule delayed event activation. Armed iff pending_activation_events_ != 0.
  SchedulableCallbackPtr activation_cb_;
  // Latched "envoy.reloadable_features.activate_fds_next_event_loop" runtime feature. If true, fd
  // events scheduled via activate are evaluated in the next iteration of the event loop after
  // polling and activating new fd events.
  const bool activate_fd_events_next_event_loop_;
};

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy