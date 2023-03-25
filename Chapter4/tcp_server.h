#pragma once

#include "tcp_socket.h"

namespace Common {
  struct TCPServer {
    auto defaultRecvCallback(TCPSocket *socket, Nanos rx_time) noexcept {
      logger_.log("%:% %() % TCPServer::defaultRecvCallback() socket:% len:% rx:%\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), socket->fd_, socket->next_rcv_valid_index_, rx_time);
    }

    auto defaultRecvFinishedCallback() noexcept {
      logger_.log("%:% %() % TCPServer::defaultRecvFinishedCallback()\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
    }

    explicit TCPServer(Logger &logger)
        : listener_socket_(logger), logger_(logger) {
      recv_callback_ = [this](auto socket, auto rx_time) { defaultRecvCallback(socket, rx_time); };
      recv_finished_callback_ = [this]() { defaultRecvFinishedCallback(); };
    }

    auto listen(const std::string &iface, int port) -> void;

    auto destroy();

    auto poll() noexcept -> void;

    auto sendAndRecv() noexcept -> void;

  private:
    auto epoll_add(TCPSocket *socket);

    auto epoll_del(TCPSocket *socket);

    auto del(TCPSocket *socket);

    auto send(TCPSocket *socket);

  public:
    int efd_ = -1;
    TCPSocket listener_socket_;

    epoll_event events_[1024];
    std::vector<TCPSocket *> sockets_, receive_sockets_, send_sockets_, disconnected_sockets_;

    std::function<void(TCPSocket *s, Nanos rx_time)> recv_callback_;
    std::function<void()> recv_finished_callback_;

    std::string time_str_;
    Logger &logger_;
  };
}
