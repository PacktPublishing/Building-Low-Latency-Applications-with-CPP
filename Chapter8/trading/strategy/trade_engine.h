#pragma once

#include <functional>

#include "common/thread_utils.h"
#include "common/time_utils.h"
#include "common/lf_queue.h"
#include "common/macros.h"
#include "common/logging.h"

#include "exchange/order_server/client_request.h"
#include "exchange/order_server/client_response.h"
#include "exchange/market_data/market_update.h"

#include "market_order_book.h"

namespace Trading {
  class TradeEngine {
  public:
    TradeEngine(Common::ClientId client_id,
                Exchange::ClientRequestLFQueue *client_requests,
                Exchange::ClientResponseLFQueue *client_responses,
                Exchange::MEMarketUpdateLFQueue *market_updates);

    ~TradeEngine();

    auto start() -> void {
      run_ = true;
      ASSERT(Common::createAndStartThread(-1, "Trading/TradeEngine", [this] { run(); }) != nullptr, "Failed to start TradeEngine thread.");
    }

    auto stop() -> void {
      while(incoming_ogw_responses_->size() || incoming_md_updates_->size()) {
        logger_.log("%:% %() % Sleeping till all updates are consumed ogw-size:% md-size:%\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), incoming_ogw_responses_->size(), incoming_md_updates_->size());

        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(10ms);
      }

      run_ = false;
    }

    auto run() noexcept -> void;

    auto sendClientRequest(const Exchange::MEClientRequest *client_request) noexcept -> void;

    auto onOrderBookUpdate(TickerId ticker_id, Price price, Side side) noexcept -> void;
    auto onTradeUpdate(const Exchange::MEMarketUpdate *market_update) noexcept -> void;

    auto onOrderUpdate(const Exchange::MEClientResponse* client_response) noexcept -> void;

    auto initLastEventTime() {
      last_event_time_ = Common::getCurrentNanos();
    }
    auto silentSeconds() {
      return (Common::getCurrentNanos() - last_event_time_) / NANOS_TO_SECS;
    }

    // Deleted default, copy & move constructors and assignment-operators.
    TradeEngine() = delete;

    TradeEngine(const TradeEngine &) = delete;

    TradeEngine(const TradeEngine &&) = delete;

    TradeEngine &operator=(const TradeEngine &) = delete;

    TradeEngine &operator=(const TradeEngine &&) = delete;

  private:
    const ClientId client_id_;

    MarketOrderBookHashMap ticker_order_book_;

    Exchange::ClientRequestLFQueue *outgoing_ogw_requests_ = nullptr;
    Exchange::ClientResponseLFQueue *incoming_ogw_responses_ = nullptr;
    Exchange::MEMarketUpdateLFQueue *incoming_md_updates_ = nullptr;

    Nanos last_event_time_ = 0;
    volatile bool run_ = false;

    std::string time_str_;
    Logger logger_;
  };
}
