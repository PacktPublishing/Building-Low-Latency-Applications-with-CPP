#pragma once

#include <functional>

#include "socket_utils.h"
#include "logging.h"

namespace Common {
  /// Size of our send and receive buffers in bytes.
  constexpr size_t TCPBufferSize = 64 * 1024 * 1024;

  struct TCPSocket {
    explicit TCPSocket(Logger &logger)
        : logger_(logger) {
      outbound_data_.resize(TCPBufferSize);
      inbound_data_.resize(TCPBufferSize);
    }

    /// Create TCPSocket with provided attributes to either listen-on / connect-to.
    auto connect(const std::string &ip, const std::string &iface, int port, bool is_listening) -> int;

    /// Called to publish outgoing data from the buffers as well as check for and callback if data is available in the read buffers.
    auto sendAndRecv() noexcept -> bool;

    /// Write outgoing data to the send buffers.
    auto send(const void *data, size_t len) noexcept -> void;

    /// Deleted default, copy & move constructors and assignment-operators.
    TCPSocket() = delete;

    TCPSocket(const TCPSocket &) = delete;

    TCPSocket(const TCPSocket &&) = delete;

    TCPSocket &operator=(const TCPSocket &) = delete;

    TCPSocket &operator=(const TCPSocket &&) = delete;

    /// File descriptor for the socket.
    int socket_fd_ = -1;

    /// Send and receive buffers and trackers for read/write indices.
    std::vector<char> outbound_data_;
    size_t next_send_valid_index_ = 0;
    std::vector<char> inbound_data_;
    size_t next_rcv_valid_index_ = 0;

    /// Socket attributes.
    struct sockaddr_in socket_attrib_{};

    /// Function wrapper to callback when there is data to be processed.
    std::function<void(TCPSocket *s, Nanos rx_time)> recv_callback_ = nullptr;

    std::string time_str_;
    Logger &logger_;
  };
}
