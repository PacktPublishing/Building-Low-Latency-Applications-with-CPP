#include "mcast_socket.h"

namespace Common {
  /// Initialize multicast socket to read from or publish to a stream.
  /// Does not join the multicast stream yet.
  auto McastSocket::init(const std::string &ip, const std::string &iface, int port, bool is_listening) -> int {
    const SocketCfg socket_cfg{ip, iface, port, true, is_listening, false};
    socket_fd_ = createSocket(logger_, socket_cfg);
    return socket_fd_;
  }

  /// Add / Join membership / subscription to a multicast stream.
  bool McastSocket::join(const std::string &ip) {
    return Common::join(socket_fd_, ip);
  }

  /// Remove / Leave membership / subscription to a multicast stream.
  auto McastSocket::leave(const std::string &, int) -> void {
    close(socket_fd_);
    socket_fd_ = -1;
  }

  /// Publish outgoing data and read incoming data.
  auto McastSocket::sendAndRecv() noexcept -> bool {
    // Read data and dispatch callbacks if data is available - non blocking.
    const ssize_t n_rcv = recv(socket_fd_, inbound_data_.data() + next_rcv_valid_index_, McastBufferSize - next_rcv_valid_index_, MSG_DONTWAIT);
    if (n_rcv > 0) {
      next_rcv_valid_index_ += n_rcv;
      logger_.log("%:% %() % read socket:% len:%\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), socket_fd_,
                  next_rcv_valid_index_);
      recv_callback_(this);
    }

    // Publish market data in the send buffer to the multicast stream.
    if (next_send_valid_index_ > 0) {
      ssize_t n = ::send(socket_fd_, outbound_data_.data(), next_send_valid_index_, MSG_DONTWAIT | MSG_NOSIGNAL);

      logger_.log("%:% %() % send socket:% len:%\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), socket_fd_, n);
    }
    next_send_valid_index_ = 0;

    return (n_rcv > 0);
  }

  /// Copy data to send buffers - does not send them out yet.
  auto McastSocket::send(const void *data, size_t len) noexcept -> void {
    memcpy(outbound_data_.data() + next_send_valid_index_, data, len);
    next_send_valid_index_ += len;
    ASSERT(next_send_valid_index_ < McastBufferSize, "Mcast socket buffer filled up and sendAndRecv() not called.");
  }
}
