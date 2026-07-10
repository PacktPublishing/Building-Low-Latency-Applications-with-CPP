// Standalone unit tests for Exchange::MEOrderBook.
//
// MEOrderBook publishes its results (client responses + market updates) back
// through its parent MatchingEngine into two lock-free queues. We construct a
// real MatchingEngine (but never call start(), so everything runs on this
// thread), drive orders through MatchingEngine::processClientRequest(), and
// drain the output queues to assert on the observable behavior.
//
// Build/run: see tests/run_tests.sh (or the command at the bottom of this file).

#include <cassert>
#include <iostream>
#include <vector>
#include <string>

#include "matcher/matching_engine.h"

using namespace Common;
using namespace Exchange;

namespace {
  int g_checks = 0;
  int g_failures = 0;

  #define CHECK(cond, msg)                                                        \
    do {                                                                          \
      ++g_checks;                                                                 \
      if (!(cond)) {                                                              \
        ++g_failures;                                                            \
        std::cerr << "  [FAIL] " << (msg) << "  (" << #cond << ") @ line "        \
                  << __LINE__ << "\n";                                            \
      }                                                                           \
    } while (0)

  /// Test harness that owns the queues + matching engine and exposes helpers to
  /// submit requests and drain the resulting responses / market updates.
  struct Harness {
    ClientRequestLFQueue  requests{ME_MAX_CLIENT_UPDATES};
    ClientResponseLFQueue responses{ME_MAX_CLIENT_UPDATES};
    MEMarketUpdateLFQueue market_updates{ME_MAX_MARKET_UPDATES};
    MatchingEngine engine{&requests, &responses, &market_updates};

    /// Submit a NEW order request straight into the matching engine.
    auto add(ClientId cid, OrderId oid, TickerId ticker, Side side, Price px, Qty qty) {
      MEClientRequest req{ClientRequestType::NEW, cid, ticker, oid, side, px, qty};
      engine.processClientRequest(&req);
    }

    /// Submit a CANCEL request straight into the matching engine.
    auto cancel(ClientId cid, OrderId oid, TickerId ticker) {
      MEClientRequest req{ClientRequestType::CANCEL, cid, ticker, oid,
                          Side::INVALID, Price_INVALID, Qty_INVALID};
      engine.processClientRequest(&req);
    }

    /// Drain and return all pending client responses.
    auto drainResponses() -> std::vector<MEClientResponse> {
      std::vector<MEClientResponse> out;
      while (responses.size()) {
        out.push_back(*responses.getNextToRead());
        responses.updateReadIndex();
      }
      return out;
    }

    /// Drain and return all pending market updates.
    auto drainMarketUpdates() -> std::vector<MEMarketUpdate> {
      std::vector<MEMarketUpdate> out;
      while (market_updates.size()) {
        out.push_back(*market_updates.getNextToRead());
        market_updates.updateReadIndex();
      }
      return out;
    }
  };

  constexpr TickerId T = 1;

  // -------------------------------------------------------------------------
  void test_add_resting_order() {
    std::cout << "test_add_resting_order\n";
    Harness h;

    h.add(/*cid*/1, /*oid*/100, T, Side::BUY, /*px*/100, /*qty*/10);

    auto resp = h.drainResponses();
    auto md   = h.drainMarketUpdates();

    // A non-crossing order produces exactly one ACCEPTED response...
    CHECK(resp.size() == 1, "one client response for a resting add");
    CHECK(resp[0].type_ == ClientResponseType::ACCEPTED, "response is ACCEPTED");
    CHECK(resp[0].client_id_ == 1 && resp[0].client_order_id_ == 100, "response echoes client/order id");
    CHECK(resp[0].leaves_qty_ == 10, "full qty rests");

    // ...and exactly one ADD market update at the resting price.
    CHECK(md.size() == 1, "one market update for a resting add");
    CHECK(md[0].type_ == MarketUpdateType::ADD, "market update is ADD");
    CHECK(md[0].price_ == 100 && md[0].qty_ == 10 && md[0].side_ == Side::BUY, "ADD carries order attributes");
  }

