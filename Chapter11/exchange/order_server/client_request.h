#pragma once

#include <sstream>

#include "common/types.h"
#include "common/lf_queue.h"

using namespace Common;

namespace Exchange {
  /// Type of the order request sent by the trading client to the exchange.
  enum class ClientRequestType : uint8_t {
    INVALID = 0,
    NEW = 1,
    CANCEL = 2
  };

  inline std::string clientRequestTypeToString(ClientRequestType type) {
    switch (type) {
      case ClientRequestType::NEW:
        return "NEW";
      case ClientRequestType::CANCEL:
        return "CANCEL";
      case ClientRequestType::INVALID:
        return "INVALID";
    }
    return "UNKNOWN";
  }

  /// These structures go over the wire / network, so the binary structures are packed to remove system dependent extra padding.
#pragma pack(push, 1)

  /// Client request structure used internally by the matching engine.
  struct MEClientRequest {
    ClientRequestType type_ = ClientRequestType::INVALID;

    ClientId client_id_ = ClientId_INVALID;
    TickerId ticker_id_ = TickerId_INVALID;
    OrderId order_id_ = OrderId_INVALID;
    Side side_ = Side::INVALID;
    Price price_ = Price_INVALID;
    Qty qty_ = Qty_INVALID;

    auto toString() const {
      std::stringstream ss;
      ss << "MEClientRequest"
         << " ["
         << "type:" << clientRequestTypeToString(type_)
         << " client:" << clientIdToString(client_id_)
         << " ticker:" << tickerIdToString(ticker_id_)
         << " oid:" << orderIdToString(order_id_)
         << " side:" << sideToString(side_)
         << " qty:" << qtyToString(qty_)
         << " price:" << priceToString(price_)
         << "]";
      return ss.str();
    }
  };

  /// Client request structure published over the network by the order gateway client.
  struct OMClientRequest {
    size_t seq_num_ = 0;
    MEClientRequest me_client_request_;

    auto toString() const {
      std::stringstream ss;
      ss << "OMClientRequest"
         << " ["
         << "seq:" << seq_num_
         << " " << me_client_request_.toString()
         << "]";
      return ss.str();
    }
  };

#pragma pack(pop) // Undo the packed binary structure directive moving forward.

  /// Lock free queues of matching engine client order request messages.
  typedef LFQueue<MEClientRequest> ClientRequestLFQueue;
}
