#include "tcp_server.h"

namespace Common {
  /// Add and remove socket file descriptors to and from the EPOLL list.
  auto TCPServer::addToEpollList(TCPSocket *socket) {
    epoll_event ev{EPOLLET | EPOLLIN, {reinterpret_cast<void *>(socket)}};
    return !epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, socket->socket_fd_, &ev);
  }

  /// Start listening for connections on the provided interface and port.
  auto TCPServer::listen(const std::string &iface, int port) -> void {
    epoll_fd_ = epoll_create(1);
    ASSERT(epoll_fd_ >= 0, "epoll_create() failed error:" + std::string(std::strerror(errno)));

    ASSERT(listener_socket_.connect("", iface, port, true) >= 0,
           "Listener socket failed to connect. iface:" + iface + " port:" + std::to_string(port) + " error:" +
           std::string(std::strerror(errno)));

    ASSERT(addToEpollList(&listener_socket_), "epoll_ctl() failed. error:" + std::string(std::strerror(errno)));
  }

  /// Publish outgoing data from the send buffer and read incoming data from the receive buffer.
  auto TCPServer::sendAndRecv() noexcept -> void {
    auto recv = false;

    std::for_each(receive_sockets_.begin(), receive_sockets_.end(), [&recv](auto socket) {
      recv |= socket->sendAndRecv();
    });

    if (recv) // There were some events and they have all been dispatched, inform listener.
      recv_finished_callback_();

    std::for_each(send_sockets_.begin(), send_sockets_.end(), [](auto socket) {
      socket->sendAndRecv();
    });
  }

  /// Check for new connections or dead connections and update containers that track the sockets.
  auto TCPServer::poll() noexcept -> void {
    const int max_events = 1 + send_sockets_.size() + receive_sockets_.size();

    const int n = epoll_wait(epoll_fd_, events_, max_events, 0);
    bool have_new_connection = false;
    for (int i = 0; i < n; ++i) {
      const auto &event = events_[i];
      auto socket = reinterpret_cast<TCPSocket *>(event.data.ptr);

      // Check for new connections.
      if (event.events & EPOLLIN) {
        if (socket == &listener_socket_) {
          logger_.log("%:% %() % EPOLLIN listener_socket:%\n", __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), socket->socket_fd_);
          have_new_connection = true;
          continue;
        }
        logger_.log("%:% %() % EPOLLIN socket:%\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), socket->socket_fd_);
        if (std::find(receive_sockets_.begin(), receive_sockets_.end(), socket) == receive_sockets_.end())
          receive_sockets_.push_back(socket);
      }

      if (event.events & EPOLLOUT) {
        logger_.log("%:% %() % EPOLLOUT socket:%\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), socket->socket_fd_);
        if (std::find(send_sockets_.begin(), send_sockets_.end(), socket) == send_sockets_.end())
          send_sockets_.push_back(socket);
      }

      if (event.events & (EPOLLERR | EPOLLHUP)) {
        logger_.log("%:% %() % EPOLLERR socket:%\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), socket->socket_fd_);
        if (std::find(receive_sockets_.begin(), receive_sockets_.end(), socket) == receive_sockets_.end())
          receive_sockets_.push_back(socket);
      }
    }

    // Accept a new connection, create a TCPSocket and add it to our containers.
    while (have_new_connection) {
      logger_.log("%:% %() % have_new_connection\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_));
      sockaddr_storage addr;
      socklen_t addr_len = sizeof(addr);
      int fd = accept(listener_socket_.socket_fd_, reinterpret_cast<sockaddr *>(&addr), &addr_len);
      if (fd == -1)
        break;

      ASSERT(setNonBlocking(fd) && disableNagle(fd),
             "Failed to set non-blocking or no-delay on socket:" + std::to_string(fd));

      logger_.log("%:% %() % accepted socket:%\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), fd);

      auto socket = new TCPSocket(logger_);
      socket->socket_fd_ = fd;
      socket->recv_callback_ = recv_callback_;
      ASSERT(addToEpollList(socket), "Unable to add socket. error:" + std::string(std::strerror(errno)));

      if (std::find(receive_sockets_.begin(), receive_sockets_.end(), socket) == receive_sockets_.end())
        receive_sockets_.push_back(socket);
    }
  }
}
