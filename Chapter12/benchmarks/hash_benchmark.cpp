#include "matcher/matching_engine.h"
#include "matcher/unordered_map_me_order_book.h"

static constexpr size_t loop_count = 100000;

template<typename T>
size_t benchmarkHashMap(T *order_book, const std::vector<Exchange::MEClientRequest>& client_requests) {
  size_t total_rdtsc = 0;

  for (size_t i = 0; i < loop_count; ++i) {
    const auto& client_request = client_requests[i];
    switch (client_request.type_) {
      case Exchange::ClientRequestType::NEW: {
        const auto start = Common::rdtsc();
        order_book->add(client_request.client_id_, client_request.order_id_, client_request.ticker_id_,
                        client_request.side_, client_request.price_, client_request.qty_);
        total_rdtsc += (Common::rdtsc() - start);
      }
        break;

      case Exchange::ClientRequestType::CANCEL: {
        const auto start = Common::rdtsc();
        order_book->cancel(client_request.client_id_, client_request.order_id_, client_request.ticker_id_);
        total_rdtsc += (Common::rdtsc() - start);
      }
        break;

      default:
        break;
    }
  }

  return (total_rdtsc / (loop_count * 2));
}

int main(int, char **) {
  srand(0);

  Common::Logger logger("hash_benchmark.log");
  Exchange::ClientRequestLFQueue client_requests(ME_MAX_CLIENT_UPDATES);
  Exchange::ClientResponseLFQueue client_responses(ME_MAX_CLIENT_UPDATES);
  Exchange::MEMarketUpdateLFQueue market_updates(ME_MAX_MARKET_UPDATES);
  auto matching_engine = new Exchange::MatchingEngine(&client_requests, &client_responses, &market_updates);

  Common::OrderId order_id = 1000;
  std::vector<Exchange::MEClientRequest> client_requests_vec;
  Price base_price = (rand() % 100) + 100;
  while (client_requests_vec.size() < loop_count) {
    const Price price = base_price + (rand() % 10) + 1;
    const Qty qty = 1 + (rand() % 100) + 1;
    const Side side = (rand() % 2 ? Common::Side::BUY : Common::Side::SELL);

    Exchange::MEClientRequest new_request{Exchange::ClientRequestType::NEW, 0, 0, order_id++, side, price, qty};
    client_requests_vec.push_back(new_request);

    const auto cxl_index = rand() % client_requests_vec.size();
    auto cxl_request = client_requests_vec[cxl_index];
    cxl_request.type_ = Exchange::ClientRequestType::CANCEL;

    client_requests_vec.push_back(cxl_request);
  }

  {
    auto me_order_book = new Exchange::MEOrderBook(0, &logger, matching_engine);
    const auto cycles = benchmarkHashMap(me_order_book, client_requests_vec);
    std::cout << "ARRAY HASHMAP " << cycles << " CLOCK CYCLES PER OPERATION." << std::endl;
  }

  {
    auto me_order_book = new Exchange::UnorderedMapMEOrderBook(0, &logger, matching_engine);
    const auto cycles = benchmarkHashMap(me_order_book, client_requests_vec);
    std::cout << "UNORDERED-MAP HASHMAP " << cycles << " CLOCK CYCLES PER OPERATION." << std::endl;
  }

  exit(EXIT_SUCCESS);
}