  // -------------------------------------------------------------------------
  void test_full_match() {
    std::cout << "test_full_match\n";
    Harness h;

    h.add(1, 100, T, Side::BUY, 100, 10); // resting bid
    h.drainResponses();
    h.drainMarketUpdates();

    h.add(2, 200, T, Side::SELL, 100, 10); // aggressive sell that fully crosses

    auto resp = h.drainResponses();
    auto md   = h.drainMarketUpdates();

    // Expect: ACCEPTED(aggressor) + FILLED(aggressor) + FILLED(resting).
    CHECK(resp.size() == 3, "three responses on a full match");
    CHECK(resp[0].type_ == ClientResponseType::ACCEPTED, "first response ACCEPTED");
    CHECK(resp[1].type_ == ClientResponseType::FILLED, "aggressor FILLED");
    CHECK(resp[1].client_id_ == 2 && resp[1].leaves_qty_ == 0, "aggressor fully filled, nothing leaves");
    CHECK(resp[2].type_ == ClientResponseType::FILLED, "resting FILLED");
    CHECK(resp[2].client_id_ == 1 && resp[2].leaves_qty_ == 0, "resting fully filled");
    CHECK(resp[1].exec_qty_ == 10 && resp[2].exec_qty_ == 10, "both fills are for 10");

    // Market updates: TRADE, then CANCEL (resting order fully consumed).
    // No ADD, since the aggressor left nothing to rest.
    bool saw_trade = false, saw_cancel = false, saw_add = false;
    for (const auto &u : md) {
      saw_trade  |= (u.type_ == MarketUpdateType::TRADE);
      saw_cancel |= (u.type_ == MarketUpdateType::CANCEL);
      saw_add    |= (u.type_ == MarketUpdateType::ADD);
    }
    CHECK(saw_trade, "a TRADE was published");
    CHECK(saw_cancel, "consumed resting order published CANCEL");
    CHECK(!saw_add, "nothing rested, so no ADD");
  }

  // -------------------------------------------------------------------------
  void test_partial_match_leaves_rest() {
    std::cout << "test_partial_match_leaves_rest\n";
    Harness h;

    h.add(1, 100, T, Side::BUY, 100, 6);  // resting bid, qty 6
    h.drainResponses();
    h.drainMarketUpdates();

    h.add(2, 200, T, Side::SELL, 100, 10); // sell 10 -> 6 filled, 4 rests as ask

    auto resp = h.drainResponses();
    auto md   = h.drainMarketUpdates();

    CHECK(resp.size() == 3, "ACCEPTED + 2 FILLED on partial cross");
    CHECK(resp[1].exec_qty_ == 6, "6 executed");
    CHECK(resp[1].leaves_qty_ == 4, "4 leaves on the aggressor");

    // The bid was fully consumed (CANCEL) and the remaining 4 rest (ADD).
    const MEMarketUpdate *add = nullptr;
    bool saw_trade = false, saw_cancel = false;
    for (const auto &u : md) {
      if (u.type_ == MarketUpdateType::ADD) add = &u;
      saw_trade  |= (u.type_ == MarketUpdateType::TRADE);
      saw_cancel |= (u.type_ == MarketUpdateType::CANCEL);
    }
    CHECK(saw_trade, "TRADE published");
    CHECK(saw_cancel, "fully-consumed bid published CANCEL");
    CHECK(add != nullptr, "remaining qty rested via ADD");
    if (add) {
      CHECK(add->side_ == Side::SELL && add->qty_ == 4 && add->price_ == 100, "resting remainder is SELL 4 @ 100");
    }
  }

  // -------------------------------------------------------------------------
  void test_no_cross_when_prices_dont_meet() {
    std::cout << "test_no_cross_when_prices_dont_meet\n";
    Harness h;

    h.add(1, 100, T, Side::BUY, 100, 10);  // bid @ 100
    h.drainResponses();
    h.drainMarketUpdates();

    h.add(2, 200, T, Side::SELL, 101, 10); // ask @ 101 does not cross bid @ 100

    auto resp = h.drainResponses();
    auto md   = h.drainMarketUpdates();

    CHECK(resp.size() == 1 && resp[0].type_ == ClientResponseType::ACCEPTED, "only ACCEPTED, no fill");
    CHECK(md.size() == 1 && md[0].type_ == MarketUpdateType::ADD, "only ADD, no TRADE");
  }

