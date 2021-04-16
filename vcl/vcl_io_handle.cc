#include "vcl/vcl_io_handle.h"

#include <string.h>

#include "common/buffer/buffer_impl.h"
#include "common/network/address_impl.h"

#include "vcl/vcl_event.h"
#include "vcl/vcl_interface.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

static inline int vcl_wrk_index_or_register() {
  int wrk_index;

  if ((wrk_index = vppcom_worker_index()) == -1) {
    vcl_interface_worker_register();
    wrk_index = vppcom_worker_index();
  }
  return wrk_index;
}

int peekVclSession(vcl_session_handle_t sh, vppcom_endpt_t* ep, uint32_t* proto) {
  auto current_wrk = vppcom_worker_index();
  auto sh_wrk = vppcom_session_worker(sh);
  uint32_t eplen = sizeof(*ep);

  // should NOT be used while system is loaded
  vppcom_worker_index_set(sh_wrk);

  if (vppcom_session_attr(sh, VPPCOM_ATTR_GET_LCL_ADDR, ep, &eplen)) {
    return -1;
  }

  uint32_t buflen = sizeof(uint32_t);
  if (vppcom_session_attr(sh, VPPCOM_ATTR_GET_PROTOCOL, proto, &buflen)) {
    return -1;
  }

  vppcom_worker_index_set(current_wrk);

  return 0;
}

static void vclEndptCopy(sockaddr* addr, socklen_t* addrlen, const vppcom_endpt_t& ep) {
  if (ep.is_ip4) {
    sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(addr);
    addr4->sin_family = AF_INET;
    *addrlen = std::min(static_cast<unsigned int>(sizeof(struct sockaddr_in)), *addrlen);
    memcpy(&addr4->sin_addr, ep.ip, *addrlen);
    addr4->sin_port = ep.port;
  } else {
    sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(addr);
    addr6->sin6_family = AF_INET6;
    *addrlen = std::min(static_cast<unsigned int>(sizeof(struct sockaddr_in6)), *addrlen);
    memcpy(&addr6->sin6_addr, ep.ip, *addrlen);
    addr6->sin6_port = ep.port;
  }
}

Envoy::Network::Address::InstanceConstSharedPtr vclEndptToAddress(const vppcom_endpt_t& ep,
                                                                  uint32_t sh) {
  sockaddr_storage addr;
  int len;

  if (ep.is_ip4) {
    addr.ss_family = AF_INET;
    len = sizeof(struct sockaddr_in);
    auto in4 = reinterpret_cast<struct sockaddr_in*>(&addr);
    memcpy(&in4->sin_addr, ep.ip, len);
    in4->sin_port = ep.port;
  } else {
    addr.ss_family = AF_INET6;
    len = sizeof(struct sockaddr_in6);
    auto in6 = reinterpret_cast<struct sockaddr_in6*>(&addr);
    memcpy(&in6->sin6_addr, ep.ip, len);
    in6->sin6_port = ep.port;
  }

  try {
    // Set v6only to false so that mapped-v6 address can be normalize to v4
    // address. Though dual stack may be disabled, it's still okay to assume the
    // address is from a dual stack socket. This is because mapped-v6 address
    // must come from a dual stack socket. An actual v6 address can come from
    // both dual stack socket and v6 only socket. If |peer_addr| is an actual v6
    // address and the socket is actually v6 only, the returned address will be
    // regarded as a v6 address from dual stack socket. However, this address is not going to be
    // used to create socket. Wrong knowledge of dual stack support won't hurt.
    return Envoy::Network::Address::addressFromSockAddr(addr, len, /*v6only=*/false);
  } catch (const EnvoyException& e) {
    PANIC(fmt::format("Invalid remote address for fd: {}, error: {}", sh, e.what()));
  }
}

static void vclEndptFromAddress(vppcom_endpt_t& endpt,
                                Envoy::Network::Address::InstanceConstSharedPtr address) {
  endpt.is_cut_thru = 0;
  if (address->ip()->version() == Envoy::Network::Address::IpVersion::v4) {
    const sockaddr_in* in = reinterpret_cast<const sockaddr_in*>(address->sockAddr());
    endpt.is_ip4 = 1;
    endpt.ip = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&in->sin_addr));
    endpt.port = static_cast<uint16_t>(in->sin_port);
  } else {
    const sockaddr_in6* in6 = reinterpret_cast<const sockaddr_in6*>(address->sockAddr());
    endpt.is_ip4 = 0;
    endpt.ip = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&in6->sin6_addr));
    endpt.port = static_cast<uint16_t>(in6->sin6_port);
  }
}

