#include <csignal>

#include "matcher/matching_engine.h"

Common::Logger* logger = nullptr;
Exchange::MatchingEngine* matching_engine = nullptr;

void signal_handler(int) {
  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(10s);

  delete logger; logger = nullptr;
  delete matching_engine; matching_engine = nullptr;

  std::this_thread::sleep_for(10s);

  exit(EXIT_SUCCESS);
}

int main(int, char **) {
  logger = new Common::Logger("exchange_main.log");

  std::signal(SIGINT, signal_handler);

  const int sleep_time = 100 * 1000;

  Exchange::ClientRequestLFQueue client_requests(ME_MAX_CLIENT_UPDATES);
  Exchange::ClientResponseLFQueue client_responses(ME_MAX_CLIENT_UPDATES);
  Exchange::MEMarketUpdateLFQueue market_updates(ME_MAX_MARKET_UPDATES);

  std::string time_str;

  logger->log("%:% %() % Starting Matching Engine...\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
  matching_engine = new Exchange::MatchingEngine(&client_requests, &client_responses, &market_updates);
  matching_engine->start();

  while (true) {
    logger->log("%:% %() % Sleeping for a few milliseconds..\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
    usleep(sleep_time * 1000);
  }
}