  // -------------------------------------------------------------------------
  void test_fifo_price_time_priority() {
    std::cout << "test_fifo_price_time_priority\n";
    Harness h;

    // Two bids at the same price; first one in gets first fill (FIFO).
    h.add(1, 100, T, Side::BUY, 100, 10);
    h.add(2, 200, T, Side::BUY, 100, 10);
    h.drainResponses();
    h.drainMarketUpdates();

    // Aggressive sell of 10 should hit client 1 (earliest) only.
    h.add(3, 300, T, Side::SELL, 100, 10);
    auto resp = h.drainResponses();

    // Find the FILLED response that belongs to a resting order (not the aggressor cid 3).
    const MEClientResponse *resting_fill = nullptr;
    for (const auto &r : resp) {
      if (r.type_ == ClientResponseType::FILLED && r.client_id_ != 3) resting_fill = &r;
    }
    CHECK(resting_fill != nullptr, "a resting order was filled");
    if (resting_fill) {
      CHECK(resting_fill->client_id_ == 1, "earliest order (client 1) filled first, not client 2");
    }
  }

  // -------------------------------------------------------------------------
  void test_best_price_selection() {
    std::cout << "test_best_price_selection\n";
    Harness h;

    // Bids at 99, 100, 98 -> best (highest) bid is 100.
    h.add(1, 1, T, Side::BUY, 99,  10);
    h.add(1, 2, T, Side::BUY, 100, 10);
    h.add(1, 3, T, Side::BUY, 98,  10);
    h.drainResponses();
    h.drainMarketUpdates();

    // Sell 5 @ 100 must match against the best bid (100), i.e. order id 2.
    h.add(2, 200, T, Side::SELL, 100, 5);
    auto resp = h.drainResponses();

    const MEClientResponse *resting_fill = nullptr;
    for (const auto &r : resp) {
      if (r.type_ == ClientResponseType::FILLED && r.client_id_ == 1) resting_fill = &r;
    }
    CHECK(resting_fill != nullptr, "best bid was filled");
    if (resting_fill) {
      CHECK(resting_fill->price_ == 100, "match happened at best bid price 100");
      CHECK(resting_fill->client_order_id_ == 2, "best-priced order (oid 2) matched");
      CHECK(resting_fill->leaves_qty_ == 5, "best bid partially filled, 5 remain");
    }
  }

  // -------------------------------------------------------------------------
  void test_cancel_resting_order() {
    std::cout << "test_cancel_resting_order\n";
    Harness h;

    h.add(1, 100, T, Side::BUY, 100, 10);
    h.drainResponses();
    h.drainMarketUpdates();

    h.cancel(1, 100, T);
    auto resp = h.drainResponses();
    auto md   = h.drainMarketUpdates();

    CHECK(resp.size() == 1 && resp[0].type_ == ClientResponseType::CANCELED, "CANCELED response");
    CHECK(resp[0].client_order_id_ == 100, "canceled the right order");
    CHECK(md.size() == 1 && md[0].type_ == MarketUpdateType::CANCEL, "CANCEL market update");

    // A subsequent crossing sell should now find nothing to trade against.
    h.add(2, 200, T, Side::SELL, 100, 10);
    auto resp2 = h.drainResponses();
    bool any_fill = false;
    for (const auto &r : resp2) any_fill |= (r.type_ == ClientResponseType::FILLED);
    CHECK(!any_fill, "no fills after the resting bid was canceled");
  }

  // -------------------------------------------------------------------------
  void test_cancel_nonexistent_order_rejected() {
    std::cout << "test_cancel_nonexistent_order_rejected\n";
    Harness h;

    h.cancel(1, 999, T); // never added
    auto resp = h.drainResponses();

    CHECK(resp.size() == 1, "one response for a bad cancel");
    CHECK(resp[0].type_ == ClientResponseType::CANCEL_REJECTED, "CANCEL_REJECTED for unknown order");
    CHECK(resp[0].client_order_id_ == 999, "rejection echoes the requested order id");
  }
} // namespace

int main() {
  std::cout << std::unitbuf; // flush after every write so a crash points at the right test.
  std::cout << "=== MEOrderBook tests ===\n";

  test_add_resting_order();
  test_full_match();
  test_partial_match_leaves_rest();
  test_no_cross_when_prices_dont_meet();
  test_fifo_price_time_priority();
  test_best_price_selection();
  test_cancel_resting_order();
  test_cancel_nonexistent_order_rejected();

  std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
  if (g_failures) {
    std::cout << "RESULT: FAILED (" << g_failures << " failing checks)\n";
    return 1;
  }
  std::cout << "RESULT: PASSED\n";
  return 0;
}