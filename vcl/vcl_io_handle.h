#pragma once

#include <list>

#include "envoy/api/io_error.h"
#include "envoy/network/io_handle.h"

#include "common/common/logger.h"
#include "common/network/io_socket_error_impl.h"

#include "vpp/include/vcl/vppcom.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

using namespace Envoy::Network;

#define VCL_INVALID_SH uint32_t(~0)
#define VCL_SH_VALID(_sh) (_sh != static_cast<uint32_t>(~0))
#define VCL_SET_SH_INVALID(_sh) (_sh = static_cast<uint32_t>(~0))

int peekVclSession(vcl_session_handle_t sh, vppcom_endpt_t* ep, uint32_t* proto);
Envoy::Network::Address::InstanceConstSharedPtr vclEndptToAddress(const vppcom_endpt_t& endpt,
                                                                  uint32_t sh);

class VclIoHandle : public Envoy::Network::IoHandle, Logger::Loggable<Logger::Id::connection> {
public:
  explicit VclIoHandle(os_fd_t fd = INVALID_SOCKET) : sh_(fd) {
    fprintf(stderr, "copyconstructor?\n");
  }

  VclIoHandle(uint32_t sh, os_fd_t fd) : sh_(sh), fd_(fd) { (void)fd_; }

  ~VclIoHandle() override;

  os_fd_t fdDoNotUse() const override { return 1 << 23; }

  uint32_t sh() const { return sh_; }
  bool isListener() const { return is_listener_; }
  void setListener(bool is_listener) { is_listener_ = is_listener; }

  Api::IoCallUint64Result close() override;

  bool isOpen() const override;

  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;
  Api::IoCallUint64Result read(Buffer::Instance& buffer, absl::optional<uint64_t> max_length) override;
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;
  Api::IoCallUint64Result write(Buffer::Instance& buffer) override;
  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override;
  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
                                  const Envoy::Network::Address::Ip* self_ip,
                                  const Envoy::Network::Address::Instance& peer_address) override;
  Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                  uint32_t self_port, RecvMsgOutput& output) override;
  Api::IoCallUint64Result recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                   RecvMsgOutput& output) override;
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() override;

  bool supportsMmsg() const override;
  bool supportsUdpGro() const override { return false; }

  Api::SysCallIntResult bind(Envoy::Network::Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult listen(int backlog) override;
  Envoy::Network::IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;
  Api::SysCallIntResult connect(Envoy::Network::Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult setOption(int level, int optname, const void* optval,
                                  socklen_t optlen) override;
  Api::SysCallIntResult getOption(int level, int optname, void* optval, socklen_t* optlen) override;
  Api::SysCallIntResult ioctl(unsigned long control_code, void* in_buffer,
                              unsigned long in_buffer_len, void* out_buffer,
                              unsigned long out_buffer_len, unsigned long* bytes_returned) override;
  Api::SysCallIntResult setBlocking(bool blocking) override;
  absl::optional<int> domain() override;
  Envoy::Network::Address::InstanceConstSharedPtr localAddress() override;
  Envoy::Network::Address::InstanceConstSharedPtr peerAddress() override;
  Api::SysCallIntResult shutdown(int) override { return {0, 0}; }

  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;
  void activateFileEvents(uint32_t events) override { file_event_->activate(events); }
  void enableFileEvents(uint32_t events) override { file_event_->setEnabled(events); }
  void resetFileEvents() override { file_event_.reset(); }

  void cb(uint32_t events) { cb_(events); }
  void setCb(Event::FileReadyCb cb) { cb_ = cb; }
  void updateEvents(uint32_t events);

  IoHandlePtr duplicate() override;

  bool no_sh_ = false;

private:
  uint32_t sh_{VCL_INVALID_SH};
  os_fd_t fd_{~0};
  Event::FileEventPtr file_event_{nullptr};
  bool is_listener_ = false;

  // Converts a VCL return types to IoCallUint64Result.
  Api::IoCallUint64Result vclCallResultToIoCallResult(const int32_t result) {
    if (result >= 0) {
      // Return nullptr as IoError upon success.
      return Api::IoCallUint64Result(
          result, Api::IoErrorPtr(nullptr, Envoy::Network::IoSocketError::deleteIoError));
    }
    RELEASE_ASSERT(result != VPPCOM_EINVAL, "Invalid argument passed in.");
    return Api::IoCallUint64Result(
        /*rc=*/0, (result == VPPCOM_EAGAIN
                       // EAGAIN is frequent enough that its memory allocation should be avoided.
                       ? Api::IoErrorPtr(Envoy::Network::IoSocketError::getIoSocketEagainInstance(),
                                         Envoy::Network::IoSocketError::deleteIoError)
                       : Api::IoErrorPtr(new Envoy::Network::IoSocketError(-result),
                                         Envoy::Network::IoSocketError::deleteIoError)));
  }
  Event::FileReadyCb cb_;
};

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy