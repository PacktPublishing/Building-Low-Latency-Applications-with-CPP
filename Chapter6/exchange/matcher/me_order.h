#pragma once

#include <cstdint>
#include <sstream>

#include "common/types.h"
#include "common/logging.h"

using namespace Common;

namespace Exchange {
  struct MEOrder final {
    TickerId ticker_id_ = TickerId_INVALID;
    ClientId client_id_ = ClientId_INVALID;
    OrderId client_order_id_ = OrderId_INVALID;
    OrderId market_order_id_ = OrderId_INVALID;
    Side side_ = Side::INVALID;
    Price price_ = Price_INVALID;
    Qty qty_ = Qty_INVALID;
    Priority priority_ = Priority_INVALID;

    MEOrder *prev_order_ = nullptr;
    MEOrder *next_order_ = nullptr;

    // only needed for use with MemPool.
    MEOrder() = default;

    MEOrder(TickerId ticker_id, ClientId client_id, OrderId client_order_id, OrderId market_order_id, Side side, Price price,
                     Qty qty, Priority priority, MEOrder *prev_order, MEOrder *next_order) noexcept
        : ticker_id_(ticker_id), client_id_(client_id), client_order_id_(client_order_id), market_order_id_(market_order_id), side_(side),
          price_(price), qty_(qty), priority_(priority), prev_order_(prev_order), next_order_(next_order) {}

    auto toString() const -> std::string;
  };
}
