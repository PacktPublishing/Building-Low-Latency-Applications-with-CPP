#pragma once

#include "common/macros.h"
#include "common/logging.h"

#include "exchange/order_server/client_response.h"

#include "om_order.h"
#include "risk_manager.h"

using namespace Common;

namespace Trading {
  class TradeEngine;

  /// Manages orders for a trading algorithm, hides the complexity of order management to simplify trading strategies.
  class OrderManager {
  public:
    OrderManager(Common::Logger *logger, TradeEngine *trade_engine, RiskManager& risk_manager)
        : trade_engine_(trade_engine), risk_manager_(risk_manager), logger_(logger) {
    }

    /// Process an order update from a client response and update the state of the orders being managed.
    auto onOrderUpdate(const Exchange::MEClientResponse *client_response) noexcept -> void {
      logger_->log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                   client_response->toString().c_str());
      auto order = &(ticker_side_order_.at(client_response->ticker_id_).at(sideToIndex(client_response->side_)));
      logger_->log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                   order->toString().c_str());

      switch (client_response->type_) {
        case Exchange::ClientResponseType::ACCEPTED: {
          order->order_state_ = OMOrderState::LIVE;
        }
          break;
        case Exchange::ClientResponseType::CANCELED: {
          order->order_state_ = OMOrderState::DEAD;
        }
          break;
        case Exchange::ClientResponseType::FILLED: {
          order->qty_ = client_response->leaves_qty_;
          if(!order->qty_)
            order->order_state_ = OMOrderState::DEAD;
        }
          break;
        case Exchange::ClientResponseType::CANCEL_REJECTED:
        case Exchange::ClientResponseType::INVALID: {
        }
          break;
      }
    }

    /// Send a new order with specified attribute, and update the OMOrder object passed here.
    auto newOrder(OMOrder *order, TickerId ticker_id, Price price, Side side, Qty qty) noexcept -> void;

    /// Send a cancel for the specified order, and update the OMOrder object passed here.
    auto cancelOrder(OMOrder *order) noexcept -> void;

    /// Move a single order on the specified side so that it has the specified price and quantity.
    /// This will perform risk checks prior to sending the order, and update the OMOrder object passed here.
    auto moveOrder(OMOrder *order, TickerId ticker_id, Price price, Side side, Qty qty) noexcept {
      switch (order->order_state_) {
        case OMOrderState::LIVE: {
          if(order->price_ != price) {
            START_MEASURE(Trading_OrderManager_cancelOrder);
            cancelOrder(order);
            END_MEASURE(Trading_OrderManager_cancelOrder, (*logger_));
          }
        }
          break;
        case OMOrderState::INVALID:
        case OMOrderState::DEAD: {
          if(LIKELY(price != Price_INVALID)) {
            START_MEASURE(Trading_RiskManager_checkPreTradeRisk);
            const auto risk_result = risk_manager_.checkPreTradeRisk(ticker_id, side, qty);
            END_MEASURE(Trading_RiskManager_checkPreTradeRisk, (*logger_));
            if(LIKELY(risk_result == RiskCheckResult::ALLOWED)) {
              START_MEASURE(Trading_OrderManager_newOrder);
              newOrder(order, ticker_id, price, side, qty);
              END_MEASURE(Trading_OrderManager_newOrder, (*logger_));
            } else
              logger_->log("%:% %() % Ticker:% Side:% Qty:% RiskCheckResult:%\n", __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_),
                           tickerIdToString(ticker_id), sideToString(side), qtyToString(qty),
                           riskCheckResultToString(risk_result));
          }
        }
          break;
        case OMOrderState::PENDING_NEW:
        case OMOrderState::PENDING_CANCEL:
          break;
      }
    }

    /// Have orders of quantity clip at the specified buy and sell prices.
    /// This can result in new orders being sent if there are none.
    /// This can result in existing orders being cancelled if they are not at the specified price or of the specified quantity.
    /// Specifying Price_INVALID for the buy or sell prices indicates that we do not want an order there.
    auto moveOrders(TickerId ticker_id, Price bid_price, Price ask_price, Qty clip) noexcept {
      {
        auto bid_order = &(ticker_side_order_.at(ticker_id).at(sideToIndex(Side::BUY)));
        START_MEASURE(Trading_OrderManager_moveOrder);
        moveOrder(bid_order, ticker_id, bid_price, Side::BUY, clip);
        END_MEASURE(Trading_OrderManager_moveOrder, (*logger_));
      }

      {
        auto ask_order = &(ticker_side_order_.at(ticker_id).at(sideToIndex(Side::SELL)));
        START_MEASURE(Trading_OrderManager_moveOrder);
        moveOrder(ask_order, ticker_id, ask_price, Side::SELL, clip);
        END_MEASURE(Trading_OrderManager_moveOrder, (*logger_));
      }
    }

    /// Helper method to fetch the buy and sell OMOrders for the specified TickerId.
    auto getOMOrderSideHashMap(TickerId ticker_id) const {
      return &(ticker_side_order_.at(ticker_id));
    }

    /// Deleted default, copy & move constructors and assignment-operators.
    OrderManager() = delete;

    OrderManager(const OrderManager &) = delete;

    OrderManager(const OrderManager &&) = delete;

    OrderManager &operator=(const OrderManager &) = delete;

    OrderManager &operator=(const OrderManager &&) = delete;

  private:
    /// The parent trade engine object, used to send out client requests.
    TradeEngine *trade_engine_ = nullptr;

    /// Risk manager to perform pre-trade risk checks.
    const RiskManager& risk_manager_;

    std::string time_str_;
    Common::Logger *logger_ = nullptr;

    /// Hash map container from TickerId -> Side -> OMOrder.
    OMOrderTickerSideHashMap ticker_side_order_;

    /// Used to set OrderIds on outgoing new order requests.
    OrderId next_order_id_ = 1;
  };
}
