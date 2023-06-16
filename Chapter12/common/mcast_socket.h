#pragma once

#include <functional>

#include "socket_utils.h"

#include "logging.h"

namespace Common {
  /// Size of send and receive buffers in bytes.
  constexpr size_t McastBufferSize = 64 * 1024 * 1024;

  struct McastSocket {
    McastSocket(Logger &logger)
        : logger_(logger) {
      send_buffer_ = new char[McastBufferSize];
      rcv_buffer_ = new char[McastBufferSize];
      recv_callback_ = [this](auto socket) { defaultRecvCallback(socket); };
    }

    ~McastSocket() {
      destroy();
      delete[] send_buffer_;
      send_buffer_ = nullptr;
      delete[] rcv_buffer_;
      rcv_buffer_ = nullptr;
    }

    /// Initialize multicast socket to read from or publish to a stream.
    /// Does not join the multicast stream yet.
    auto init(const std::string &ip, const std::string &iface, int port, bool is_listening) -> int;

    auto destroy() -> void;

    /// Add / Join membership / subscription to a multicast stream.
    auto join(const std::string &ip, const std::string &iface, int port) -> bool;

    /// Remove / Leave membership / subscription to a multicast stream.
    auto leave(const std::string &ip, int port) -> void;

    /// Publish outgoing data and read incoming data.
    auto sendAndRecv() noexcept -> bool;

    /// Copy data to send buffers - does not send them out yet.
    auto send(const void *data, size_t len) noexcept -> void;

    void defaultRecvCallback(McastSocket *socket) noexcept {
      logger_.log("%:% %() % McastSocket::defaultRecvCallback() socket:% len:%\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), socket->fd_, socket->next_rcv_valid_index_);
    }

    int fd_ = -1;
    bool send_disconnected_ = false;
    bool recv_disconnected_ = false;

    /// Send and receive buffers, typically only one or the other is needed, not both.
    char *send_buffer_ = nullptr;
    size_t next_send_valid_index_ = 0;
    char *rcv_buffer_ = nullptr;
    size_t next_rcv_valid_index_ = 0;

    /// Function wrapper for the method to call when data is read.
    std::function<void(McastSocket *s)> recv_callback_;

    std::string time_str_;
    Logger &logger_;
  };
}
