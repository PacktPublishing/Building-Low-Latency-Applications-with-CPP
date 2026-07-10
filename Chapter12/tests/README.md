# MEOrderBook unit tests

Unit tests for `Exchange::MEOrderBook` (the matching-engine limit order book).

## What is covered

`me_order_book_test.cpp` drives orders through a real `MatchingEngine`
(single-threaded — it never calls `start()`, so everything runs on the test
thread) and asserts on the client responses and market updates the book emits.

| Test | Scenario |
| --- | --- |
| `test_add_resting_order` | A non-crossing order → `ACCEPTED` response + `ADD` market update |
| `test_full_match` | Aggressor fully crosses a resting order → 2×`FILLED` + `TRADE`/`CANCEL`, no `ADD` |
| `test_partial_match_leaves_rest` | Aggressor partially fills, remainder rests → `ADD` for the leftover |
| `test_no_cross_when_prices_dont_meet` | Bid < ask → no trade, order just rests |
| `test_fifo_price_time_priority` | Two orders at the same price → earliest one fills first |
| `test_best_price_selection` | Multiple price levels → match happens at the best price |
| `test_cancel_resting_order` | Cancel → `CANCELED` response + `CANCEL` market update |
| `test_cancel_nonexistent_order_rejected` | Cancel of an unknown order → `CANCEL_REJECTED` |

## Running

From `Chapter12/`:

```bash
bash tests/run_tests.sh
```

Expected tail:

```
39/39 checks passed
RESULT: PASSED
```

## Why the test builds under AddressSanitizer by default

Each `MEOrderBook` embeds a `cid_oid_to_order_` array **by value**
(`ME_MAX_NUM_CLIENTS * ME_MAX_ORDER_IDS` pointers ≈ 2 GB), and a
`MatchingEngine` constructs 8 of them → ~16 GB of virtual reservations. macOS's
default allocator misbehaves at that scale in a plain build (segfault), even
though the order-book logic is correct.

Two independent checks confirm it is a footprint issue, not a logic bug:

1. Shrinking the capacity constants (`ME_MAX_ORDER_IDS`, `ME_MAX_NUM_CLIENTS`)
   makes a plain `-O0` build pass 39/39.
2. The AddressSanitizer build — which uses its own allocator **and** full
   memory-safety checking — passes 39/39 at the real full-size config.

So `run_tests.sh` builds with `-fsanitize=address`. On Linux (with overcommit)
the plain build runs fine natively:

```bash
NO_ASAN=1 bash tests/run_tests.sh
```

## Cross-platform note

The book's code targets x86/Linux. Small, platform-guarded shims were added so
it also compiles/runs on arm64 macOS (the x86/Linux path is unchanged):

- `common/perf_utils.h` — `rdtsc()` gained an aarch64 (`cntvct_el0`) branch.
- `common/thread_utils.h` — Linux-only thread affinity (`cpu_set_t` /
  `pthread_setaffinity_np`) is a no-op elsewhere.
- `common/lf_queue.h` — `std::to_string(pthread_self())` uses a cast that works
  where `pthread_t` is a pointer (macOS).
- `exchange/matcher/me_order_book.h` — `getNextPriority()` now has an explicit
  `-> Priority` return type (its `auto` deduction was ill-formed where
  `Priority` is not `unsigned long`).
