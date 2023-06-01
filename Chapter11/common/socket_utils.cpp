#include "socket_utils.h"

namespace Common {
  auto getIfaceIP(const std::string &iface) -> std::string {
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

  auto setNonBlocking(int fd) -> bool {
    const auto flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
      return false;
    if (flags & O_NONBLOCK)
      return true;
    return (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1);
  }

  auto setNoDelay(int fd) -> bool {
    int one = 1;
    return (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<void *>(&one), sizeof(one)) != -1);
  }

  auto setSOTimestamp(int fd) -> bool {
    int one = 1;
    return (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMP, reinterpret_cast<void *>(&one), sizeof(one)) != -1);
  }

  auto wouldBlock() -> bool {
    return (errno == EWOULDBLOCK || errno == EINPROGRESS);
  }

  auto setMcastTTL(int fd, int mcast_ttl) -> bool {
    return (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<void *>(&mcast_ttl), sizeof(mcast_ttl)) != -1);
  }

  auto setTTL(int fd, int ttl) -> bool {
    return (setsockopt(fd, IPPROTO_IP, IP_TTL, reinterpret_cast<void *>(&ttl), sizeof(ttl)) != -1);
  }

  auto join(int fd, const std::string &ip, const std::string &iface, int) -> bool {
    ip_mreq mreq;
    mreq.imr_interface.s_addr = inet_addr(iface.c_str());
    mreq.imr_multiaddr.s_addr = inet_addr(ip.c_str());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    return (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != -1);
  }

  auto createSocket(Logger &logger, const std::string &t_ip, const std::string &iface, int port,
                    bool is_udp, bool is_blocking, bool is_listening, int ttl, bool needs_so_timestamp) -> int {
    std::string time_str;

    const auto ip = t_ip.empty() ? getIfaceIP(iface) : t_ip;
    logger.log("%:% %() % ip:% iface:% port:% is_udp:% is_blocking:% is_listening:% ttl:% SO_time:%\n", __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str), ip, iface, port, is_udp, is_blocking, is_listening, ttl, needs_so_timestamp);

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = is_udp ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = is_udp ? IPPROTO_UDP : IPPROTO_TCP;
    hints.ai_flags = is_listening ? AI_PASSIVE : 0;
    if (std::isdigit(ip.c_str()[0]))
      hints.ai_flags |= AI_NUMERICHOST;
    hints.ai_flags |= AI_NUMERICSERV;

    addrinfo *result = nullptr;
    const auto rc = getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &result);
    if (rc) {
      logger.log("getaddrinfo() failed. error:% errno:%\n", gai_strerror(rc), strerror(errno));
      return -1;
    }

    int fd = -1;
    int one = 1;
    for (addrinfo *rp = result; rp; rp = rp->ai_next) {
      fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (fd == -1) {
        logger.log("socket() failed. errno:%\n", strerror(errno));
        return -1;
      }
      if (!is_blocking) {
        if (!setNonBlocking(fd)) {
          logger.log("setNonBlocking() failed. errno:%\n", strerror(errno));
          return -1;
        }

        if (!is_udp && !setNoDelay(fd)) {
          logger.log("setNoDelay() failed. errno:%\n", strerror(errno));
          return -1;
        }
      }
      if (!is_listening && connect(fd, rp->ai_addr, rp->ai_addrlen) == 1 && !wouldBlock()) {
        logger.log("connect() failed. errno:%\n", strerror(errno));
        return -1;
      }
      if (is_listening && setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof(one)) == -1) {
        logger.log("setsockopt() SO_REUSEADDR failed. errno:%\n", strerror(errno));
        return -1;
      }
      if (is_listening) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);

        if (is_udp && bind(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
          logger.log("bind() failed. errno:%\n", strerror(errno));
          return -1;
        } else if (!is_udp && bind(fd, rp->ai_addr, rp->ai_addrlen) == -1) {
          logger.log("bind() failed. errno:%\n", strerror(errno));
          return -1;
        }
      }

      if (!is_udp && is_listening && listen(fd, MaxTCPServerBacklog) == -1) {
        logger.log("listen() failed. errno:%\n", strerror(errno));
        return -1;
      }
      if (is_udp && ttl) {
        const bool is_multicast = atoi(ip.c_str()) & 0xe0;
        if (is_multicast && !setMcastTTL(fd, ttl)) {
          logger.log("setMcastTTL() failed. errno:%\n", strerror(errno));
          return -1;
        }
        if (!is_multicast && !setTTL(fd, ttl)) {
          logger.log("setTTL() failed. errno:%\n", strerror(errno));
          return -1;
        }
      }
      if (needs_so_timestamp && !setSOTimestamp(fd)) {
        logger.log("setSOTimestamp() failed. errno:%\n", strerror(errno));
        return -1;
      }
    }

    if (result)
      freeaddrinfo(result);

    return fd;
  }
}