VclIoHandle::~VclIoHandle() {
  if (VCL_SH_VALID(sh_)) {
    VclIoHandle::close();
  }
}

Api::IoCallUint64Result VclIoHandle::close() {
  VCL_LOG("closing sh %x", sh_);
  RELEASE_ASSERT(VCL_SH_VALID(sh_), "sh must be valid");
  const int rc = vppcom_session_close(sh_);
  VCL_SET_SH_INVALID(sh_);
  return Api::IoCallUint64Result(
      rc, Api::IoErrorPtr(nullptr, Envoy::Network::IoSocketError::deleteIoError));
}

bool VclIoHandle::isOpen() const { return VCL_SH_VALID(sh_); }

Api::IoCallUint64Result VclIoHandle::readv(uint64_t max_length, Buffer::RawSlice* slices,
                                           uint64_t num_slice) {
  if (!VCL_SH_VALID(sh_)) {
    return vclCallResultToIoCallResult(VPPCOM_EBADFD);
  }

  VCL_LOG("reading on sh %x", sh_);

  int32_t result = 0, rv = 0, num_bytes_read = 0;
  size_t slice_length;

  for (uint64_t i = 0; i < num_slice; i++) {
    slice_length = std::min(slices[i].len_, static_cast<size_t>(max_length - num_bytes_read));
    rv = vppcom_session_read(sh_, slices[i].mem_, slice_length);
    if (rv < 0) {
      break;
    }
    num_bytes_read += rv;
    if (uint64_t(num_bytes_read) == max_length) {
      break;
    }
  }
  result = (num_bytes_read == 0) ? rv : num_bytes_read;
  VCL_LOG("done reading on sh %x bytes %d result %d", sh_, num_bytes_read, result);
  return vclCallResultToIoCallResult(result);
}

Api::IoCallUint64Result VclIoHandle::read(Buffer::Instance& buffer, absl::optional<uint64_t> ) {
  vppcom_data_segment_t ds[16];
  int32_t rv;

  rv = vppcom_session_read_segments(sh_, ds, 16, ~0);
  if (rv < 0) {
    return vclCallResultToIoCallResult(rv);
  }

  uint32_t ds_index = 0, sh = sh_, len;
  int32_t n_bytes = 0;
  while (n_bytes < rv) {
    len = ds[ds_index].len;
    auto fragment = new Envoy::Buffer::BufferFragmentImpl(
        ds[ds_index].data, len,
        [&, sh, len](const void*, size_t, const Envoy::Buffer::BufferFragmentImpl* this_fragment) {
          vppcom_session_free_segments(sh, len);
          delete this_fragment;
        });

    buffer.addBufferFragment(*fragment);
    n_bytes += len;
    ds_index += 1;
  }

  return vclCallResultToIoCallResult(rv);
}

Api::IoCallUint64Result VclIoHandle::writev(const Buffer::RawSlice* slices, uint64_t num_slice) {
  if (!VCL_SH_VALID(sh_)) {
    return vclCallResultToIoCallResult(VPPCOM_EBADFD);
  }

  VCL_LOG("writing on sh %x", sh_);

  uint64_t num_bytes_written = 0;
  int32_t result = 0, rv = 0;

  for (uint64_t i = 0; i < num_slice; i++) {
    rv = vppcom_session_write(sh_, slices[i].mem_, slices[i].len_);
    if (rv < 0) {
      break;
    }
    num_bytes_written += rv;
  }
  result = (num_bytes_written == 0) ? rv : num_bytes_written;

  return vclCallResultToIoCallResult(result);
}

Api::IoCallUint64Result VclIoHandle::write(Buffer::Instance& buffer) {
  constexpr uint64_t MaxSlices = 16;
  Buffer::RawSliceVector slices = buffer.getRawSlices(MaxSlices);
  Api::IoCallUint64Result result = writev(slices.begin(), slices.size());
  if (result.ok() && result.rc_ > 0) {
    buffer.drain(static_cast<uint64_t>(result.rc_));
  }
  return result;
}

