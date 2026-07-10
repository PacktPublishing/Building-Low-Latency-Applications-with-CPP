#!/bin/bash
# Build and run the MEOrderBook unit tests.
#
# The tests drive orders through a real MatchingEngine (single-threaded, no
# start()) and assert on the client responses + market updates it emits.
#
# NOTE ON macOS: each MEOrderBook embeds a ~2GB cid_oid_to_order_ array by
# value (ME_MAX_NUM_CLIENTS * ME_MAX_ORDER_IDS pointers), and a MatchingEngine
# builds 8 of them -> ~16GB of virtual reservations. macOS's default allocator
# is unhappy at that scale in a plain build, so we build under AddressSanitizer
# by default, which uses its own allocator and also validates memory safety.
# On Linux you can drop -fsanitize=address and it will run fine natively.

set -e
cd "$(dirname "$0")/.."   # -> Chapter12

OUT="${TMPDIR:-/tmp}/me_order_book_test"

# -Wno-* silence pre-existing warnings in the book's own code (sprintf usage,
# %ld vs long long) so the test output stays readable.
FLAGS="-std=c++2a -g -O1 -Wall -Wno-deprecated-declarations -Wno-format -I. -Iexchange"
if [ "${NO_ASAN:-0}" != "1" ]; then
  FLAGS="$FLAGS -fsanitize=address"
fi

echo "Building ($FLAGS)..."
g++ $FLAGS \
  tests/me_order_book_test.cpp \
  exchange/matcher/matching_engine.cpp \
  exchange/matcher/me_order_book.cpp \
  exchange/matcher/me_order.cpp \
  -o "$OUT" -lpthread

echo "Running..."
ASAN_OPTIONS=detect_leaks=0 "$OUT"
