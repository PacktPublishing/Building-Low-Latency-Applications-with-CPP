#pragma once

#include "common/types.h"
#include "common/thread_utils.h"
#include "common/lf_queue.h"
#include "common/macros.h"
#include "common/mcast_socket.h"
#include "common/mem_pool.h"
#include "common/logging.h"

#include "market_data/market_update.h"
#include "matcher/me_order.h"

using namespace Common;

namespace Exchange {
  class SnapshotSynthesizer {
  public:
    SnapshotSynthesizer(MDPMarketUpdateLFQueue *market_updates, const std::string &iface,
                        const std::string &snapshot_ip, int snapshot_port);

    ~SnapshotSynthesizer();

    /// Start and stop the snapshot synthesizer thread.
    auto start() -> void;

    auto stop() -> void;

    /// Process an incremental market update and update the limit order book snapshot.
    auto addToSnapshot(const MDPMarketUpdate *market_update);

    /// Publish a full snapshot cycle on the snapshot multicast stream.
    auto publishSnapshot();

    /// Main method for this thread - processes incremental updates from the market data publisher, updates the snapshot and publishes the snapshot periodically.
    auto run() -> void;

    /// Deleted default, copy & move constructors and assignment-operators.
    SnapshotSynthesizer() = delete;

    SnapshotSynthesizer(const SnapshotSynthesizer &) = delete;

    SnapshotSynthesizer(const SnapshotSynthesizer &&) = delete;

    SnapshotSynthesizer &operator=(const SnapshotSynthesizer &) = delete;

    SnapshotSynthesizer &operator=(const SnapshotSynthesizer &&) = delete;

  private:
    /// Lock free queue containing incremental market data updates coming in from the market data publisher.
    MDPMarketUpdateLFQueue *snapshot_md_updates_ = nullptr;

    Logger logger_;

    volatile bool run_ = false;

    std::string time_str_;

    /// Multicast socket for the snapshot multicast stream.
    McastSocket snapshot_socket_;

    /// Hash map from TickerId -> Full limit order book snapshot containing information for every live order.
    std::array<std::array<MEMarketUpdate *, ME_MAX_ORDER_IDS>, ME_MAX_TICKERS> ticker_orders_;
    size_t last_inc_seq_num_ = 0;
    Nanos last_snapshot_time_ = 0;

    /// Memory pool to manage MEMarketUpdate messages for the orders in the snapshot limit order books.
    MemPool<MEMarketUpdate> order_pool_;
  };
}
