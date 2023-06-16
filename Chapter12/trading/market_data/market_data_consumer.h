#pragma once

#include <functional>
#include <map>

#include "common/thread_utils.h"
#include "common/lf_queue.h"
#include "common/macros.h"
#include "common/mcast_socket.h"

#include "exchange/market_data/market_update.h"

namespace Trading {
  class MarketDataConsumer {
  public:
    MarketDataConsumer(Common::ClientId client_id, Exchange::MEMarketUpdateLFQueue *market_updates, const std::string &iface,
                       const std::string &snapshot_ip, int snapshot_port,
                       const std::string &incremental_ip, int incremental_port);

    ~MarketDataConsumer() {
      stop();

      using namespace std::literals::chrono_literals;
      std::this_thread::sleep_for(5s);
    }

    /// Start and stop the market data consumer main thread.
    auto start() {
      run_ = true;
      ASSERT(Common::createAndStartThread(-1, "Trading/MarketDataConsumer", [this]() { run(); }) != nullptr, "Failed to start MarketData thread.");
    }

    auto stop() -> void {
      run_ = false;
    }

    /// Deleted default, copy & move constructors and assignment-operators.
    MarketDataConsumer() = delete;

    MarketDataConsumer(const MarketDataConsumer &) = delete;

    MarketDataConsumer(const MarketDataConsumer &&) = delete;

    MarketDataConsumer &operator=(const MarketDataConsumer &) = delete;

    MarketDataConsumer &operator=(const MarketDataConsumer &&) = delete;

  private:
    /// Track the next expected sequence number on the incremental market data stream, used to detect gaps / drops.
    size_t next_exp_inc_seq_num_ = 1;

    /// Lock free queue on which decoded market data updates are pushed to, to be consumed by the trade engine.
    Exchange::MEMarketUpdateLFQueue *incoming_md_updates_ = nullptr;

    volatile bool run_ = false;

    std::string time_str_;
    Logger logger_;

    /// Multicast subscriber sockets for the incremental and market data streams.
    Common::McastSocket incremental_mcast_socket_, snapshot_mcast_socket_;

    /// Tracks if we are currently in the process of recovering / synchronizing with the snapshot market data stream either because we just started up or we dropped a packet.
    bool in_recovery_ = false;

    /// Information for the snapshot multicast stream.
    const std::string iface_, snapshot_ip_;
    const int snapshot_port_;

    /// Containers to queue up market data updates from the snapshot and incremental channels, queued up in order of increasing sequence numbers.
    typedef std::map<size_t, Exchange::MEMarketUpdate> QueuedMarketUpdates;
    QueuedMarketUpdates snapshot_queued_msgs_, incremental_queued_msgs_;

  private:
    /// Main loop for this thread - reads and processes messages from the multicast sockets - the heavy lifting is in the recvCallback() and checkSnapshotSync() methods.
    auto run() noexcept -> void;

    /// Process a market data update, the consumer needs to use the socket parameter to figure out whether this came from the snapshot or the incremental stream.
    auto recvCallback(McastSocket *socket) noexcept -> void;

    /// Queue up a message in the *_queued_msgs_ containers, first parameter specifies if this update came from the snapshot or the incremental streams.
    auto queueMessage(bool is_snapshot, const Exchange::MDPMarketUpdate *request);

    /// Start the process of snapshot synchronization by subscribing to the snapshot multicast stream.
    auto startSnapshotSync() -> void;

    /// Check if a recovery / synchronization is possible from the queued up market data updates from the snapshot and incremental market data streams.
    auto checkSnapshotSync() -> void;
  };
}
