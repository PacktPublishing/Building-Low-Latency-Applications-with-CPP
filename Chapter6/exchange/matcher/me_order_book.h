#pragma once

#include <unordered_map>
#include <map>
#include <deque>

#include "common/types.h"
#include "common/mem_pool.h"
#include "order_server/client_response.h"
#include "market_data/market_update.h"

#include "me_order.h"

using namespace Common;

namespace Exchange {
  class MatchingEngine;

  class MEOrderBook final {
  public:
    explicit MEOrderBook(TickerId ticker_id, Logger &logger, MatchingEngine *matching_engine);

    ~MEOrderBook();

    auto add(ClientId client_id, OrderId client_order_id, TickerId ticker_id, Side side, Price price, Qty qty) noexcept -> void;

    auto cancel(ClientId client_id, OrderId order_id, TickerId ticker_id) noexcept -> void;

    auto toString(bool detailed, bool validity_check) const -> std::string;

    // Deleted default, copy & move constructors and assignment-operators.
    MEOrderBook() = delete;

    MEOrderBook(const MEOrderBook &) = delete;

    MEOrderBook(const MEOrderBook &&) = delete;

    MEOrderBook &operator=(const MEOrderBook &) = delete;

    MEOrderBook &operator=(const MEOrderBook &&) = delete;

  private:
    TickerId ticker_id_ = TickerId_INVALID;

    MatchingEngine *matching_engine_ = nullptr;

    typedef std::array<MEOrder *, ME_MAX_ORDER_IDS> OrderHashMap;
    typedef std::array<OrderHashMap, ME_MAX_NUM_CLIENTS> ClientOrderHashMap;
    ClientOrderHashMap cid_oid_to_order_;

    struct MEOrdersAtPrice {
      Side side_ = Side::INVALID;
      Price price_ = Price_INVALID;

      MEOrder *first_me_order_ = nullptr;

      MEOrdersAtPrice *prev_entry_ = nullptr;
      MEOrdersAtPrice *next_entry_ = nullptr;

      MEOrdersAtPrice() = default;

      MEOrdersAtPrice(Side side, Price price, MEOrder *first_me_order, MEOrdersAtPrice *prev_entry, MEOrdersAtPrice *next_entry)
          : side_(side), price_(price), first_me_order_(first_me_order), prev_entry_(prev_entry), next_entry_(next_entry) {}

      auto toString() const {
        std::stringstream ss;
        ss << "MEOrdersAtPrice["
           << "side:" << sideToString(side_) << " "
           << "price:" << priceToString(price_) << " "
           << "first_me_order:" << (first_me_order_ ? first_me_order_->toString() : "null") << " "
           << "prev:" << priceToString(prev_entry_ ? prev_entry_->price_ : Price_INVALID) << " "
           << "next:" << priceToString(next_entry_ ? next_entry_->price_ : Price_INVALID) << "]";

        return ss.str();
      }
    };

    MemPool<MEOrdersAtPrice> orders_at_price_pool_;
    MEOrdersAtPrice *bids_by_price_ = nullptr;
    MEOrdersAtPrice *asks_by_price_ = nullptr;

    typedef std::array<MEOrdersAtPrice *, ME_MAX_PRICE_LEVELS> OrdersAtPriceHashMap;
    OrdersAtPriceHashMap price_orders_at_price_;

    MemPool<MEOrder> order_pool_;

    MEClientResponse client_response_;
    MEMarketUpdate market_update_;

    OrderId next_market_order_id_ = 1;

    std::string time_str_;
    Logger &logger_;

  private:
    auto generateNewMarketOrderId() noexcept -> OrderId {
      return next_market_order_id_++;
    }

    auto priceToIndex(Price price) const noexcept {
      return (price % ME_MAX_PRICE_LEVELS);
    }

    auto getOrdersAtPrice(Side side, Price price) const noexcept -> MEOrdersAtPrice * {
      auto orders_at_price = price_orders_at_price_.at(priceToIndex(price));
      if (orders_at_price)
        ASSERT(orders_at_price->side_ == side, "OrdersAtPrice:" + orders_at_price->toString() + " does not match side:" + sideToString(side));
      return orders_at_price;
    }

