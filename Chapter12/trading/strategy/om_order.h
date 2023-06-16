#pragma once

#include <array>
#include <sstream>
#include "common/types.h"

using namespace Common;

namespace Trading {
  /// Represents the type / action in the order structure in the order manager.
  enum class OMOrderState : int8_t {
    INVALID = 0,
    PENDING_NEW = 1,
    LIVE = 2,
    PENDING_CANCEL = 3,
    DEAD = 4
  };

  inline auto OMOrderStateToString(OMOrderState side) -> std::string {
    switch (side) {
      case OMOrderState::PENDING_NEW:
        return "PENDING_NEW";
      case OMOrderState::LIVE:
        return "LIVE";
      case OMOrderState::PENDING_CANCEL:
        return "PENDING_CANCEL";
      case OMOrderState::DEAD:
        return "DEAD";
      case OMOrderState::INVALID:
        return "INVALID";
    }

    return "UNKNOWN";
  }

  /// Internal structure used by the order manager to represent a single strategy order.
  struct OMOrder {
    TickerId ticker_id_ = TickerId_INVALID;
    OrderId order_id_ = OrderId_INVALID;
    Side side_ = Side::INVALID;
    Price price_ = Price_INVALID;
    Qty qty_ = Qty_INVALID;
    OMOrderState order_state_ = OMOrderState::INVALID;

    auto toString() const {
      std::stringstream ss;
      ss << "OMOrder" << "["
         << "tid:" << tickerIdToString(ticker_id_) << " "
         << "oid:" << orderIdToString(order_id_) << " "
         << "side:" << sideToString(side_) << " "
         << "price:" << priceToString(price_) << " "
         << "qty:" << qtyToString(qty_) << " "
         << "state:" << OMOrderStateToString(order_state_) << "]";

      return ss.str();
    }
  };

  /// Hash map from Side -> OMOrder.
  typedef std::array<OMOrder, sideToIndex(Side::MAX) + 1> OMOrderSideHashMap;

  /// Hash map from TickerId -> Side -> OMOrder.
  typedef std::array<OMOrderSideHashMap, ME_MAX_TICKERS> OMOrderTickerSideHashMap;
}
