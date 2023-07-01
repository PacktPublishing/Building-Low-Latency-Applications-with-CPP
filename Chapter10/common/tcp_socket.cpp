#include "tcp_socket.h"

namespace Common {
  /// Create TCPSocket with provided attributes to either listen-on / connect-to.
  auto TCPSocket::connect(const std::string &ip, const std::string &iface, int port, bool is_listening) -> int {
    destroy();
    // Note that needs_so_timestamp=true for FIFOSequencer.
    fd_ = createSocket(logger_, ip, iface, port, false, false, is_listening, 0, true);

    inInAddr.sin_addr.s_addr = INADDR_ANY;
    inInAddr.sin_port = htons(port);
    inInAddr.sin_family = AF_INET;

    return fd_;
  }

  auto TCPSocket::destroy() -> void {
    close(fd_);
    fd_ = -1;
  }

  /// Called to publish outgoing data from the buffers as well as check for and callback if data is available in the read buffers.
  auto TCPSocket::sendAndRecv() noexcept -> bool {
    char ctrl[CMSG_SPACE(sizeof(struct timeval))];
    struct cmsghdr *cmsg = (struct cmsghdr *) &ctrl;

    struct iovec iov;
    iov.iov_base = rcv_buffer_ + next_rcv_valid_index_;
    iov.iov_len = TCPBufferSize - next_rcv_valid_index_;

    msghdr msg;
    msg.msg_control = ctrl;
    msg.msg_controllen = sizeof(ctrl);
    msg.msg_name = &inInAddr;
    msg.msg_namelen = sizeof(inInAddr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    // Non-blocking call to read available data.
    const auto n_rcv = recvmsg(fd_, &msg, MSG_DONTWAIT);
    if (n_rcv > 0) {
      next_rcv_valid_index_ += n_rcv;

      Nanos kernel_time = 0;
      struct timeval time_kernel;
      if (cmsg->cmsg_level == SOL_SOCKET &&
          cmsg->cmsg_type == SCM_TIMESTAMP &&
          cmsg->cmsg_len == CMSG_LEN(sizeof(time_kernel))) {
        memcpy(&time_kernel, CMSG_DATA(cmsg), sizeof(time_kernel));
        kernel_time = time_kernel.tv_sec * NANOS_TO_SECS + time_kernel.tv_usec * NANOS_TO_MICROS; // convert timestamp to nanoseconds.
      }

      const auto user_time = getCurrentNanos();

      logger_.log("%:% %() % read socket:% len:% utime:% ktime:% diff:%\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), fd_, next_rcv_valid_index_, user_time, kernel_time, (user_time - kernel_time));
      recv_callback_(this, kernel_time);
    }

    ssize_t n_send = std::min(TCPBufferSize, next_send_valid_index_);
    while (n_send > 0) {
      auto n_send_this_msg = std::min(static_cast<ssize_t>(next_send_valid_index_), n_send);
      const int flags = MSG_DONTWAIT | MSG_NOSIGNAL | (n_send_this_msg < n_send ? MSG_MORE : 0);

      // Non-blocking call to send data.
      auto n = ::send(fd_, send_buffer_, n_send_this_msg, flags);
      if (UNLIKELY(n < 0)) {
        if (!wouldBlock())
          send_disconnected_ = true;
        break;
      }

      logger_.log("%:% %() % send socket:% len:%\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), fd_, n);

      n_send -= n;
      ASSERT(n == n_send_this_msg, "Don't support partial send lengths yet.");
    }
    next_send_valid_index_ = 0;

    return (n_rcv > 0);
  }

  /// Write outgoing data to the send buffers.
  auto TCPSocket::send(const void *data, size_t len) noexcept -> void {
    if (len > 0) {
      memcpy(send_buffer_ + next_send_valid_index_, data, len);
      next_send_valid_index_ += len;
    }
  }
}
