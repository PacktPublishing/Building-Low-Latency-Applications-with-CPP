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
    int ttl_ = -1;
    bool needs_so_timestamp_ =  false;

    auto toString() const {
      std::stringstream ss;
      ss << "SocketCfg[ip:" << ip_
      << " iface:" << iface_
      << " port:" << port_
      << " is_udp:" << is_udp_
      << " is_listening:" << is_listening_
      << " ttl:" << ttl_
      << " needs_SO_timestamp:" << needs_so_timestamp_
      << "]";

      return ss.str();
    }
  };

  /// Represents the maximum number of pending / unaccepted TCP connections.
  constexpr int MaxTCPServerBacklog = 1024;

  /// Convert interface name "eth0" to ip "123.123.123.123".
  auto getIfaceIP(const std::string &iface) -> std::string;

  /// Sockets will not block on read, but instead return immediately if data is not available.
  auto setNonBlocking(int fd) -> bool;

  /// Disable Nagle's algorithm and associated delays.
  auto setNoDelay(int fd) -> bool;

  /// Allow software receive timestamps on incoming packets.
  auto setSOTimestamp(int fd) -> bool;

  /// Check the errno variable to see if an operation would have blocked if the socket was not set to non-blocking.
  auto wouldBlock() -> bool;

  /// Set the Time To Live (TTL) value on multicast packets so they can only pass through a certain number of hops before being discarded.
  auto setMcastTTL(int fd, int ttl) -> bool;

  /// Set the Time To Live (TTL) value on non-multicast packets so they can only pass through a certain number of hops before being discarded.
  auto setTTL(int fd, int ttl) -> bool;

  /// Add / Join membership / subscription to the multicast stream specified and on the interface specified.
  auto join(int fd, const std::string &ip, const std::string &iface, int port) -> bool;

  /// Create a TCP / UDP socket to either connect to or listen for data on or listen for connections on the specified interface and IP:port information.
  auto createSocket(Logger &logger, const SocketCfg& socket_cfg) -> int;
}
