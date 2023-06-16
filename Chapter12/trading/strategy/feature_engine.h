#pragma once

#include "common/macros.h"
#include "common/logging.h"

using namespace Common;

namespace Trading {
  /// Sentinel value to represent invalid / uninitialized feature value.
  constexpr auto Feature_INVALID = std::numeric_limits<double>::quiet_NaN();

  class FeatureEngine {
  public:
    FeatureEngine(Common::Logger *logger)
        : logger_(logger) {
    }

    /// Process a change in order book and in this case compute the fair market price.
    auto onOrderBookUpdate(TickerId ticker_id, Price price, Side side, MarketOrderBook* book) noexcept -> void {
      const auto bbo = book->getBBO();
      if(LIKELY(bbo->bid_price_ != Price_INVALID && bbo->ask_price_ != Price_INVALID)) {
        mkt_price_ = (bbo->bid_price_ * bbo->ask_qty_ + bbo->ask_price_ * bbo->bid_qty_) / static_cast<double>(bbo->bid_qty_ + bbo->ask_qty_);
      }

      logger_->log("%:% %() % ticker:% price:% side:% mkt-price:% agg-trade-ratio:%\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), ticker_id, Common::priceToString(price).c_str(),
                   Common::sideToString(side).c_str(), mkt_price_, agg_trade_qty_ratio_);
    }

    /// Process a trade event and in this case compute the feature to capture aggressive trade quantity ratio against the BBO quantity.
    auto onTradeUpdate(const Exchange::MEMarketUpdate *market_update, MarketOrderBook* book) noexcept -> void {
      const auto bbo = book->getBBO();
      if(LIKELY(bbo->bid_price_ != Price_INVALID && bbo->ask_price_ != Price_INVALID)) {
        agg_trade_qty_ratio_ = static_cast<double>(market_update->qty_) / (market_update->side_ == Side::BUY ? bbo->ask_qty_ : bbo->bid_qty_);
      }

      logger_->log("%:% %() % % mkt-price:% agg-trade-ratio:%\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_),
                   market_update->toString().c_str(), mkt_price_, agg_trade_qty_ratio_);
    }

    auto getMktPrice() const noexcept {
      return mkt_price_;
    }

    auto getAggTradeQtyRatio() const noexcept {
      return agg_trade_qty_ratio_;
    }

    /// Deleted default, copy & move constructors and assignment-operators.
    FeatureEngine() = delete;

    FeatureEngine(const FeatureEngine &) = delete;

    FeatureEngine(const FeatureEngine &&) = delete;

    FeatureEngine &operator=(const FeatureEngine &) = delete;

    FeatureEngine &operator=(const FeatureEngine &&) = delete;

  private:
    std::string time_str_;
    Common::Logger *logger_ = nullptr;

    /// The two features we compute in our feature engine.
    double mkt_price_ = Feature_INVALID, agg_trade_qty_ratio_ = Feature_INVALID;
  };
}
