#include "common/mem_pool.h"
#include "common/opt_mem_pool.h"
#include "common/perf_utils.h"

#include "exchange/market_data/market_update.h"

template<typename T>
size_t benchmarkMemPool(T *mem_pool) {
  constexpr size_t loop_count = 100000;
  size_t total_rdtsc = 0;
  std::array<Exchange::MDPMarketUpdate*, 256> allocated_objs;

  for (size_t i = 0; i < loop_count; ++i) {
    for(size_t j = 0; j < allocated_objs.size(); ++j) {
      const auto start = Common::rdtsc();
      allocated_objs[j] = mem_pool->allocate();
      total_rdtsc += (Common::rdtsc() - start);
    }
    for(size_t j = 0; j < allocated_objs.size(); ++j) {
      const auto start = Common::rdtsc();
      mem_pool->deallocate(allocated_objs[j]);
      total_rdtsc += (Common::rdtsc() - start);
    }
  }

  return (total_rdtsc / (loop_count * allocated_objs.size()));
}

int main(int, char **) {
  {
    Common::MemPool<Exchange::MDPMarketUpdate> mem_pool(512);
    const auto cycles = benchmarkMemPool(&mem_pool);
    std::cout << "ORIGINAL MEMPOOL " << cycles << " CLOCK CYCLES PER OPERATION." << std::endl;
  }

  {
    OptCommon::OptMemPool<Exchange::MDPMarketUpdate> opt_mem_pool(512);
    const auto cycles = benchmarkMemPool(&opt_mem_pool);
    std::cout << "OPTIMIZED MEMPOOL " << cycles << " CLOCK CYCLES PER OPERATION." << std::endl;
  }

  exit(EXIT_SUCCESS);
}
