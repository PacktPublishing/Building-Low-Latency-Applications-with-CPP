#pragma once

#include "common/macros.h"
#include "common/logging.h"

#include "order_manager.h"
#include "feature_engine.h"

using namespace Common;

namespace Trading {
  class LiquidityTaker {
  public:
    LiquidityTaker(Common::Logger *logger, TradeEngine *trade_engine, const FeatureEngine *feature_engine,
                   OrderManager *order_manager,
                   const TradeEngineCfgHashMap &ticker_cfg);

    /// Process order book updates, which for the liquidity taking algorithm is none.
    auto onOrderBookUpdate(TickerId ticker_id, Price price, Side side, MarketOrderBook *) noexcept -> void {
      logger_->log("%:% %() % ticker:% price:% side:%\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), ticker_id, Common::priceToString(price).c_str(),
                   Common::sideToString(side).c_str());
    }

    /// Process trade events, fetch the aggressive trade ratio from the feature engine, check against the trading threshold and send aggressive orders.
    auto onTradeUpdate(const Exchange::MEMarketUpdate *market_update, MarketOrderBook *book) noexcept -> void {
      logger_->log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                   market_update->toString().c_str());

      const auto bbo = book->getBBO();
      const auto agg_qty_ratio = feature_engine_->getAggTradeQtyRatio();

      if (LIKELY(bbo->bid_price_ != Price_INVALID && bbo->ask_price_ != Price_INVALID && agg_qty_ratio != Feature_INVALID)) {
        logger_->log("%:% %() % % agg-qty-ratio:%\n", __FILE__, __LINE__, __FUNCTION__,
                     Common::getCurrentTimeStr(&time_str_),
                     bbo->toString().c_str(), agg_qty_ratio);

        const auto clip = ticker_cfg_.at(market_update->ticker_id_).clip_;
        const auto threshold = ticker_cfg_.at(market_update->ticker_id_).threshold_;

        if (agg_qty_ratio >= threshold) {
          START_MEASURE(Trading_OrderManager_moveOrders);
          if (market_update->side_ == Side::BUY)
            order_manager_->moveOrders(market_update->ticker_id_, bbo->ask_price_, Price_INVALID, clip);
          else
            order_manager_->moveOrders(market_update->ticker_id_, Price_INVALID, bbo->bid_price_, clip);
          END_MEASURE(Trading_OrderManager_moveOrders, (*logger_));
        }
      }
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
    LiquidityTaker() = delete;

    LiquidityTaker(const LiquidityTaker &) = delete;

    LiquidityTaker(const LiquidityTaker &&) = delete;

    LiquidityTaker &operator=(const LiquidityTaker &) = delete;

    LiquidityTaker &operator=(const LiquidityTaker &&) = delete;

  private:
    /// The feature engine that drives the liquidity taking algorithm.
    const FeatureEngine *feature_engine_ = nullptr;

    /// Used by the liquidity taking algorithm to send aggressive orders.
    OrderManager *order_manager_ = nullptr;

    std::string time_str_;
    Common::Logger *logger_ = nullptr;

    /// Holds the trading configuration for the liquidity taking algorithm.
    const TradeEngineCfgHashMap ticker_cfg_;
  };
}
