#pragma once

#include "common/macros.h"
#include "common/types.h"
#include "common/logging.h"

#include "exchange/order_server/client_response.h"

#include "market_order_book.h"

using namespace Common;

namespace Trading {
  /// PositionInfo tracks the position, pnl (realized and unrealized) and volume for a single trading instrument.
  struct PositionInfo {
    int32_t position_ = 0;
    double real_pnl_ = 0, unreal_pnl_ = 0, total_pnl_ = 0;
    std::array<double, sideToIndex(Side::MAX) + 1> open_vwap_;
    Qty volume_ = 0;
    const BBO *bbo_ = nullptr;

    auto toString() const {
      std::stringstream ss;
      ss << "Position{"
         << "pos:" << position_
         << " u-pnl:" << unreal_pnl_
         << " r-pnl:" << real_pnl_
         << " t-pnl:" << total_pnl_
         << " vol:" << qtyToString(volume_)
         << " vwaps:[" << (position_ ? open_vwap_.at(sideToIndex(Side::BUY)) / std::abs(position_) : 0)
         << "X" << (position_ ? open_vwap_.at(sideToIndex(Side::SELL)) / std::abs(position_) : 0)
         << "] "
         << (bbo_ ? bbo_->toString() : "") << "}";

      return ss.str();
    }

    /// Process an execution and update the position, pnl and volume.
    auto addFill(const Exchange::MEClientResponse *client_response, Logger *logger) noexcept {
      const auto old_position = position_;
      const auto side_index = sideToIndex(client_response->side_);
      const auto opp_side_index = sideToIndex(client_response->side_ == Side::BUY ? Side::SELL : Side::BUY);
      const auto side_value = sideToValue(client_response->side_);
      position_ += client_response->exec_qty_ * side_value;
      volume_ += client_response->exec_qty_;

      if (old_position * sideToValue(client_response->side_) >= 0) { // opened / increased position.
        open_vwap_[side_index] += (client_response->price_ * client_response->exec_qty_);
      } else { // decreased position.
        const auto opp_side_vwap = open_vwap_[opp_side_index] / std::abs(old_position);
        open_vwap_[opp_side_index] = opp_side_vwap * std::abs(position_);
        real_pnl_ += std::min(static_cast<int32_t>(client_response->exec_qty_), std::abs(old_position)) *
                     (opp_side_vwap - client_response->price_) * sideToValue(client_response->side_);
        if (position_ * old_position < 0) { // flipped position to opposite sign.
          open_vwap_[side_index] = (client_response->price_ * std::abs(position_));
          open_vwap_[opp_side_index] = 0;
        }
      }

      if (!position_) { // flat
        open_vwap_[sideToIndex(Side::BUY)] = open_vwap_[sideToIndex(Side::SELL)] = 0;
        unreal_pnl_ = 0;
      } else {
        if (position_ > 0)
          unreal_pnl_ =
              (client_response->price_ - open_vwap_[sideToIndex(Side::BUY)] / std::abs(position_)) *
              std::abs(position_);
        else
          unreal_pnl_ =
              (open_vwap_[sideToIndex(Side::SELL)] / std::abs(position_) - client_response->price_) *
              std::abs(position_);
      }

      total_pnl_ = unreal_pnl_ + real_pnl_;

      std::string time_str;
      logger->log("%:% %() % % %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str),
                  toString(), client_response->toString().c_str());
    }

    /// Process a change in top-of-book prices (BBO), and update unrealized pnl if there is an open position.
    auto updateBBO(const BBO *bbo, Logger *logger) noexcept {
      std::string time_str;
      bbo_ = bbo;

      if (position_ && bbo->bid_price_ != Price_INVALID && bbo->ask_price_ != Price_INVALID) {
        const auto mid_price = (bbo->bid_price_ + bbo->ask_price_) * 0.5;
        if (position_ > 0)
          unreal_pnl_ =
              (mid_price - open_vwap_[sideToIndex(Side::BUY)] / std::abs(position_)) *
              std::abs(position_);
        else
          unreal_pnl_ =
              (open_vwap_[sideToIndex(Side::SELL)] / std::abs(position_) - mid_price) *
              std::abs(position_);

        const auto old_total_pnl = total_pnl_;
        total_pnl_ = unreal_pnl_ + real_pnl_;

        if (total_pnl_ != old_total_pnl)
          logger->log("%:% %() % % %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str),
                      toString(), bbo_->toString());
      }
    }
  };

  /// Top level position keeper class to compute position, pnl and volume for all trading instruments.
  class PositionKeeper {
  public:
    PositionKeeper(Common::Logger *logger)
        : logger_(logger) {
    }

    /// Deleted default, copy & move constructors and assignment-operators.
    PositionKeeper() = delete;

    PositionKeeper(const PositionKeeper &) = delete;

    PositionKeeper(const PositionKeeper &&) = delete;

    PositionKeeper &operator=(const PositionKeeper &) = delete;

    PositionKeeper &operator=(const PositionKeeper &&) = delete;

  private:
    std::string time_str_;
    Common::Logger *logger_ = nullptr;

    /// Hash map container from TickerId -> PositionInfo.
    std::array<PositionInfo, ME_MAX_TICKERS> ticker_position_;

  public:
    auto addFill(const Exchange::MEClientResponse *client_response) noexcept {
      ticker_position_.at(client_response->ticker_id_).addFill(client_response, logger_);
    }

    auto updateBBO(TickerId ticker_id, const BBO *bbo) noexcept {
      ticker_position_.at(ticker_id).updateBBO(bbo, logger_);
    }

    auto getPositionInfo(TickerId ticker_id) const noexcept {
      return &(ticker_position_.at(ticker_id));
    }

    auto toString() const {
      double total_pnl = 0;
      Qty total_vol = 0;

      std::stringstream ss;
      for(TickerId i = 0; i < ticker_position_.size(); ++i) {
        ss << "TickerId:" << tickerIdToString(i) << " " << ticker_position_.at(i).toString() << "\n";

        total_pnl += ticker_position_.at(i).total_pnl_;
        total_vol += ticker_position_.at(i).volume_;
      }
      ss << "Total PnL:" << total_pnl << " Vol:" << total_vol << "\n";

      return ss.str();
    }
  };
}
