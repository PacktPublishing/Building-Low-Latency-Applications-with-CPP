#pragma once

#include <functional>

#include "common/socket_utils.h"

#include "common/logging.h"

namespace Common {
  constexpr size_t McastBufferSize = 64 * 1024 * 1024;

  struct McastSocket {
    McastSocket(Logger& logger)
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

    auto init(const std::string &ip, const std::string &iface, int port, bool is_listening) noexcept -> int;

    void destroy();

    bool join(const std::string &ip, const std::string &iface, int port);

    void leave(const std::string &ip, int port);

    bool sendAndRecv();

    void send(const void *data, size_t len);

    void defaultRecvCallback(McastSocket *socket) {
      logger_.log("%:% %() McastSocket::defaultRecvCallback() socket:% len:%\n", __FILE__, __LINE__, __FUNCTION__, socket->fd_,
                                     socket->next_rcv_valid_index_);
    }

    int fd_ = -1;
    bool send_disconnected_ = false;
    bool recv_disconnected_ = false;

    char *send_buffer_ = nullptr;
    size_t next_send_valid_index_ = 0;
    char *rcv_buffer_ = nullptr;
    size_t next_rcv_valid_index_ = 0;

    std::function<void(McastSocket *s)> recv_callback_;

    Logger& logger_;
  };
}
