#pragma once

#include <iostream>
#include <string>
#include <unordered_set>
#include <sstream>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <fcntl.h>

#include "macros.h"

#include "logging.h"

namespace Common {
  struct SocketCfg {
    std::string ip_;
    std::string iface_;
    int port_ = -1;
    bool is_udp_ = false;
    bool is_listening_ = false;
    bool needs_so_timestamp_ =  false;

    auto toString() const {
      std::stringstream ss;
      ss << "SocketCfg[ip:" << ip_
      << " iface:" << iface_
      << " port:" << port_
      << " is_udp:" << is_udp_
      << " is_listening:" << is_listening_
      << " needs_SO_timestamp:" << needs_so_timestamp_
      << "]";

      return ss.str();
    }
  };

  /// Represents the maximum number of pending / unaccepted TCP connections.
  constexpr int MaxTCPServerBacklog = 1024;

  /// Convert interface name "eth0" to ip "123.123.123.123".
  inline auto getIfaceIP(const std::string &iface) -> std::string {
    char buf[NI_MAXHOST] = {'\0'};
    ifaddrs *ifaddr = nullptr;

    if (getifaddrs(&ifaddr) != -1) {
      for (ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET && iface == ifa->ifa_name) {
          getnameinfo(ifa->ifa_addr, sizeof(sockaddr_in), buf, sizeof(buf), NULL, 0, NI_NUMERICHOST);
          break;
        }
      }
      freeifaddrs(ifaddr);
    }

    return buf;
  }

  /// Sockets will not block on read, but instead return immediately if data is not available.
  inline auto setNonBlocking(int fd) -> bool {
    const auto flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
      return false;
    if (flags & O_NONBLOCK)
      return true;
    return (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1);
  }

  /// Disable Nagle's algorithm and associated delays.
  inline auto disableNagle(int fd) -> bool {
    int one = 1;
    return (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<void *>(&one), sizeof(one)) != -1);
  }

  /// Allow software receive timestamps on incoming packets.
  inline auto setSOTimestamp(int fd) -> bool {
    int one = 1;
    return (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMP, reinterpret_cast<void *>(&one), sizeof(one)) != -1);
  }

  /// Add / Join membership / subscription to the multicast stream specified and on the interface specified.
  inline auto join(int fd, const std::string &ip, const std::string &, int) -> bool {
    const ip_mreq mreq{{inet_addr(ip.c_str())}, {htonl(INADDR_ANY)}};
    return (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != -1);
  }

  /// Create a TCP / UDP socket to either connect to or listen for data on or listen for connections on the specified interface and IP:port information.
  [[nodiscard]] inline auto createSocket(Logger &logger, const SocketCfg& socket_cfg) -> int {
    std::string time_str;

    const auto ip = socket_cfg.ip_.empty() ? getIfaceIP(socket_cfg.iface_) : socket_cfg.ip_;
    logger.log("%:% %() % cfg:%\n", __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str), socket_cfg.toString());

    const int input_flags = (socket_cfg.is_listening_ ? AI_PASSIVE : 0) | (std::isdigit(ip.c_str()[0]) ? AI_NUMERICHOST : 0) | AI_NUMERICSERV;
    const addrinfo hints{input_flags, AF_INET, socket_cfg.is_udp_ ? SOCK_DGRAM : SOCK_STREAM,
                         socket_cfg.is_udp_ ? IPPROTO_UDP : IPPROTO_TCP, 0, 0, nullptr, nullptr};

    addrinfo *result = nullptr;
    const auto rc = getaddrinfo(ip.c_str(), std::to_string(socket_cfg.port_).c_str(), &hints, &result);
    ASSERT(!rc, "getaddrinfo() failed. error:" + std::string(gai_strerror(rc)) + "errno:" + strerror(errno));

    int fd = -1;
    int one = 1;
    for (addrinfo *rp = result; rp; rp = rp->ai_next) {
      fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      ASSERT(fd != -1, "socket() failed. errno:" + std::string(strerror(errno)));

      ASSERT(setNonBlocking(fd), "setNonBlocking() failed. errno:" + std::string(strerror(errno)));

      if (!socket_cfg.is_udp_ && !disableNagle(fd)) { // disable Nagle for TCP sockets.
        logger.log("setNoDelay() failed. errno:%\n", strerror(errno));
        return -1;
      }

      if (!socket_cfg.is_listening_ && connect(fd, rp->ai_addr, rp->ai_addrlen) == 1) { // establish connection to specified address.
        logger.log("connect() failed. errno:%\n", strerror(errno));
        return -1;
      }
      if (socket_cfg.is_listening_ && setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof(one)) == -1) { // allow re-using the address in the call to bind()
        logger.log("setsockopt() SO_REUSEADDR failed. errno:%\n", strerror(errno));
        return -1;
      }
      if (socket_cfg.is_listening_) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(socket_cfg.port_);

        // bind to the specified port number.
        if (socket_cfg.is_udp_ && bind(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
          logger.log("bind() failed. errno:%\n", strerror(errno));
          return -1;
        } else if (!socket_cfg.is_udp_ && bind(fd, rp->ai_addr, rp->ai_addrlen) == -1) {
          logger.log("bind() failed. errno:%\n", strerror(errno));
          return -1;
        }
      }

      if (!socket_cfg.is_udp_ && socket_cfg.is_listening_ && listen(fd, MaxTCPServerBacklog) == -1) { // listen for incoming TCP connections.
        logger.log("listen() failed. errno:%\n", strerror(errno));
        return -1;
      }

      if (socket_cfg.needs_so_timestamp_ && !setSOTimestamp(fd)) { // enable software receive timestamps.
        logger.log("setSOTimestamp() failed. errno:%\n", strerror(errno));
        return -1;
      }
    }

    if (result)
      freeaddrinfo(result);

    return fd;
  }
}
