#include "market_data_consumer.h"

namespace Trading {
  MarketDataConsumer::MarketDataConsumer(Common::ClientId client_id, Exchange::MEMarketUpdateLFQueue *market_updates,
                                         const std::string &iface,
                                         const std::string &snapshot_ip, int snapshot_port,
                                         const std::string &incremental_ip, int incremental_port)
      : incoming_md_updates_(market_updates), run_(false),
        logger_("trading_market_data_consumer_" + std::to_string(client_id) + ".log"),
        incremental_mcast_socket_(logger_), snapshot_mcast_socket_(logger_),
        iface_(iface), snapshot_ip_(snapshot_ip), snapshot_port_(snapshot_port) {
    auto recv_callback = [this](auto socket) {
      recvCallback(socket);
    };

    incremental_mcast_socket_.recv_callback_ = recv_callback;
    ASSERT(incremental_mcast_socket_.init(incremental_ip, iface, incremental_port, /*is_listening*/ true) >= 0,
           "Unable to create incremental mcast socket. error:" + std::string(std::strerror(errno)));

    ASSERT(incremental_mcast_socket_.join(incremental_ip),
           "Join failed on:" + std::to_string(incremental_mcast_socket_.socket_fd_) + " error:" + std::string(std::strerror(errno)));

    snapshot_mcast_socket_.recv_callback_ = recv_callback;
  }

  /// Main loop for this thread - reads and processes messages from the multicast sockets - the heavy lifting is in the recvCallback() and checkSnapshotSync() methods.
  auto MarketDataConsumer::run() noexcept -> void {
    logger_.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
    while (run_) {
      incremental_mcast_socket_.sendAndRecv();
      snapshot_mcast_socket_.sendAndRecv();
    }
  }

  /// Start the process of snapshot synchronization by subscribing to the snapshot multicast stream.
  auto MarketDataConsumer::startSnapshotSync() -> void {
    snapshot_queued_msgs_.clear();
    incremental_queued_msgs_.clear();

    ASSERT(snapshot_mcast_socket_.init(snapshot_ip_, iface_, snapshot_port_, /*is_listening*/ true) >= 0,
           "Unable to create snapshot mcast socket. error:" + std::string(std::strerror(errno)));
    ASSERT(snapshot_mcast_socket_.join(snapshot_ip_), // IGMP multicast subscription.
           "Join failed on:" + std::to_string(snapshot_mcast_socket_.socket_fd_) + " error:" + std::string(std::strerror(errno)));
  }

