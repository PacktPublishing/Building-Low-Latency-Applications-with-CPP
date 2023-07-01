#pragma once

#include <functional>

#include "common/thread_utils.h"
#include "common/macros.h"
#include "common/tcp_server.h"

#include "exchange/order_server/client_request.h"
#include "exchange/order_server/client_response.h"

namespace Trading {
  class OrderGateway {
  public:
    OrderGateway(ClientId client_id,
                 Exchange::ClientRequestLFQueue *client_requests,
                 Exchange::ClientResponseLFQueue *client_responses,
                 std::string ip, const std::string &iface, int port);

    ~OrderGateway() {
      stop();

      using namespace std::literals::chrono_literals;
      std::this_thread::sleep_for(5s);
    }

    /// Start and stop the order gateway main thread.
    auto start() {
      run_ = true;
      ASSERT(tcp_socket_.connect(ip_, iface_, port_, false) >= 0,
             "Unable to connect to ip:" + ip_ + " port:" + std::to_string(port_) + " on iface:" + iface_ + " error:" + std::string(std::strerror(errno)));
      ASSERT(Common::createAndStartThread(-1, "Trading/OrderGateway", [this]() { run(); }) != nullptr, "Failed to start OrderGateway thread.");
    }

    auto stop() -> void {
      run_ = false;
    }

    /// Deleted default, copy & move constructors and assignment-operators.
    OrderGateway() = delete;

    OrderGateway(const OrderGateway &) = delete;

    OrderGateway(const OrderGateway &&) = delete;

    OrderGateway &operator=(const OrderGateway &) = delete;

    OrderGateway &operator=(const OrderGateway &&) = delete;

  private:
    const ClientId client_id_;

    /// Exchange's order server's TCP server address.
    std::string ip_;
    const std::string iface_;
    const int port_ = 0;

    /// Lock free queue on which we consume client requests from the trade engine and forward them to the exchange's order server.
    Exchange::ClientRequestLFQueue *outgoing_requests_ = nullptr;

    /// Lock free queue on which we write client responses which we read and processed from the exchange, to be consumed by the trade engine.
    Exchange::ClientResponseLFQueue *incoming_responses_ = nullptr;

    volatile bool run_ = false;

    std::string time_str_;
    Logger logger_;

    /// Sequence numbers to track the sequence number to set on outgoing client requests and expected on incoming client responses.
    size_t next_outgoing_seq_num_ = 1;
    size_t next_exp_seq_num_ = 1;

    /// TCP connection to the exchange's order server.
    Common::TCPSocket tcp_socket_;

  private:
    /// Main thread loop - sends out client requests to the exchange and reads and dispatches incoming client responses.
    auto run() noexcept -> void;

    /// Callback when an incoming client response is read, we perform some checks and forward it to the lock free queue connected to the trade engine.
    auto recvCallback(TCPSocket *socket, Nanos rx_time) noexcept -> void;
  };
}
