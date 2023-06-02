#pragma once

#include <functional>

#include "common/thread_utils.h"
#include "common/macros.h"
#include "common/tcp_server.h"

#include "order_server/client_request.h"
#include "order_server/client_response.h"
#include "order_server/fifo_sequencer.h"

namespace Exchange {
  class OrderServer {
  public:
    OrderServer(ClientRequestLFQueue *client_requests, ClientResponseLFQueue *client_responses, const std::string &iface, int port);

    ~OrderServer();

    auto start() -> void;

    auto stop() -> void;

    auto run() noexcept {
      logger_.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
      while (run_) {
        tcp_server_.poll();

        tcp_server_.sendAndRecv();

        for (auto client_response = outgoing_responses_->getNextToRead(); outgoing_responses_->size() && client_response; client_response = outgoing_responses_->getNextToRead()) {
          TTT_MEASURE(T5t_OrderServer_LFQueue_read, logger_);

          auto &next_outgoing_seq_num = cid_next_outgoing_seq_num_[client_response->client_id_];
          logger_.log("%:% %() % Processing cid:% seq:% %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                      client_response->client_id_, next_outgoing_seq_num, client_response->toString());

          ASSERT(cid_tcp_socket_[client_response->client_id_] != nullptr,
                 "Dont have a TCPSocket for ClientId:" + std::to_string(client_response->client_id_));
          START_MEASURE(Exchange_TCPSocket_send);
          cid_tcp_socket_[client_response->client_id_]->send(&next_outgoing_seq_num, sizeof(next_outgoing_seq_num));
          cid_tcp_socket_[client_response->client_id_]->send(client_response, sizeof(MEClientResponse));
          END_MEASURE(Exchange_TCPSocket_send, logger_);

          outgoing_responses_->updateReadIndex();
          TTT_MEASURE(T6t_OrderServer_TCP_write, logger_);

          ++next_outgoing_seq_num;
        }
      }
    }

    auto recvCallback(TCPSocket *socket, Nanos rx_time) noexcept {
      TTT_MEASURE(T1_OrderServer_TCP_read, logger_);
      logger_.log("%:% %() % Received socket:% len:% rx:%\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                  socket->fd_, socket->next_rcv_valid_index_, rx_time);

      if (socket->next_rcv_valid_index_ >= sizeof(OMClientRequest)) {
        size_t i = 0;
        for (; i + sizeof(OMClientRequest) <= socket->next_rcv_valid_index_; i += sizeof(OMClientRequest)) {
          auto request = reinterpret_cast<const OMClientRequest *>(socket->rcv_buffer_ + i);
          logger_.log("%:% %() % Received %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), request->toString());

          if (UNLIKELY(cid_tcp_socket_[request->me_client_request_.client_id_] == nullptr)) {
            cid_tcp_socket_[request->me_client_request_.client_id_] = socket;
          }

          if (cid_tcp_socket_[request->me_client_request_.client_id_] != socket) {
            logger_.log("%:% %() % Received ClientRequest from ClientId:% on different socket:% expected:%\n", __FILE__, __LINE__, __FUNCTION__,
                        Common::getCurrentTimeStr(&time_str_), request->me_client_request_.client_id_, socket->fd_,
                        cid_tcp_socket_[request->me_client_request_.client_id_]->fd_);
            continue;
          }

          auto &next_exp_seq_num = cid_next_exp_seq_num_[request->me_client_request_.client_id_];
          if (request->seq_num_ != next_exp_seq_num) {
            logger_.log("%:% %() % Incorrect sequence number. ClientId:% SeqNum expected:% received:%\n", __FILE__, __LINE__, __FUNCTION__,
                        Common::getCurrentTimeStr(&time_str_), request->me_client_request_.client_id_, next_exp_seq_num, request->seq_num_);
            continue;
          }

          ++next_exp_seq_num;

          START_MEASURE(Exchange_FIFOSequencer_addClientRequest);
          fifo_sequencer_.addClientRequest(rx_time, request->me_client_request_);
          END_MEASURE(Exchange_FIFOSequencer_addClientRequest, logger_);
        }
        memcpy(socket->rcv_buffer_, socket->rcv_buffer_ + i, socket->next_rcv_valid_index_ - i);
        socket->next_rcv_valid_index_ -= i;
      }
    }

    auto recvFinishedCallback() noexcept {
      START_MEASURE(Exchange_FIFOSequencer_sequenceAndPublish);
      fifo_sequencer_.sequenceAndPublish();
      END_MEASURE(Exchange_FIFOSequencer_sequenceAndPublish, logger_);
    }

    // Deleted default, copy & move constructors and assignment-operators.
    OrderServer() = delete;

    OrderServer(const OrderServer &) = delete;

    OrderServer(const OrderServer &&) = delete;

    OrderServer &operator=(const OrderServer &) = delete;

    OrderServer &operator=(const OrderServer &&) = delete;

  private:
    const std::string iface_;
    const int port_ = 0;

    ClientResponseLFQueue *outgoing_responses_ = nullptr;

    volatile bool run_ = false;

    std::string time_str_;
    Logger logger_;

    std::array<size_t, ME_MAX_NUM_CLIENTS> cid_next_outgoing_seq_num_;
    std::array<size_t, ME_MAX_NUM_CLIENTS> cid_next_exp_seq_num_;
    std::array<Common::TCPSocket *, ME_MAX_NUM_CLIENTS> cid_tcp_socket_;

    Common::TCPServer tcp_server_;

    FIFOSequencer fifo_sequencer_;
  };
}
