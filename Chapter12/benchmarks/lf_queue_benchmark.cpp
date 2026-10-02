#include <x86intrin.h>
#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "common/lf_queue.h"
#include "exchange/market_data/market_update.h"

/// Benchmarks Common::LFQueue between two threads pinned to separate physical cores.
/// Uses only the public LFQueue API, so the same file measures every version of the queue.

using Msg = Exchange::MEMarketUpdate;
using Queue = Common::LFQueue<Msg>;

constexpr size_t QUEUE_SIZE = Common::ME_MAX_MARKET_UPDATES;
constexpr size_t THROUGHPUT_MSGS = 50'000'000;
constexpr size_t PING_PONG_ROUND_TRIPS = 2'000'000;
constexpr size_t RUNS = 5;

static int producer_core = -1;
static int consumer_core = -1;

static void pinTo(int core) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core, &cpuset);
  if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0) {
    std::fprintf(stderr, "Failed to pin thread to core %d\n", core);
    std::exit(EXIT_FAILURE);
  }
}

/// rdtsc is not ordered with surrounding instructions: the CPU may read the counter before earlier
/// loads complete. Fence both sides so the timed region is exactly the code between the two reads.
static inline uint64_t tscStart() {
  _mm_lfence();  // earlier instructions finish before the counter is read
  const auto tsc = __rdtsc();
  _mm_lfence();  // the timed code does not start before the counter is read
  return tsc;
}

static inline uint64_t tscEnd() {
  unsigned aux;
  const auto tsc = __rdtscp(&aux);  // waits for earlier instructions, including loads, to complete
  _mm_lfence();  // later instructions do not start before the counter is read
  return tsc;
}

/// TSC ticks per nanosecond, measured against steady_clock.
static double tscPerNs() {
  const auto t0 = std::chrono::steady_clock::now();
  const auto c0 = tscStart();
  while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(200)) {
  }
  const auto c1 = tscEnd();
  const auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
  return static_cast<double>(c1 - c0) / ns;
}

static void push(Queue &q, uint64_t seq) {
  auto next_write = q.getNextToWriteTo();
  next_write->order_id_ = seq;
  next_write->qty_ = static_cast<Common::Qty>(seq);
  q.updateWriteIndex();
}

static uint64_t popBlocking(Queue &q) {
  const Msg *next_read;
  while (!(next_read = q.getNextToRead())) {
  }
  const auto seq = next_read->order_id_;
  q.updateReadIndex();
  return seq;
}

/// Producer streams THROUGHPUT_MSGS messages, consumer reads and verifies their order.
/// Back-pressure goes through a benchmark-owned counter so the queue never overflows,
/// independent of whether the queue itself can detect being full.
static double throughputNsPerMsg() {
  Queue q(QUEUE_SIZE);
  alignas(128) std::atomic<uint64_t> consumed{0};
  constexpr uint64_t window = QUEUE_SIZE / 2;
  constexpr uint64_t publish_every = 64;

  std::thread consumer([&] {
    pinTo(consumer_core);
    for (uint64_t i = 0; i < THROUGHPUT_MSGS; ++i) {
      const auto seq = popBlocking(q);
      if (seq != i) {
        std::fprintf(stderr, "Throughput: expected seq %lu got %lu\n", i, seq);
        std::exit(EXIT_FAILURE);
      }
      if ((i + 1) % publish_every == 0)
        consumed.store(i + 1, std::memory_order_release);
    }
    consumed.store(THROUGHPUT_MSGS, std::memory_order_release);
  });

  pinTo(producer_core);
  const auto start = std::chrono::steady_clock::now();
  uint64_t consumed_cache = 0;
  for (uint64_t i = 0; i < THROUGHPUT_MSGS; ++i) {
    while (i - consumed_cache >= window)
      consumed_cache = consumed.load(std::memory_order_acquire);
    push(q, i);
  }
  consumer.join();
  const auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
  return ns / THROUGHPUT_MSGS;
}

