#include <algorithm>

#include "me_order_book.h"

#include "matcher/matching_engine.h"

namespace Exchange {
  MEOrderBook::MEOrderBook(TickerId ticker_id, Logger &logger, MatchingEngine *matching_engine)
      : ticker_id_(ticker_id), matching_engine_(matching_engine), orders_at_price_pool_(ME_MAX_PRICE_LEVELS), order_pool_(ME_MAX_ORDER_IDS),
        logger_(logger) {
  }

  MEOrderBook::~MEOrderBook() {
    logger_.log("%:% %() % OrderBook\n%\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                toString(false, true));

    matching_engine_ = nullptr;
    bids_by_price_ = asks_by_price_ = nullptr;
    for (auto &itr: cid_oid_to_order_) {
      itr.fill(nullptr);
    }
  }

  auto MEOrderBook::checkForMatch(ClientId client_id, OrderId client_order_id, TickerId ticker_id, Side side, Price price, Qty qty,
                                  Qty new_market_order_id) noexcept {
    auto leaves_qty = qty;

    if (side == Side::BUY) {
      while (leaves_qty && asks_by_price_) {
        auto ask_itr = asks_by_price_->first_me_order_;
        if (LIKELY(price < ask_itr->price_)) {
          break;
        }

        auto order = ask_itr;
        const auto order_qty = order->qty_;
        const auto fill_qty = std::min(leaves_qty, order_qty);
        leaves_qty -= fill_qty;
        order->qty_ -= fill_qty;
        ASSERT(fill_qty > 0, "Zero fill_qty of oid:" + orderIdToString(client_order_id) + " against:" + order->toString());

        client_response_ = {ClientResponseType::FILLED, client_id, ticker_id, client_order_id,
                            new_market_order_id, side, ask_itr->price_, fill_qty, leaves_qty};
        matching_engine_->sendClientResponse(&client_response_);

        client_response_ = {ClientResponseType::FILLED, order->client_id_, ticker_id, order->client_order_id_,
                            order->market_order_id_, order->side_, ask_itr->price_, fill_qty, order->qty_};
        matching_engine_->sendClientResponse(&client_response_);

        market_update_ = {MarketUpdateType::TRADE, OrderId_INVALID, ticker_id, side, ask_itr->price_, fill_qty, Priority_INVALID};
        matching_engine_->sendMarketUpdate(&market_update_);

        if (fill_qty == order_qty) {
          market_update_ = {MarketUpdateType::CANCEL, order->market_order_id_, ticker_id, order->side_,
                            order->price_, order_qty, Priority_INVALID};
          matching_engine_->sendMarketUpdate(&market_update_);

          removeOrder(order);
        } else {
          market_update_ = {MarketUpdateType::MODIFY, order->market_order_id_, ticker_id, order->side_,
                            order->price_, order->qty_, order->priority_};
          matching_engine_->sendMarketUpdate(&market_update_);
        }
      }
    }
    if (side == Side::SELL) {
      while (leaves_qty && bids_by_price_) {
        auto bid_itr = bids_by_price_;
        if (LIKELY(price > bid_itr->price_)) {
          break;
        }

        auto order = bid_itr->first_me_order_;
        const auto order_qty = order->qty_;
        const auto fill_qty = std::min(leaves_qty, order_qty);
        ASSERT(fill_qty > 0, "Zero fill_qty of oid:" + orderIdToString(client_order_id) + " against:" + order->toString());

        leaves_qty -= fill_qty;
        order->qty_ -= fill_qty;

        client_response_ = {ClientResponseType::FILLED, client_id, ticker_id, client_order_id,
                            new_market_order_id, side, bid_itr->price_, fill_qty, leaves_qty};
        matching_engine_->sendClientResponse(&client_response_);

        client_response_ = {ClientResponseType::FILLED, order->client_id_, ticker_id, order->client_order_id_,
                            order->market_order_id_, order->side_, bid_itr->price_, fill_qty, order->qty_};
        matching_engine_->sendClientResponse(&client_response_);

        market_update_ = {MarketUpdateType::TRADE, OrderId_INVALID, ticker_id, side, bid_itr->price_, fill_qty, Priority_INVALID};
        matching_engine_->sendMarketUpdate(&market_update_);

        if (fill_qty == order_qty) {
          market_update_ = {MarketUpdateType::CANCEL, order->market_order_id_, ticker_id, order->side_,
                            order->price_, order_qty, Priority_INVALID};
          matching_engine_->sendMarketUpdate(&market_update_);

          removeOrder(order);
        } else {
          market_update_ = {MarketUpdateType::MODIFY, order->market_order_id_, ticker_id, order->side_,
                            order->price_, order->qty_, order->priority_};
          matching_engine_->sendMarketUpdate(&market_update_);
        }
      }
    }

    return leaves_qty;
  }

  auto MEOrderBook::add(ClientId client_id, OrderId client_order_id, TickerId ticker_id, Side side, Price price, Qty qty) noexcept -> void {
    const auto new_market_order_id = generateNewMarketOrderId();
    client_response_ = {ClientResponseType::ACCEPTED, client_id, ticker_id, client_order_id, new_market_order_id, side, price, 0, qty};
    matching_engine_->sendClientResponse(&client_response_);

    ASSERT(side == Side::BUY || side == Side::SELL, "Side needs to be BUY/SELL, received:" + sideToString(side));

    const auto leaves_qty = checkForMatch(client_id, client_order_id, ticker_id, side, price, qty, new_market_order_id);

    if (LIKELY(leaves_qty)) {
      const auto priority = getNextPriority(ticker_id, side, price);

      auto order = order_pool_.allocate(ticker_id, client_id, client_order_id, new_market_order_id, side, price, leaves_qty, priority, nullptr,
                                        nullptr);
      addOrder(order);

      market_update_ = {MarketUpdateType::ADD, new_market_order_id, ticker_id, side, price, leaves_qty, priority};
      matching_engine_->sendMarketUpdate(&market_update_);
    }
  }

  auto MEOrderBook::cancel(ClientId client_id, OrderId order_id, TickerId ticker_id) noexcept -> void {
    auto is_cancelable = (client_id < cid_oid_to_order_.size());
    MEOrder *exchange_order = nullptr;
    if (LIKELY(is_cancelable)) {
      auto &co_itr = cid_oid_to_order_.at(client_id);
      ASSERT(order_id < co_itr.size(), "Received oid:" + std::to_string(order_id) + " larger than MAX_ORDER_ID:" + std::to_string(ME_MAX_ORDER_IDS));
      exchange_order = co_itr.at(order_id);
      is_cancelable = (exchange_order != nullptr);
      if (is_cancelable) {
        ASSERT(exchange_order->client_id_ == client_id,
               "ClientId does not match. Expected:" + clientIdToString(client_id) + ". Got:" + clientIdToString(exchange_order->client_id_));
        ASSERT(exchange_order->ticker_id_ == ticker_id,
               "TickerId does not match. Expected:" + tickerIdToString(ticker_id) + ". Got:" + tickerIdToString(exchange_order->ticker_id_));
        ASSERT(exchange_order->client_order_id_ == order_id,
               "OrderId does not match. Expected:" + tickerIdToString(order_id) + ". Got:" + tickerIdToString(exchange_order->client_order_id_));
      }
    }

    if (UNLIKELY(!is_cancelable)) {
      client_response_ = {ClientResponseType::CANCEL_REJECTED, client_id, ticker_id, order_id, OrderId_INVALID,
                          Side::INVALID, Price_INVALID, Qty_INVALID, Qty_INVALID};
    } else {
      client_response_ = {ClientResponseType::CANCELED, client_id, ticker_id, order_id, exchange_order->market_order_id_,
                          exchange_order->side_, exchange_order->price_, Qty_INVALID, exchange_order->qty_};
      market_update_ = {MarketUpdateType::CANCEL, exchange_order->market_order_id_, ticker_id, exchange_order->side_, exchange_order->price_, 0,
                        exchange_order->priority_};

      ASSERT(exchange_order->client_id_ == client_id, "Trying to cancel different client's order.");
      ASSERT(exchange_order->client_order_id_ == order_id, "Trying to cancel different client oid.");

      removeOrder(exchange_order);

      matching_engine_->sendMarketUpdate(&market_update_);
    }

    matching_engine_->sendClientResponse(&client_response_);
  }

  auto MEOrderBook::toString(bool detailed, bool validity_check) const -> std::string {
    std::stringstream ss;
    std::string time_str;

    auto printer = [&](std::stringstream &ss, MEOrdersAtPrice *itr, Side side, Price &last_price, bool sanity_check) {
      char buf[4096];
      Qty qty = 0;
      size_t num_orders = 0;

      ASSERT(itr->first_me_order_, "FirstMEOrder should not be null:" + itr->toString());
      for (auto o_itr = itr->first_me_order_;; o_itr = o_itr->next_order_) {
        qty += o_itr->qty_;
        ++num_orders;
        if (o_itr->next_order_ == itr->first_me_order_)
          break;
      }
      sprintf(buf, " <px:%3s p:%3s n:%3s> %-3s @ %-5s(%-4s)",
              priceToString(itr->price_).c_str(), priceToString(itr->prev_entry_->price_).c_str(), priceToString(itr->next_entry_->price_).c_str(),
              priceToString(itr->price_).c_str(), qtyToString(qty).c_str(), std::to_string(num_orders).c_str());
      ss << buf;
      for (auto o_itr = itr->first_me_order_;; o_itr = o_itr->next_order_) {
        if (detailed) {
          sprintf(buf, "[oid:%s q:%s p:%s n:%s] ",
                  orderIdToString(o_itr->market_order_id_).c_str(), qtyToString(o_itr->qty_).c_str(),
                  orderIdToString(o_itr->prev_order_ ? o_itr->prev_order_->market_order_id_ : OrderId_INVALID).c_str(),
                  orderIdToString(o_itr->next_order_ ? o_itr->next_order_->market_order_id_ : OrderId_INVALID).c_str());
          ss << buf;
        }
        if (o_itr->next_order_ == itr->first_me_order_)
          break;
      }

      ss << std::endl;

      if (sanity_check) {
        if ((side == Side::SELL && last_price >= itr->price_) || (side == Side::BUY && last_price <= itr->price_)) {
          FATAL("Bids/Asks not sorted by ascending/descending prices last:" + priceToString(last_price) + " itr:" + itr->toString());
        }
        last_price = itr->price_;
      }
    };

    ss << "Ticker:" << tickerIdToString(ticker_id_) << std::endl;
    {
      auto ask_itr = asks_by_price_;
      auto last_ask_price = std::numeric_limits<Price>::min();
      for (size_t count = 0; ask_itr; ++count) {
        ss << "ASKS L:" << count << " => ";
        auto next_ask_itr = (ask_itr->next_entry_ == asks_by_price_ ? nullptr : ask_itr->next_entry_);
        printer(ss, ask_itr, Side::SELL, last_ask_price, validity_check);
        ask_itr = next_ask_itr;
      }
    }

    ss << std::endl << "                          X" << std::endl << std::endl;

    {
      auto bid_itr = bids_by_price_;
      auto last_bid_price = std::numeric_limits<Price>::max();
      for (size_t count = 0; bid_itr; ++count) {
        ss << "BIDS L:" << count << " => ";
        auto next_bid_itr = (bid_itr->next_entry_ == bids_by_price_ ? nullptr : bid_itr->next_entry_);
        printer(ss, bid_itr, Side::BUY, last_bid_price, validity_check);
        bid_itr = next_bid_itr;
      }
    }

    return ss.str();
  }
}