  /// Check if a recovery / synchronization is possible from the queued up market data updates from the snapshot and incremental market data streams.
  auto MarketDataConsumer::checkSnapshotSync() -> void {
    if (snapshot_queued_msgs_.empty()) {
      return;
    }

    const auto &first_snapshot_msg = snapshot_queued_msgs_.begin()->second;
    if (first_snapshot_msg.type_ != Exchange::MarketUpdateType::SNAPSHOT_START) {
      logger_.log("%:% %() % Returning because have not seen a SNAPSHOT_START yet.\n",
                  __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
      snapshot_queued_msgs_.clear();
      return;
    }

    std::vector<Exchange::MEMarketUpdate> final_events;

    auto have_complete_snapshot = true;
    size_t next_snapshot_seq = 0;
    for (auto &snapshot_itr: snapshot_queued_msgs_) {
      logger_.log("%:% %() % % => %\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), snapshot_itr.first, snapshot_itr.second.toString());
      if (snapshot_itr.first != next_snapshot_seq) {
        have_complete_snapshot = false;
        logger_.log("%:% %() % Detected gap in snapshot stream expected:% found:% %.\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), next_snapshot_seq, snapshot_itr.first, snapshot_itr.second.toString());
        break;
      }

      if (snapshot_itr.second.type_ != Exchange::MarketUpdateType::SNAPSHOT_START &&
          snapshot_itr.second.type_ != Exchange::MarketUpdateType::SNAPSHOT_END)
        final_events.push_back(snapshot_itr.second);

      ++next_snapshot_seq;
    }

    if (!have_complete_snapshot) {
      logger_.log("%:% %() % Returning because found gaps in snapshot stream.\n",
                  __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
      snapshot_queued_msgs_.clear();
      return;
    }

    const auto &last_snapshot_msg = snapshot_queued_msgs_.rbegin()->second;
    if (last_snapshot_msg.type_ != Exchange::MarketUpdateType::SNAPSHOT_END) {
      logger_.log("%:% %() % Returning because have not seen a SNAPSHOT_END yet.\n",
                  __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
      return;
    }

    auto have_complete_incremental = true;
    size_t num_incrementals = 0;
    next_exp_inc_seq_num_ = last_snapshot_msg.order_id_ + 1;
    for (auto inc_itr = incremental_queued_msgs_.begin(); inc_itr != incremental_queued_msgs_.end(); ++inc_itr) {
      logger_.log("%:% %() % Checking next_exp:% vs. seq:% %.\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), next_exp_inc_seq_num_, inc_itr->first, inc_itr->second.toString());

      if (inc_itr->first < next_exp_inc_seq_num_)
        continue;

      if (inc_itr->first != next_exp_inc_seq_num_) {
        logger_.log("%:% %() % Detected gap in incremental stream expected:% found:% %.\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), next_exp_inc_seq_num_, inc_itr->first, inc_itr->second.toString());
        have_complete_incremental = false;
        break;
      }

      logger_.log("%:% %() % % => %\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), inc_itr->first, inc_itr->second.toString());

      if (inc_itr->second.type_ != Exchange::MarketUpdateType::SNAPSHOT_START &&
          inc_itr->second.type_ != Exchange::MarketUpdateType::SNAPSHOT_END)
        final_events.push_back(inc_itr->second);

      ++next_exp_inc_seq_num_;
      ++num_incrementals;
    }

    if (!have_complete_incremental) {
      logger_.log("%:% %() % Returning because have gaps in queued incrementals.\n",
                  __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
      snapshot_queued_msgs_.clear();
      return;
    }

    for (const auto &itr: final_events) {
      auto next_write = incoming_md_updates_->getNextToWriteTo();
      *next_write = itr;
      incoming_md_updates_->updateWriteIndex();
    }

    logger_.log("%:% %() % Recovered % snapshot and % incremental orders.\n", __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str_), snapshot_queued_msgs_.size() - 2, num_incrementals);

    snapshot_queued_msgs_.clear();
    incremental_queued_msgs_.clear();
    in_recovery_ = false;

    snapshot_mcast_socket_.leave(snapshot_ip_, snapshot_port_);;
  }

  /// Queue up a message in the *_queued_msgs_ containers, first parameter specifies if this update came from the snapshot or the incremental streams.
  auto MarketDataConsumer::queueMessage(bool is_snapshot, const Exchange::MDPMarketUpdate *request) {
    if (is_snapshot) {
      if (snapshot_queued_msgs_.find(request->seq_num_) != snapshot_queued_msgs_.end()) {
        logger_.log("%:% %() % Packet drops on snapshot socket. Received for a 2nd time:%\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), request->toString());
        snapshot_queued_msgs_.clear();
      }
      snapshot_queued_msgs_[request->seq_num_] = request->me_market_update_;
    } else {
      incremental_queued_msgs_[request->seq_num_] = request->me_market_update_;
    }

    logger_.log("%:% %() % size snapshot:% incremental:% % => %\n", __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str_), snapshot_queued_msgs_.size(), incremental_queued_msgs_.size(), request->seq_num_, request->toString());

    checkSnapshotSync();
  }

  /// Process a market data update, the consumer needs to use the socket parameter to figure out whether this came from the snapshot or the incremental stream.
  auto MarketDataConsumer::recvCallback(McastSocket *socket) noexcept -> void {
    TTT_MEASURE(T7_MarketDataConsumer_UDP_read, logger_);

    START_MEASURE(Trading_MarketDataConsumer_recvCallback);
    const auto is_snapshot = (socket->socket_fd_ == snapshot_mcast_socket_.socket_fd_);
    if (UNLIKELY(is_snapshot && !in_recovery_)) { // market update was read from the snapshot market data stream and we are not in recovery, so we dont need it and discard it.
      socket->next_rcv_valid_index_ = 0;

      logger_.log("%:% %() % WARN Not expecting snapshot messages.\n",
                  __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));

      return;
    }

    if (socket->next_rcv_valid_index_ >= sizeof(Exchange::MDPMarketUpdate)) {
      size_t i = 0;
      for (; i + sizeof(Exchange::MDPMarketUpdate) <= socket->next_rcv_valid_index_; i += sizeof(Exchange::MDPMarketUpdate)) {
        auto request = reinterpret_cast<const Exchange::MDPMarketUpdate *>(socket->inbound_data_.data() + i);
        logger_.log("%:% %() % Received % socket len:% %\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_),
                    (is_snapshot ? "snapshot" : "incremental"), sizeof(Exchange::MDPMarketUpdate), request->toString());

        const bool already_in_recovery = in_recovery_;
        in_recovery_ = (already_in_recovery || request->seq_num_ != next_exp_inc_seq_num_);

        if (UNLIKELY(in_recovery_)) {
          if (UNLIKELY(!already_in_recovery)) { // if we just entered recovery, start the snapshot synchonization process by subscribing to the snapshot multicast stream.
            logger_.log("%:% %() % Packet drops on % socket. SeqNum expected:% received:%\n", __FILE__, __LINE__, __FUNCTION__,
                        Common::getCurrentTimeStr(&time_str_), (is_snapshot ? "snapshot" : "incremental"), next_exp_inc_seq_num_, request->seq_num_);
            startSnapshotSync();
          }

          queueMessage(is_snapshot, request); // queue up the market data update message and check if snapshot recovery / synchronization can be completed successfully.
        } else if (!is_snapshot) { // not in recovery and received a packet in the correct order and without gaps, process it.
          logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), request->toString());

          ++next_exp_inc_seq_num_;

          auto next_write = incoming_md_updates_->getNextToWriteTo();
          *next_write = std::move(request->me_market_update_);
          incoming_md_updates_->updateWriteIndex();
          TTT_MEASURE(T8_MarketDataConsumer_LFQueue_write, logger_);
        }
      }
      memcpy(socket->inbound_data_.data(), socket->inbound_data_.data() + i, socket->next_rcv_valid_index_ - i);
      socket->next_rcv_valid_index_ -= i;
    }
    END_MEASURE(Trading_MarketDataConsumer_recvCallback, logger_);
  }
}
