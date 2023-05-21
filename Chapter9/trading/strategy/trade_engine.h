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

#include "feature_engine.h"
#include "position_keeper.h"
#include "order_manager.h"
#include "risk_manager.h"

#include "market_maker.h"
#include "liquidity_taker.h"

namespace Trading {
  class TradeEngine {
  public:
    TradeEngine(Common::ClientId client_id,
                AlgoType algo_type,
                const TradeEngineCfgHashMap &ticker_cfg,
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

      logger_.log("%:% %() % POSITIONS\n%\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                  position_keeper_.toString());

      run_ = false;
    }

    auto run() noexcept -> void;

    auto sendClientRequest(const Exchange::MEClientRequest *client_request) noexcept -> void;

    auto onOrderBookUpdate(TickerId ticker_id, Price price, Side side, MarketOrderBook *book) noexcept -> void;
    auto onTradeUpdate(const Exchange::MEMarketUpdate *market_update, MarketOrderBook *book) noexcept -> void;

    auto onOrderUpdate(const Exchange::MEClientResponse *client_response) noexcept -> void;

    std::function<void(TickerId ticker_id, Price price, Side side, MarketOrderBook *book)> algoOnOrderBookUpdate_;
    std::function<void(const Exchange::MEMarketUpdate *market_update, MarketOrderBook *book)> algoOnTradeUpdate_;
    std::function<void(const Exchange::MEClientResponse *client_response)> algoOnOrderUpdate_;

    auto initLastEventTime() {
      last_event_time_ = Common::getCurrentNanos();
    }

    auto silentSeconds() {
      return (Common::getCurrentNanos() - last_event_time_) / NANOS_TO_SECS;
    }

    auto clientId() const {
      return client_id_;
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

    FeatureEngine feature_engine_;
    PositionKeeper position_keeper_;
    OrderManager order_manager_;
    RiskManager risk_manager_;

    MarketMaker *mm_algo_ = nullptr;
    LiquidityTaker *taker_algo_ = nullptr;

    auto defaultAlgoOnOrderBookUpdate(TickerId ticker_id, Price price, Side side, MarketOrderBook *) noexcept -> void {
      logger_.log("%:% %() % ticker:% price:% side:%\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), ticker_id, Common::priceToString(price).c_str(),
                  Common::sideToString(side).c_str());
    }

    auto defaultAlgoOnTradeUpdate(const Exchange::MEMarketUpdate *market_update, MarketOrderBook *) noexcept -> void {
      logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                  market_update->toString().c_str());
    }

    auto defaultAlgoOnOrderUpdate(const Exchange::MEClientResponse *client_response) noexcept -> void {
      logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                  client_response->toString().c_str());
    }
  };
}