Api::IoCallUint64Result VclIoHandle::recv(void* buffer, size_t length, int flags) {
  VCL_LOG("recv on sh %x", sh_);
  auto rv = vppcom_session_recvfrom(sh_, buffer, length, flags, 0);
  return vclCallResultToIoCallResult(rv);
}

Api::IoCallUint64Result VclIoHandle::sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice,
                                             int, const Envoy::Network::Address::Ip*,
                                             const Envoy::Network::Address::Instance&) {
  if (!VCL_SH_VALID(sh_)) {
    return vclCallResultToIoCallResult(VPPCOM_EBADFD);
  }

  absl::FixedArray<iovec> iov(num_slice);
  uint64_t num_slices_to_write = 0;
  uint64_t num_bytes_written = 0;

  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      iov[num_slices_to_write].iov_base = slices[i].mem_;
      iov[num_slices_to_write].iov_len = slices[i].len_;
      num_slices_to_write++;
    }
  }
  if (num_slices_to_write == 0) {
    return Api::ioCallUint64ResultNoError();
  }

  // VCL has no sendmsg semantics- Treat as a session write followed by a flush
  auto result = 0;
  for (uint64_t i = 0; i < num_slices_to_write; i++) {
    int n;
    if (i < (num_slices_to_write - 1)) {
      n = vppcom_session_write(sh_, iov[i].iov_base, iov[i].iov_len);
      if (n < 0) {
        result = (num_bytes_written == 0) ? n : num_bytes_written;
        break;
      }
    } else {
      // Flush after the last segment is written
      n = vppcom_session_write_msg(sh_, iov[i].iov_base, iov[i].iov_len);
      if (n < 0) {
        result = (num_bytes_written == 0) ? n : num_bytes_written;
        break;
      }
    }
    num_bytes_written += n;
  }

  return vclCallResultToIoCallResult(result);
}

Api::IoCallUint64Result VclIoHandle::recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                             uint32_t self_port, RecvMsgOutput& output) {
  if (!VCL_SH_VALID(sh_)) {
    return vclCallResultToIoCallResult(VPPCOM_EBADFD);
  }

  absl::FixedArray<iovec> iov(num_slice);
  uint64_t num_slices_for_read = 0;
  uint64_t num_bytes_recvd = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      iov[num_slices_for_read].iov_base = slices[i].mem_;
      iov[num_slices_for_read].iov_len = slices[i].len_;
      ++num_slices_for_read;
    }
  }

  // VCL has no recvmsg semantics- treat as a read into each slice, which is not
  // as cumbersome as it sounds, since VCL will simply copy from shared mem buffers
  // if the data is available.
  uint8_t ipaddr[sizeof(absl::uint128)];
  vppcom_endpt_t endpt;
  endpt.ip = ipaddr;
  endpt.port = static_cast<uint16_t>(self_port);
  uint32_t result = 0;

  for (uint64_t i = 0; i < num_slices_for_read; i++) {
    int n;
    n = vppcom_session_recvfrom(sh_, iov[i].iov_base, iov[i].iov_len, 0, &endpt);
    if (n < 0) {
      result = (num_bytes_recvd == 0) ? n : num_bytes_recvd;
      break;
    }
    if (i == 0) {
      output.msg_[0].peer_address_ = vclEndptToAddress(endpt, sh_);
    }
    num_bytes_recvd += n;
  }

  if (result < 0) {
    return vclCallResultToIoCallResult(result);
  }

  output.dropped_packets_ = nullptr;

  return vclCallResultToIoCallResult(result);
}

Api::IoCallUint64Result VclIoHandle::recvmmsg(RawSliceArrays&, uint32_t, RecvMsgOutput&) {
  throw EnvoyException("not supported");
}

bool VclIoHandle::supportsMmsg() const { return false; }

Api::SysCallIntResult VclIoHandle::bind(Envoy::Network::Address::InstanceConstSharedPtr address) {
  if (!VCL_SH_VALID(sh_)) {
    return {-1, VPPCOM_EBADFD};
  }

  auto wrk_index = vcl_wrk_index_or_register();
  RELEASE_ASSERT(wrk_index != -1, "should be initialized");

  vppcom_endpt_t endpt;
  vclEndptFromAddress(endpt, address);
  int32_t rv = vppcom_session_bind(sh_, &endpt);
  return {rv < 0 ? -1 : 0, -rv};
}

