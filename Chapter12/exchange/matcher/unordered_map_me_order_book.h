#pragma once

#include <unordered_map>

#include "common/types.h"
#include "common/mem_pool.h"
#include "common/logging.h"
#include "order_server/client_response.h"
#include "market_data/market_update.h"

#include "me_order.h"

using namespace Common;

namespace Exchange {
  class MatchingEngine;

  class UnorderedMapMEOrderBook final {
  public:
    explicit UnorderedMapMEOrderBook(TickerId ticker_id, Logger *logger, MatchingEngine *matching_engine);

    ~UnorderedMapMEOrderBook();

    /// Create and add a new order in the order book with provided attributes.
    /// It will check to see if this new order matches an existing passive order with opposite side, and perform the matching if that is the case.
    auto add(ClientId client_id, OrderId client_order_id, TickerId ticker_id, Side side, Price price, Qty qty) noexcept -> void;

    /// Attempt to cancel an order in the order book, issue a cancel-rejection if order does not exist.
    auto cancel(ClientId client_id, OrderId order_id, TickerId ticker_id) noexcept -> void;

    auto toString(bool detailed, bool validity_check) const -> std::string;

    /// Deleted default, copy & move constructors and assignment-operators.
    UnorderedMapMEOrderBook() = delete;

    UnorderedMapMEOrderBook(const UnorderedMapMEOrderBook &) = delete;

    UnorderedMapMEOrderBook(const UnorderedMapMEOrderBook &&) = delete;

    UnorderedMapMEOrderBook &operator=(const UnorderedMapMEOrderBook &) = delete;

    UnorderedMapMEOrderBook &operator=(const UnorderedMapMEOrderBook &&) = delete;

  private:
    TickerId ticker_id_ = TickerId_INVALID;

    /// The parent matching engine instance, used to publish market data and client responses.
    MatchingEngine *matching_engine_ = nullptr;

    /// Hash map from ClientId -> OrderId -> MEOrder.
    std::unordered_map<ClientId, std::unordered_map<OrderId, MEOrder *>> cid_oid_to_order_;

    /// Memory pool to manage MEOrdersAtPrice objects.
    MemPool<MEOrdersAtPrice> orders_at_price_pool_;

    /// Pointers to beginning / best prices / top of book of buy and sell price levels.
    MEOrdersAtPrice *bids_by_price_ = nullptr;
    MEOrdersAtPrice *asks_by_price_ = nullptr;

    /// Hash map from Price -> MEOrdersAtPrice.
    std::unordered_map<Price, MEOrdersAtPrice *> price_orders_at_price_;

    /// Memory pool to manage MEOrder objects.
    MemPool<MEOrder> order_pool_;

    /// These are used to publish client responses and market updates.
    MEClientResponse client_response_;
    MEMarketUpdate market_update_;

    OrderId next_market_order_id_ = 1;

    std::string time_str_;
    Logger *logger_ = nullptr;

  private:
    auto generateNewMarketOrderId() noexcept -> OrderId {
      return next_market_order_id_++;
    }

    auto priceToIndex(Price price) const noexcept {
      return (price % ME_MAX_PRICE_LEVELS);
    }

    /// Fetch and return the MEOrdersAtPrice corresponding to the provided price.
    auto getOrdersAtPrice(Price price) const noexcept -> MEOrdersAtPrice * {
      if(price_orders_at_price_.find(priceToIndex(price)) == price_orders_at_price_.end())
        return nullptr;

      return price_orders_at_price_.at(priceToIndex(price));
    }

    /// Add a new MEOrdersAtPrice at the correct price into the containers - the hash map and the doubly linked list of price levels.
    auto addOrdersAtPrice(MEOrdersAtPrice *new_orders_at_price) noexcept {
      price_orders_at_price_[priceToIndex(new_orders_at_price->price_)] = new_orders_at_price;

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
              (new_orders_at_price->side_ == Side::SELL && new_orders_at_price->price_ < best_orders_by_price->price_)) {
            target->next_entry_ = (target->next_entry_ == best_orders_by_price ? new_orders_at_price : target->next_entry_);
            (new_orders_at_price->side_ == Side::BUY ? bids_by_price_ : asks_by_price_) = new_orders_at_price;
          }
        }
      }
    }

    /// Remove the MEOrdersAtPrice from the containers - the hash map and the doubly linked list of price levels.
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

      price_orders_at_price_[priceToIndex(price)] = nullptr;

      orders_at_price_pool_.deallocate(orders_at_price);
    }

    auto getNextPriority(Price price) noexcept {
      const auto orders_at_price = getOrdersAtPrice(price);
      if (!orders_at_price)
        return 1lu;

      return orders_at_price->first_me_order_->prev_order_->priority_ + 1;
    }

    /// Match a new aggressive order with the provided parameters against a passive order held in the bid_itr object and generate client responses and market updates for the match.
    /// It will update the passive order (bid_itr) based on the match and possibly remove it if fully matched.
    /// It will return remaining quantity on the aggressive order in the leaves_qty parameter.
    auto match(TickerId ticker_id, ClientId client_id, Side side, OrderId client_order_id, OrderId new_market_order_id, MEOrder* bid_itr, Qty* leaves_qty) noexcept;

    /// Check if a new order with the provided attributes would match against existing passive orders on the other side of the order book.
    /// This will call the match() method to perform the match if there is a match to be made and return the quantity remaining if any on this new order.
    auto checkForMatch(ClientId client_id, OrderId client_order_id, TickerId ticker_id, Side side, Price price, Qty qty, Qty new_market_order_id) noexcept;

    /// Remove and de-allocate provided order from the containers.
    auto removeOrder(MEOrder *order) noexcept {
      auto orders_at_price = getOrdersAtPrice(order->price_);

      if (order->prev_order_ == order) { // only one element.
        removeOrdersAtPrice(order->side_, order->price_);
      } else { // remove the link.
        const auto order_before = order->prev_order_;
        const auto order_after = order->next_order_;
        order_before->next_order_ = order_after;
        order_after->prev_order_ = order_before;

        if (orders_at_price->first_me_order_ == order) {
          orders_at_price->first_me_order_ = order_after;
        }

        order->prev_order_ = order->next_order_ = nullptr;
      }

      cid_oid_to_order_[order->client_id_][order->client_order_id_] = nullptr;
      order_pool_.deallocate(order);
    }

    /// Add a single order at the end of the FIFO queue at the price level that this order belongs in.
    auto addOrder(MEOrder *order) noexcept {
      const auto orders_at_price = getOrdersAtPrice(order->price_);

      if (!orders_at_price) {
        order->next_order_ = order->prev_order_ = order;

        auto new_orders_at_price = orders_at_price_pool_.allocate(order->side_, order->price_, order, nullptr, nullptr);
        addOrdersAtPrice(new_orders_at_price);
      } else {
        auto first_order = (orders_at_price ? orders_at_price->first_me_order_ : nullptr);

        first_order->prev_order_->next_order_ = order;
        order->prev_order_ = first_order->prev_order_;
        order->next_order_ = first_order;
        first_order->prev_order_ = order;
      }

      cid_oid_to_order_[order->client_id_][order->client_order_id_] = order;
    }
  };
}
