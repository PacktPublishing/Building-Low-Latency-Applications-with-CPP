#pragma once

#include "common/thread_utils.h"
#include "common/macros.h"

#include "order_server/client_request.h"

namespace Exchange {
  /// Maximum number of unprocessed client request messages across all TCP connections in the order server / FIFO sequencer.
  constexpr size_t ME_MAX_PENDING_REQUESTS = 1024;

  class FIFOSequencer {
  public:
    FIFOSequencer(ClientRequestLFQueue *client_requests, Logger *logger)
        : incoming_requests_(client_requests), logger_(logger) {
    }

    ~FIFOSequencer() {
    }

    /// Queue up a client request, not processed immediately, processed when sequenceAndPublish() is called.
    auto addClientRequest(Nanos rx_time, const MEClientRequest &request) {
      if (pending_size_ >= pending_client_requests_.size()) {
        FATAL("Too many pending requests");
      }
      pending_client_requests_.at(pending_size_++) = std::move(RecvTimeClientRequest{rx_time, request});
    }

    /// Sort pending client requests in ascending receive time order and then write them to the lock free queue for the matching engine to consume from.
    auto sequenceAndPublish() {
      if (UNLIKELY(!pending_size_))
        return;

      logger_->log("%:% %() % Processing % requests.\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), pending_size_);

      std::sort(pending_client_requests_.begin(), pending_client_requests_.begin() + pending_size_);

      for (size_t i = 0; i < pending_size_; ++i) {
        const auto &client_request = pending_client_requests_.at(i);

        logger_->log("%:% %() % Writing RX:% Req:% to FIFO.\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                     client_request.recv_time_, client_request.request_.toString());

        auto next_write = incoming_requests_->getNextToWriteTo();
        *next_write = std::move(client_request.request_);
        incoming_requests_->updateWriteIndex();
        TTT_MEASURE(T2_OrderServer_LFQueue_write, (*logger_));
      }

      pending_size_ = 0;
    }

    /// Deleted default, copy & move constructors and assignment-operators.
    FIFOSequencer() = delete;

    FIFOSequencer(const FIFOSequencer &) = delete;

    FIFOSequencer(const FIFOSequencer &&) = delete;

    FIFOSequencer &operator=(const FIFOSequencer &) = delete;

    FIFOSequencer &operator=(const FIFOSequencer &&) = delete;

  private:
    /// Lock free queue used to publish client requests to, so that the matching engine can consume them.
    ClientRequestLFQueue *incoming_requests_ = nullptr;

    std::string time_str_;
    Logger *logger_ = nullptr;

    /// A structure that encapsulates the software receive time as well as the client request.
    struct RecvTimeClientRequest {
      Nanos recv_time_ = 0;
      MEClientRequest request_;

      auto operator<(const RecvTimeClientRequest &rhs) const {
        return (recv_time_ < rhs.recv_time_);
      }
    };

    /// Queue of pending client requests, not sorted.
    std::array<RecvTimeClientRequest, ME_MAX_PENDING_REQUESTS> pending_client_requests_;
    size_t pending_size_ = 0;
  };
}