Api::SysCallIntResult VclIoHandle::listen(int backlog) {
  auto wrk_index = vcl_wrk_index_or_register();
  RELEASE_ASSERT(wrk_index != -1, "should be initialized");

  VCL_LOG("trying to listen sh %u", sh_);
  RELEASE_ASSERT(is_listener_ == false, "");
  RELEASE_ASSERT(vppcom_session_worker(sh_) == wrk_index, "");

  is_listener_ = true;

  int32_t rv = vppcom_session_listen(sh_, backlog);

  VCL_LOG("about to call epoll ctl for sh %u wrk %u epoll_handle %u", sh_, wrk_index,
          vcl_epoll_handle(wrk_index));
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.u64 = reinterpret_cast<uint64_t>(this);
  rv = vppcom_epoll_ctl(vcl_epoll_handle(wrk_index), EPOLL_CTL_ADD, sh_, &ev);
  return {rv < 0 ? -1 : 0, -rv};
}

Envoy::Network::IoHandlePtr VclIoHandle::accept(sockaddr* addr, socklen_t* addrlen) {
  auto wrk_index = vcl_wrk_index_or_register();
  RELEASE_ASSERT(wrk_index != -1 && isListener(), "must have worker and must be listener");

  auto sh = sh_;
  if (wrk_index) {
    VCL_LOG("trying to accept fd %d sh %x", fd_, sh);
  }

  vppcom_endpt_t endpt;
  sockaddr_storage ss;
  endpt.ip = reinterpret_cast<uint8_t*>(&ss);
  auto new_sh = vppcom_session_accept(sh, &endpt, O_NONBLOCK);
  if (new_sh >= 0) {
    vclEndptCopy(addr, addrlen, endpt);
    return std::make_unique<VclIoHandle>(new_sh, 1 << 23);
  }
  return nullptr;
}

Api::SysCallIntResult
VclIoHandle::connect(Envoy::Network::Address::InstanceConstSharedPtr address) {
  if (!VCL_SH_VALID(sh_)) {
    return {-1, VPPCOM_EBADFD};
  }
  vppcom_endpt_t endpt;
  uint8_t ipaddr[sizeof(absl::uint128)];
  endpt.ip = ipaddr;
  vclEndptFromAddress(endpt, address);
  int32_t rv = vppcom_session_connect(sh_, &endpt);
  return {rv < 0 ? -1 : 0, -rv};
}

Api::SysCallIntResult VclIoHandle::setOption(int level, int optname, const void* optval,
                                             socklen_t optlen) {
  VCL_LOG("trying to set option");
  if (!VCL_SH_VALID(sh_)) {
    return {-1, VPPCOM_EBADFD};
  }
  int32_t rv = 0;

  switch (level) {
  case SOL_TCP:
    switch (optname) {
    case TCP_NODELAY:
      rv =
          vppcom_session_attr(sh_, VPPCOM_ATTR_SET_TCP_NODELAY, const_cast<void*>(optval), &optlen);
      break;
    case TCP_MAXSEG:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_TCP_USER_MSS, const_cast<void*>(optval),
                               &optlen);
      break;
    case TCP_KEEPIDLE:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_TCP_KEEPIDLE, const_cast<void*>(optval),
                               &optlen);
      break;
    case TCP_KEEPINTVL:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_TCP_KEEPINTVL, const_cast<void*>(optval),
                               &optlen);
      break;
    case TCP_CONGESTION:
    case TCP_CORK:
      /* Ignore */
      rv = 0;
      break;
    default:
      ENVOY_LOG(debug, "ERROR: setOption() SOL_TCP: sh %u optname %d unsupported!", sh_, optname);
      break;
    }
    break;
  case SOL_IPV6:
    switch (optname) {
    case IPV6_V6ONLY:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_V6ONLY, const_cast<void*>(optval), &optlen);
      break;
    default:
      ENVOY_LOG(debug, "ERROR: setOption() SOL_IPV6: sh %u optname %d unsupported!", sh_, optname);
      break;
    }
    break;
  case SOL_SOCKET:
    switch (optname) {
    case SO_KEEPALIVE:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_KEEPALIVE, const_cast<void*>(optval), &optlen);
      break;
    case SO_REUSEADDR:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_REUSEADDR, const_cast<void*>(optval), &optlen);
      break;
    case SO_BROADCAST:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_BROADCAST, const_cast<void*>(optval), &optlen);
      break;
    default:
      ENVOY_LOG(debug, "ERROR: setOption() SOL_SOCKET: sh %u optname %d unsupported!", sh_,
                optname);
      break;
    }
    break;
  default:
    break;
  }

  return {rv < 0 ? -1 : 0, -rv};
}