/// One message in flight: producer sends on `ping`, consumer echoes it back on `pong`.
/// No timing inside the loop, so the mean round trip is exact: wall-clock time / round trips.
static double pingPongMeanNs() {
  Queue ping(QUEUE_SIZE), pong(QUEUE_SIZE);

  std::thread echo([&] {
    pinTo(consumer_core);
    for (size_t i = 0; i < PING_PONG_ROUND_TRIPS; ++i)
      push(pong, popBlocking(ping));
  });

  pinTo(producer_core);
  const auto start = std::chrono::steady_clock::now();
  for (uint64_t i = 0; i < PING_PONG_ROUND_TRIPS; ++i) {
    push(ping, i);
    const auto seq = popBlocking(pong);
    if (seq != i) {
      std::fprintf(stderr, "Ping-pong: expected seq %lu got %lu\n", i, seq);
      std::exit(EXIT_FAILURE);
    }
  }
  const auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
  echo.join();
  return ns / PING_PONG_ROUND_TRIPS;
}

struct LatencyPercentiles {
  double p50, p90, p95, p99, p999;
};

/// Same ping-pong, but every round trip is timed with fenced rdtsc to get the distribution.
/// The fences keep the CPU from overlapping work across iterations, so these samples read
/// higher than the exact mean from pingPongMeanNs() (about 25-30 ns on an i7-12700H).
static LatencyPercentiles pingPongPercentilesNs(double tsc_per_ns) {
  Queue ping(QUEUE_SIZE), pong(QUEUE_SIZE);
  std::vector<uint64_t> cycles(PING_PONG_ROUND_TRIPS);

  std::thread echo([&] {
    pinTo(consumer_core);
    for (size_t i = 0; i < PING_PONG_ROUND_TRIPS; ++i)
      push(pong, popBlocking(ping));
  });

  pinTo(producer_core);
  for (uint64_t i = 0; i < PING_PONG_ROUND_TRIPS; ++i) {
    const auto start = tscStart();
    push(ping, i);
    const auto seq = popBlocking(pong);
    cycles[i] = tscEnd() - start;
    if (seq != i) {
      std::fprintf(stderr, "Ping-pong: expected seq %lu got %lu\n", i, seq);
      std::exit(EXIT_FAILURE);
    }
  }
  echo.join();

  std::sort(cycles.begin(), cycles.end());
  const auto at = [&](double q) { return static_cast<double>(cycles[static_cast<size_t>(q * (cycles.size() - 1))]) / tsc_per_ns; };
  return {at(0.50), at(0.90), at(0.95), at(0.99), at(0.999)};
}

template<typename F>
static auto medianOf(F &&measure) {
  std::vector<decltype(measure())> results;
  for (size_t i = 0; i < RUNS; ++i)
    results.push_back(measure());
  std::sort(results.begin(), results.end());
  return results[RUNS / 2];
}

int main(int argc, char **argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <producer core> <consumer core>\n"
                         "Pick two different physical cores: not SMT siblings, and not E-cores on a hybrid CPU (see lscpu -e).\n",
                 argv[0]);
    return EXIT_FAILURE;
  }
  producer_core = std::atoi(argv[1]);
  consumer_core = std::atoi(argv[2]);
  std::printf("LFQueue<MEMarketUpdate> size:%zu producer core:%d consumer core:%d runs:%zu (median reported)\n",
              QUEUE_SIZE, producer_core, consumer_core, RUNS);

  const auto tsc_per_ns = tscPerNs();

  const auto throughput = medianOf(throughputNsPerMsg);
  std::printf("throughput     %8.2f ns/msg (%zu msgs)\n", throughput, THROUGHPUT_MSGS);

  const auto rtt_mean = medianOf(pingPongMeanNs);
  std::printf("ping-pong RTT  mean %8.1f ns (wall clock, no per-sample timing, %zu round trips)\n", rtt_mean, PING_PONG_ROUND_TRIPS);

  std::vector<LatencyPercentiles> runs;
  for (size_t i = 0; i < RUNS; ++i)
    runs.push_back(pingPongPercentilesNs(tsc_per_ns));
  std::sort(runs.begin(), runs.end(), [](const auto &a, const auto &b) { return a.p50 < b.p50; });
  const auto &l = runs[RUNS / 2];
  std::printf("ping-pong RTT  p50 %8.1f  p90 %8.1f  p95 %8.1f  p99 %8.1f  p99.9 %8.1f ns (fenced rdtsc per sample, includes fence overhead)\n",
              l.p50, l.p90, l.p95, l.p99, l.p999);
  return 0;
}
