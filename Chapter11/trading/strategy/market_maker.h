#pragma once

#include "common/macros.h"
#include "common/logging.h"

#include "order_manager.h"
#include "feature_engine.h"

using namespace Common;

namespace Trading {
  class MarketMaker {
  public:
    MarketMaker(Common::Logger *logger, TradeEngine *trade_engine, const FeatureEngine *feature_engine,
                OrderManager *order_manager,
                const TradeEngineCfgHashMap &ticker_cfg);

    /// Process order book updates, fetch the fair market price from the feature engine, check against the trading threshold and modify the passive orders.
    auto onOrderBookUpdate(TickerId ticker_id, Price price, Side side, const MarketOrderBook *book) noexcept -> void {
      logger_->log("%:% %() % ticker:% price:% side:%\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), ticker_id, Common::priceToString(price).c_str(),
                   Common::sideToString(side).c_str());

      const auto bbo = book->getBBO();
      const auto fair_price = feature_engine_->getMktPrice();

      if (LIKELY(bbo->bid_price_ != Price_INVALID && bbo->ask_price_ != Price_INVALID && fair_price != Feature_INVALID)) {
        logger_->log("%:% %() % % fair-price:%\n", __FILE__, __LINE__, __FUNCTION__,
                     Common::getCurrentTimeStr(&time_str_),
                     bbo->toString().c_str(), fair_price);

        const auto clip = ticker_cfg_.at(ticker_id).clip_;
        const auto threshold = ticker_cfg_.at(ticker_id).threshold_;

        const auto bid_price = bbo->bid_price_ - (fair_price - bbo->bid_price_ >= threshold ? 0 : 1);
        const auto ask_price = bbo->ask_price_ + (bbo->ask_price_ - fair_price >= threshold ? 0 : 1);

        START_MEASURE(Trading_OrderManager_moveOrders);
        order_manager_->moveOrders(ticker_id, bid_price, ask_price, clip);
        END_MEASURE(Trading_OrderManager_moveOrders, (*logger_));
      }
    }

    /// Process trade events, which for the market making algorithm is none.
    auto onTradeUpdate(const Exchange::MEMarketUpdate *market_update, MarketOrderBook * /* book */) noexcept -> void {
      logger_->log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                   market_update->toString().c_str());
    }

    /// Process client responses for the strategy's orders.
    auto onOrderUpdate(const Exchange::MEClientResponse *client_response) noexcept -> void {
      logger_->log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                   client_response->toString().c_str());

      START_MEASURE(Trading_OrderManager_onOrderUpdate);
      order_manager_->onOrderUpdate(client_response);
      END_MEASURE(Trading_OrderManager_onOrderUpdate, (*logger_));
    }

    /// Deleted default, copy & move constructors and assignment-operators.
    MarketMaker() = delete;

    MarketMaker(const MarketMaker &) = delete;

    MarketMaker(const MarketMaker &&) = delete;

    MarketMaker &operator=(const MarketMaker &) = delete;

    MarketMaker &operator=(const MarketMaker &&) = delete;

  private:
    /// The feature engine that drives the market making algorithm.
    const FeatureEngine *feature_engine_ = nullptr;

    /// Used by the market making algorithm to manage its passive orders.
    OrderManager *order_manager_ = nullptr;

    std::string time_str_;
    Common::Logger *logger_ = nullptr;

    /// Holds the trading configuration for the market making algorithm.
    const TradeEngineCfgHashMap ticker_cfg_;
  };
}