Api::SysCallIntResult VclIoHandle::getOption(int level, int optname, void* optval,
                                             socklen_t* optlen) {
  VCL_LOG("trying to get option\n");
  if (!VCL_SH_VALID(sh_)) {
    return {-1, VPPCOM_EBADFD};
  }
  int32_t rv = 0;

  switch (level) {
  case SOL_TCP:
    switch (optname) {
    case TCP_NODELAY:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_TCP_NODELAY, optval, optlen);
      break;
    case TCP_MAXSEG:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_TCP_USER_MSS, optval, optlen);
      break;
    case TCP_KEEPIDLE:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_TCP_KEEPIDLE, optval, optlen);
      break;
    case TCP_KEEPINTVL:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_TCP_KEEPINTVL, optval, optlen);
      break;
    case TCP_INFO:
      if (optval && optlen && (*optlen == sizeof(struct tcp_info))) {
        ENVOY_LOG(debug, "ERROR: getOption() TCP_INFO: sh %u optname %d unsupported!", sh_,
                  optname);
        memset(optval, 0, *optlen);
        rv = VPPCOM_OK;
      } else
        rv = -EFAULT;
      break;
    case TCP_CONGESTION:
      *optlen = strlen("cubic");
      strncpy(static_cast<char*>(optval), "cubic", *optlen + 1);
      rv = 0;
      break;
    default:
      ENVOY_LOG(debug, "ERROR: getOption() SOL_TCP: sh %u optname %d unsupported!", sh_, optname);
      break;
    }
    break;
  case SOL_IPV6:
    switch (optname) {
    case IPV6_V6ONLY:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_V6ONLY, optval, optlen);
      break;
    default:
      ENVOY_LOG(debug, "ERROR: getOption() SOL_IPV6: sh %u optname %d unsupported!", sh_, optname);
      break;
    }
    break;
  case SOL_SOCKET:
    switch (optname) {
    case SO_ACCEPTCONN:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_LISTEN, optval, optlen);
      break;
    case SO_KEEPALIVE:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_KEEPALIVE, optval, optlen);
      break;
    case SO_PROTOCOL:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_PROTOCOL, optval, optlen);
      *static_cast<int*>(optval) = *static_cast<int*>(optval) ? SOCK_DGRAM : SOCK_STREAM;
      break;
    case SO_SNDBUF:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_TX_FIFO_LEN, optval, optlen);
      break;
    case SO_RCVBUF:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_RX_FIFO_LEN, optval, optlen);
      break;
    case SO_REUSEADDR:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_REUSEADDR, optval, optlen);
      break;
    case SO_BROADCAST:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_BROADCAST, optval, optlen);
      break;
    case SO_ERROR:
      rv = vppcom_session_attr(sh_, VPPCOM_ATTR_GET_ERROR, optval, optlen);
      break;
    default:
      ENVOY_LOG(debug, "ERROR: getOption() SOL_SOCKET: sh %u optname %d unsupported!", sh_,
                optname);
      ;
      break;
    }
    break;
  default:
    break;
  }
  return {rv < 0 ? -1 : 0, -rv};
}

Api::SysCallIntResult VclIoHandle::ioctl(unsigned long, void*, unsigned long, void*, unsigned long,
                                         unsigned long*) {
  return {0, 0};
}

Api::SysCallIntResult VclIoHandle::setBlocking(bool) {
  uint32_t flags = O_NONBLOCK;
  uint32_t buflen = sizeof(flags);
  int32_t rv = vppcom_session_attr(sh_, VPPCOM_ATTR_SET_FLAGS, &flags, &buflen);
  return {rv < 0 ? -1 : 0, -rv};
}

