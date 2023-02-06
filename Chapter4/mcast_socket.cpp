#include "common/mcast_socket.h"

namespace Common {
  auto McastSocket::init(const std::string &ip, const std::string &iface, int port, bool is_listening) noexcept -> int {
    destroy();
    fd_ = createSocket(logger_, ip, iface, port, true, false, is_listening, 32);
    return fd_;
  }

  void McastSocket::destroy() {
    close(fd_);
    fd_ = -1;
  }

  bool McastSocket::join(const std::string &ip, const std::string &iface, int port) {
    // TODO: After IGMP-join finishes need to update poll-fd list.
    return Common::join(fd_, ip, iface, port);
  }

  void McastSocket::leave(const std::string &, int) {
    // TODO: Remove from poll-fd list.
    destroy();
  }

  bool McastSocket::sendAndRecv() {
    const ssize_t n_rcv = recv(fd_, rcv_buffer_ + next_rcv_valid_index_, McastBufferSize - next_rcv_valid_index_, MSG_DONTWAIT);
    if (n_rcv > 0) {
      next_rcv_valid_index_ += n_rcv;
      logger_.log("%:% %() read socket:% len:%\n", __FILE__, __LINE__, __FUNCTION__, fd_, next_rcv_valid_index_);
      recv_callback_(this);
    }

    ssize_t n_send = std::min(McastBufferSize, next_send_valid_index_);
    while (n_send > 0) {
      ssize_t n_send_this_msg = std::min(static_cast<ssize_t>(next_send_valid_index_), n_send);
      const int flags = MSG_DONTWAIT | MSG_NOSIGNAL | (n_send_this_msg < n_send ? MSG_MORE : 0);
      ssize_t n = ::send(fd_, send_buffer_, n_send_this_msg, flags);
      if (n < 0) {
        if (!wouldBlock())
          send_disconnected_ = true;
        break;
      }

      logger_.log("%:% %() send socket:% len:%\n", __FILE__, __LINE__, __FUNCTION__, fd_, n);

      n_send -= n;
      ASSERT(n == n_send_this_msg, "Don't support partial send lengths yet.");
    }
    next_send_valid_index_ = 0;

    return (n_rcv > 0);
  }

  void McastSocket::send(const void *data, size_t len) {
    if (len > 0) {
      memcpy(send_buffer_ + next_send_valid_index_, data, len);
      next_send_valid_index_ += len;
    }
  }
}
