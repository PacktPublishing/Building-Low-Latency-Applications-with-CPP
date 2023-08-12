#pragma once

#include "tcp_socket.h"

namespace Common {
  struct TCPServer {
    explicit TCPServer(Logger &logger)
        : listener_socket_(logger), logger_(logger) {
    }

    /// Start listening for connections on the provided interface and port.
    auto listen(const std::string &iface, int port) -> void;

    auto destroy();

    /// Check for new connections or dead connections and update containers that track the sockets.
    auto poll() noexcept -> void;

    /// Publish outgoing data from the send buffer and read incoming data from the receive buffer.
    auto sendAndRecv() noexcept -> void;

  private:
    /// Add and remove socket file descriptors to and from the EPOLL list.
    auto epoll_add(TCPSocket *socket);

    auto epoll_del(TCPSocket *socket);

    auto del(TCPSocket *socket);

  public:
    /// Socket on which this server is listening for new connections on.
    int efd_ = -1;
    TCPSocket listener_socket_;

    epoll_event events_[1024];

    /// Collection of all sockets, sockets for incoming data, sockets for outgoing data and dead connections.
    std::vector<TCPSocket *> sockets_, receive_sockets_, send_sockets_, disconnected_sockets_;

    /// Function wrapper to call back when data is available.
    std::function<void(TCPSocket *s, Nanos rx_time)> recv_callback_;
    /// Function wrapper to call back when all data across all TCPSockets has been read and dispatched this round.
    std::function<void()> recv_finished_callback_;

    std::string time_str_;
    Logger &logger_;
  };
}
