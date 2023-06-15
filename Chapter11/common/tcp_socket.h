#pragma once

#include <functional>

#include "socket_utils.h"
#include "logging.h"

namespace Common {
  /// Size of our send and receive buffers in bytes.
  constexpr size_t TCPBufferSize = 64 * 1024 * 1024;

  struct TCPSocket {
    /// Default callback to be used to receive and process data.
    auto defaultRecvCallback(TCPSocket *socket, Nanos rx_time) noexcept {
      logger_.log("%:% %() % TCPSocket::defaultRecvCallback() socket:% len:% rx:%\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), socket->fd_, socket->next_rcv_valid_index_, rx_time);
    }

    explicit TCPSocket(Logger &logger)
        : logger_(logger) {
      send_buffer_ = new char[TCPBufferSize];
      rcv_buffer_ = new char[TCPBufferSize];
      recv_callback_ = [this](auto socket, auto rx_time) { defaultRecvCallback(socket, rx_time); };
    }

    auto destroy() -> void;

    ~TCPSocket() {
      destroy();
      delete[] send_buffer_;
      send_buffer_ = nullptr;
      delete[] rcv_buffer_;
      rcv_buffer_ = nullptr;
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

    int fd_ = -1;

    /// Send and receive buffers and trackers for read/write indices.
    char *send_buffer_ = nullptr;
    size_t next_send_valid_index_ = 0;
    char *rcv_buffer_ = nullptr;
    size_t next_rcv_valid_index_ = 0;

    /// To track the state of outgoing or incoming connections.
    bool send_disconnected_ = false;
    bool recv_disconnected_ = false;

    /// Socket attributes.
    struct sockaddr_in inInAddr;

    /// Function wrapper to callback when there is data to be processed.
    std::function<void(TCPSocket *s, Nanos rx_time)> recv_callback_;

    std::string time_str_;
    Logger &logger_;
  };
}
