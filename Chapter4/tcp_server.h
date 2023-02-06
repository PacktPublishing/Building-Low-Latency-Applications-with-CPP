#pragma once

#include "common/tcp_socket.h"

namespace Common {
  struct TCPServer {
    auto defaultRecvCallback(TCPSocket *socket) noexcept {
      logger_.log("%:% %() TCPServer::defaultRecvCallback() socket:% len:%\n", __FILE__, __LINE__, __FUNCTION__, socket->fd_, socket->next_rcv_valid_index_);
    }

    explicit TCPServer(Logger& logger)
    : listener_socket_(logger), logger_(logger) {
      recv_callback_ = [this](auto socket) { defaultRecvCallback(socket); };
    }

    auto listen(const std::string &iface, int port) noexcept -> void;

    auto destroy() noexcept;

    auto epoll_add(TCPSocket *socket) noexcept;

    auto epoll_del(TCPSocket *socket) noexcept;

    auto poll() noexcept -> void;

    auto sendAndRecv() noexcept -> void;

    auto add(TCPSocket *socket) noexcept;

    auto del(TCPSocket *socket) noexcept;

    auto send(TCPSocket *socket) noexcept;

    auto disconnect(TCPSocket *socket) noexcept;

    int efd_ = -1;
    TCPSocket listener_socket_;

    epoll_event events_[1024];
    std::unordered_set<TCPSocket *> sockets_, receive_sockets_, send_sockets_, disconnected_sockets_;

    std::function<void(TCPSocket *s)> recv_callback_;

    Logger& logger_;
  };
}