absl::optional<int> VclIoHandle::domain() {
  VCL_LOG("grabbing domain sh %x", sh_);
  return {AF_INET};
};

Envoy::Network::Address::InstanceConstSharedPtr VclIoHandle::localAddress() {
  vppcom_endpt_t ep;
  uint32_t eplen = sizeof(ep);
  uint8_t addr_buf[sizeof(struct sockaddr_in6)];
  ep.ip = addr_buf;
  if (vppcom_session_attr(sh_, VPPCOM_ATTR_GET_LCL_ADDR, &ep, &eplen)) {
    return nullptr;
  }
  return vclEndptToAddress(ep, sh_);
}

Envoy::Network::Address::InstanceConstSharedPtr VclIoHandle::peerAddress() {
  VCL_LOG("grabbing peer address sh %x", sh_);
  vppcom_endpt_t ep;
  uint32_t eplen = sizeof(ep);
  uint8_t addr_buf[sizeof(struct sockaddr_in6)];
  ep.ip = addr_buf;
  if (vppcom_session_attr(sh_, VPPCOM_ATTR_GET_PEER_ADDR, &ep, &eplen)) {
    return nullptr;
  }
  return vclEndptToAddress(ep, sh_);
}

void VclIoHandle::updateEvents(uint32_t events) {

  struct epoll_event ev;
  ev.events = 0;

  if (events & Event::FileReadyType::Read) {
    ev.events |= EPOLLIN;
  }
  if (events & Event::FileReadyType::Write) {
    ev.events |= EPOLLOUT;
  }
  if (events & Event::FileReadyType::Closed) {
    ev.events |= EPOLLERR | EPOLLHUP;
  }

  auto wrk_index = vcl_wrk_index_or_register();
  ev.data.u64 = reinterpret_cast<uint64_t>(this);

  vppcom_epoll_ctl(vcl_epoll_handle(wrk_index), EPOLL_CTL_MOD, sh_, &ev);
}

void VclIoHandle::initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                                      Event::FileTriggerType, uint32_t events) {
  VCL_LOG("adding events for sh %x fd %u isListener %u", sh_, fd_, isListener());

  struct epoll_event ev;
  ev.events = 0;

  if (events & Event::FileReadyType::Read) {
    ev.events |= EPOLLIN;
  }
  if (events & Event::FileReadyType::Write) {
    ev.events |= EPOLLOUT;
  }
  if (events & Event::FileReadyType::Closed) {
    ev.events |= EPOLLERR | EPOLLHUP;
  }

  auto wrk_index = vcl_wrk_index_or_register();
  vcl_interface_register_epoll_event(dispatcher);

  cb_ = cb;
  ev.data.u64 = reinterpret_cast<uint64_t>(this);
  vppcom_epoll_ctl(vcl_epoll_handle(wrk_index), EPOLL_CTL_ADD, sh_, &ev);

  file_event_ = Event::FileEventPtr{new VclEvent(dispatcher, *this, cb)};
}

IoHandlePtr VclIoHandle::duplicate() {
  auto wrk_index = vcl_wrk_index_or_register();
  VCL_LOG("duplicate called");
  fprintf(stderr, "duplicate session %u\n", sh_);

  if (vppcom_session_worker(sh_) == wrk_index) {
    return std::unique_ptr<VclIoHandle>(this);
  }

  // Find what must be duplicated. Asssume this is ONLY called for listeners
  vppcom_endpt_t ep;
  uint8_t addr_buf[sizeof(struct sockaddr_in6)];
  ep.ip = addr_buf;
  uint32_t proto;

  if (peekVclSession(sh_, &ep, &proto)) {
    RELEASE_ASSERT(0, "");
  }

  auto address = vclEndptToAddress(ep, -1);
  auto sh = vppcom_session_create(proto, 1);
  IoHandlePtr io_handle = std::make_unique<VclIoHandle>(static_cast<uint32_t>(sh), 1 << 23);

  io_handle->bind(address);

  return io_handle;
}

absl::optional<std::chrono::milliseconds> VclIoHandle::lastRoundTripTime() { return {}; }

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy
