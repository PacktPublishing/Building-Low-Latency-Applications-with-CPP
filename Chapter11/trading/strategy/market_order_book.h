#pragma once

#include "common/types.h"
#include "common/mem_pool.h"
#include "common/logging.h"

#include "market_order.h"
#include "exchange/market_data/market_update.h"

namespace Trading {
  class TradeEngine;

  class MarketOrderBook final {
  public:
    MarketOrderBook(TickerId ticker_id, Logger *logger);

    ~MarketOrderBook();

    /// Process market data update and update the limit order book.
    auto onMarketUpdate(const Exchange::MEMarketUpdate *market_update) noexcept -> void;

    auto setTradeEngine(TradeEngine *trade_engine) {
      trade_engine_ = trade_engine;
    }

    /// Update the BBO abstraction, the two boolean parameters represent if the buy or the sekk (or both) sides or both need to be updated.
    auto updateBBO(bool update_bid, bool update_ask) noexcept {
      if(update_bid) {
        if(bids_by_price_) {
          bbo_.bid_price_ = bids_by_price_->price_;
          bbo_.bid_qty_ = bids_by_price_->first_mkt_order_->qty_;
          for(auto order = bids_by_price_->first_mkt_order_->next_order_; order != bids_by_price_->first_mkt_order_; order = order->next_order_)
            bbo_.bid_qty_ += order->qty_;
        }
        else {
          bbo_.bid_price_ = Price_INVALID;
          bbo_.bid_qty_ = Qty_INVALID;
        }
      }

      if(update_ask) {
        if(asks_by_price_) {
          bbo_.ask_price_ = asks_by_price_->price_;
          bbo_.ask_qty_ = asks_by_price_->first_mkt_order_->qty_;
          for(auto order = asks_by_price_->first_mkt_order_->next_order_; order != asks_by_price_->first_mkt_order_; order = order->next_order_)
            bbo_.ask_qty_ += order->qty_;
        }
        else {
          bbo_.ask_price_ = Price_INVALID;
          bbo_.ask_qty_ = Qty_INVALID;
        }
      }
    }

    auto getBBO() const noexcept -> const BBO* {
      return &bbo_;
    }

    auto toString(bool detailed, bool validity_check) const -> std::string;

    /// Deleted default, copy & move constructors and assignment-operators.
    MarketOrderBook() = delete;

    MarketOrderBook(const MarketOrderBook &) = delete;

    MarketOrderBook(const MarketOrderBook &&) = delete;

    MarketOrderBook &operator=(const MarketOrderBook &) = delete;

    MarketOrderBook &operator=(const MarketOrderBook &&) = delete;

  private:
    const TickerId ticker_id_;

    /// Parent trade engine that owns this limit order book, used to send notifications when book changes or trades occur.
    TradeEngine *trade_engine_ = nullptr;

    /// Hash map from OrderId -> MarketOrder.
    OrderHashMap oid_to_order_;

    /// Memory pool to manage MarketOrdersAtPrice objects.
    MemPool<MarketOrdersAtPrice> orders_at_price_pool_;

    /// Pointers to beginning / best prices / top of book of buy and sell price levels.
    MarketOrdersAtPrice *bids_by_price_ = nullptr;
    MarketOrdersAtPrice *asks_by_price_ = nullptr;

    /// Hash map from Price -> MarketOrdersAtPrice.
    OrdersAtPriceHashMap price_orders_at_price_;

    /// Memory pool to manage MarketOrder objects.
    MemPool<MarketOrder> order_pool_;

    BBO bbo_;

    std::string time_str_;
    Logger *logger_ = nullptr;

  private:
    auto priceToIndex(Price price) const noexcept {
      return (price % ME_MAX_PRICE_LEVELS);
    }

    /// Fetch and return the MarketOrdersAtPrice corresponding to the provided price.
    auto getOrdersAtPrice(Price price) const noexcept -> MarketOrdersAtPrice * {
      return price_orders_at_price_.at(priceToIndex(price));
    }

    /// Add a new MarketOrdersAtPrice at the correct price into the containers - the hash map and the doubly linked list of price levels.
    auto addOrdersAtPrice(MarketOrdersAtPrice *new_orders_at_price) noexcept {
      price_orders_at_price_.at(priceToIndex(new_orders_at_price->price_)) = new_orders_at_price;

      const auto best_orders_by_price = (new_orders_at_price->side_ == Side::BUY ? bids_by_price_ : asks_by_price_);
      if (UNLIKELY(!best_orders_by_price)) {
        (new_orders_at_price->side_ == Side::BUY ? bids_by_price_ : asks_by_price_) = new_orders_at_price;
        new_orders_at_price->prev_entry_ = new_orders_at_price->next_entry_ = new_orders_at_price;
      } else {
        auto target = best_orders_by_price;
        bool add_after = ((new_orders_at_price->side_ == Side::SELL && new_orders_at_price->price_ > target->price_) ||
                          (new_orders_at_price->side_ == Side::BUY && new_orders_at_price->price_ < target->price_));
        if (add_after) {
          target = target->next_entry_;
          add_after = ((new_orders_at_price->side_ == Side::SELL && new_orders_at_price->price_ > target->price_) ||
                       (new_orders_at_price->side_ == Side::BUY && new_orders_at_price->price_ < target->price_));
        }
        while (add_after && target != best_orders_by_price) {
          add_after = ((new_orders_at_price->side_ == Side::SELL && new_orders_at_price->price_ > target->price_) ||
                       (new_orders_at_price->side_ == Side::BUY && new_orders_at_price->price_ < target->price_));
          if (add_after)
            target = target->next_entry_;
        }

        if (add_after) { // add new_orders_at_price after target.
          if (target == best_orders_by_price) {
            target = best_orders_by_price->prev_entry_;
          }
          new_orders_at_price->prev_entry_ = target;
          target->next_entry_->prev_entry_ = new_orders_at_price;
          new_orders_at_price->next_entry_ = target->next_entry_;
          target->next_entry_ = new_orders_at_price;
        } else { // add new_orders_at_price before target.
          new_orders_at_price->prev_entry_ = target->prev_entry_;
          new_orders_at_price->next_entry_ = target;
          target->prev_entry_->next_entry_ = new_orders_at_price;
          target->prev_entry_ = new_orders_at_price;

          if ((new_orders_at_price->side_ == Side::BUY && new_orders_at_price->price_ > best_orders_by_price->price_) ||
              (new_orders_at_price->side_ == Side::SELL &&
               new_orders_at_price->price_ < best_orders_by_price->price_)) {
            target->next_entry_ = (target->next_entry_ == best_orders_by_price ? new_orders_at_price
                                                                               : target->next_entry_);
            (new_orders_at_price->side_ == Side::BUY ? bids_by_price_ : asks_by_price_) = new_orders_at_price;
          }
        }
      }
    }

    /// Remove the MarketOrdersAtPrice from the containers - the hash map and the doubly linked list of price levels.
    auto removeOrdersAtPrice(Side side, Price price) noexcept {
      const auto best_orders_by_price = (side == Side::BUY ? bids_by_price_ : asks_by_price_);
      auto orders_at_price = getOrdersAtPrice(price);

      if (UNLIKELY(orders_at_price->next_entry_ == orders_at_price)) { // empty side of book.
        (side == Side::BUY ? bids_by_price_ : asks_by_price_) = nullptr;
      } else {
        orders_at_price->prev_entry_->next_entry_ = orders_at_price->next_entry_;
        orders_at_price->next_entry_->prev_entry_ = orders_at_price->prev_entry_;

        if (orders_at_price == best_orders_by_price) {
          (side == Side::BUY ? bids_by_price_ : asks_by_price_) = orders_at_price->next_entry_;
        }

        orders_at_price->prev_entry_ = orders_at_price->next_entry_ = nullptr;
      }

      price_orders_at_price_.at(priceToIndex(price)) = nullptr;

      orders_at_price_pool_.deallocate(orders_at_price);
    }

    /// Remove and de-allocate provided order from the containers.
    auto removeOrder(MarketOrder *order) noexcept -> void {
      auto orders_at_price = getOrdersAtPrice(order->price_);

      if (order->prev_order_ == order) { // only one element.
        removeOrdersAtPrice(order->side_, order->price_);
      } else { // remove the link.
        const auto order_before = order->prev_order_;
        const auto order_after = order->next_order_;
        order_before->next_order_ = order_after;
        order_after->prev_order_ = order_before;

        if (orders_at_price->first_mkt_order_ == order) {
          orders_at_price->first_mkt_order_ = order_after;
        }

        order->prev_order_ = order->next_order_ = nullptr;
      }

      oid_to_order_.at(order->order_id_) = nullptr;
      order_pool_.deallocate(order);
    }

    /// Add a single order at the end of the FIFO queue at the price level that this order belongs in.
    auto addOrder(MarketOrder *order) noexcept -> void {
      const auto orders_at_price = getOrdersAtPrice(order->price_);

      if (!orders_at_price) {
        order->next_order_ = order->prev_order_ = order;

        auto new_orders_at_price = orders_at_price_pool_.allocate(order->side_, order->price_, order, nullptr, nullptr);
        addOrdersAtPrice(new_orders_at_price);
      } else {
        auto first_order = (orders_at_price ? orders_at_price->first_mkt_order_ : nullptr);

        first_order->prev_order_->next_order_ = order;
        order->prev_order_ = first_order->prev_order_;
        order->next_order_ = first_order;
        first_order->prev_order_ = order;
      }

      oid_to_order_.at(order->order_id_) = order;
    }
  };

  /// Hash map from TickerId -> MarketOrderBook.
  typedef std::array<MarketOrderBook *, ME_MAX_TICKERS> MarketOrderBookHashMap;
}
