#pragma once

#include <functional>

#include "market_data/snapshot_synthesizer.h"

namespace Exchange {
  class MarketDataPublisher {
  public:
    MarketDataPublisher(MEMarketUpdateLFQueue *market_updates, const std::string &iface,
                        const std::string &snapshot_ip, int snapshot_port,
                        const std::string &incremental_ip, int incremental_port);

    ~MarketDataPublisher() {
      stop();

      using namespace std::literals::chrono_literals;
      std::this_thread::sleep_for(5s);

      delete snapshot_synthesizer_;
      snapshot_synthesizer_ = nullptr;
    }

    auto start() {
      run_ = true;

      ASSERT(Common::createAndStartThread(-1, "Exchange/MarketDataPublisher", [this]() { run(); }) != nullptr, "Failed to start MarketData thread.");

      snapshot_synthesizer_->start();
    }

    auto stop() -> void {
      run_ = false;

      snapshot_synthesizer_->stop();
    }

    auto run() noexcept -> void;

    // Deleted default, copy & move constructors and assignment-operators.
    MarketDataPublisher() = delete;

    MarketDataPublisher(const MarketDataPublisher &) = delete;

    MarketDataPublisher(const MarketDataPublisher &&) = delete;

    MarketDataPublisher &operator=(const MarketDataPublisher &) = delete;

    MarketDataPublisher &operator=(const MarketDataPublisher &&) = delete;

  private:
    size_t next_inc_seq_num_ = 1;
    MEMarketUpdateLFQueue *outgoing_md_updates_ = nullptr;

    MDPMarketUpdateLFQueue snapshot_md_updates_;

    volatile bool run_ = false;

    std::string time_str_;
    Logger logger_;

    Common::McastSocket incremental_socket_;

    SnapshotSynthesizer *snapshot_synthesizer_ = nullptr;
  };
}