    auto addOrdersAtPrice(MEOrdersAtPrice *new_orders_at_price) noexcept {
      const auto existing_orders_at_price = getOrdersAtPrice(new_orders_at_price->side_, new_orders_at_price->price_);
      ASSERT(existing_orders_at_price == nullptr,
             "Tried to set a new OrdersAtPrice when one already exists:" + (existing_orders_at_price ? existing_orders_at_price->toString() : ""));

      price_orders_at_price_.at(priceToIndex(new_orders_at_price->price_)) = new_orders_at_price;

      auto best_orders_by_price = (new_orders_at_price->side_ == Side::BUY ? bids_by_price_ : asks_by_price_);
      if (!best_orders_by_price) {
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

      ASSERT(getOrdersAtPrice(new_orders_at_price->side_, new_orders_at_price->price_) == new_orders_at_price,
             "Adding new OrdersAtPrice to containers failed for:" + new_orders_at_price->toString());
    }

    auto removeOrdersAtPrice(Side side, Price price) noexcept {
      auto best_orders_by_price = (side == Side::BUY ? bids_by_price_ : asks_by_price_);
      auto orders_at_price = getOrdersAtPrice(side, price);
      ASSERT(orders_at_price, "Did not find OrdersAtPrice for side:" + sideToString(side) + " price:" + priceToString(price));

      if (orders_at_price->next_entry_ == orders_at_price) { // empty side of book.
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

    auto getNextPriority(TickerId, Side side, Price price) noexcept {
      ASSERT(side == Side::BUY || side == Side::SELL, "Side needs to be buy/sell, passed:" + sideToString(side));

      const auto orders_at_price = getOrdersAtPrice(side, price);
      if (!orders_at_price)
        return 1lu;

      ASSERT(orders_at_price->first_me_order_ != nullptr, "First MEOrders is null for non-null OrdersAtPrice:" + orders_at_price->toString());

      return orders_at_price->first_me_order_->prev_order_->priority_ + 1;
    }

    auto
    checkForMatch(ClientId client_id, OrderId client_order_id, TickerId ticker_id, Side side, Price price, Qty qty, Qty new_market_order_id) noexcept;

    auto removeOrder(MEOrder *order) noexcept {
      ASSERT(order->side_ == Side::BUY || order->side_ == Side::SELL, "Invalid side on:" + order->toString());
      auto orders_at_price = getOrdersAtPrice(order->side_, order->price_);
      ASSERT(orders_at_price != nullptr, "Could not find an OrdersAtPrice for:" + order->toString());

      ASSERT(order->prev_order_->price_ == order->price_ && order->next_order_->price_ == order->price_,
             "Do not have an entry at the right price for order:" + order->toString());
      ASSERT(order->prev_order_->next_order_ == order && order->next_order_->prev_order_ == order,
             "Do not have an entry at the right priority for order:" + order->toString());

      if (order->prev_order_ == order) { // only one element.
        ASSERT(order->next_order_ == order, "Expected both prev and next pointers to point to self for:" + order->toString());

        removeOrdersAtPrice(order->side_, order->price_);
      } else { // remove the link.
        auto order_before = order->prev_order_;
        auto order_after = order->next_order_;
        order_before->next_order_ = order_after;
        order_after->prev_order_ = order_before;

        if (orders_at_price->first_me_order_ == order) {
          orders_at_price->first_me_order_ = order_after;
        }

        order->prev_order_ = order->next_order_ = nullptr;
      }

      orders_at_price = getOrdersAtPrice(order->side_, order->price_);
      if (orders_at_price) {
        auto first_order = orders_at_price->first_me_order_;
        for (auto o_itr = first_order;; o_itr = o_itr->next_order_) {
          ASSERT(o_itr->market_order_id_ != order->market_order_id_,
                 "Somehow still in bids/asks order:" + order->toString() + " entry:" + o_itr->toString());

          if (o_itr->next_order_ == first_order)
            break;
        }
      }

      ASSERT(cid_oid_to_order_.at(order->client_id_)[order->client_order_id_] != nullptr,
             "Expecting an entry in cid_oid_order map for:" + order->toString());
      cid_oid_to_order_.at(order->client_id_)[order->client_order_id_] = nullptr;
      order_pool_.deallocate(order);
    }

    auto addOrder(MEOrder *order) noexcept {
      ASSERT(order->side_ == Side::BUY || order->side_ == Side::SELL, "Invalid side on:" + order->toString());

      auto orders_at_price = getOrdersAtPrice(order->side_, order->price_);

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

      cid_oid_to_order_.at(order->client_id_)[order->client_order_id_] = order;
    }
  };
}
