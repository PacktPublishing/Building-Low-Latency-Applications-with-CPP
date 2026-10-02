#!/bin/bash

# Build lf_queue_benchmark with g++ and clang++ using the CMake Release flags plus -march=native and run both.
# Required arguments: <producer core> <consumer core>. Pick two different physical cores:
# not SMT siblings, and not E-cores on a hybrid CPU (see lscpu -e).

set -e
if [ $# -ne 2 ]; then
  echo "usage: $0 <producer core> <consumer core>" >&2
  exit 1
fi
cd "$(dirname "$0")/.."

FLAGS="-std=c++2a -Wall -Wextra -Werror -Wpedantic -O3 -DNDEBUG -march=native -I. -Iexchange -Itrading -pthread"
mkdir -p ./cmake-build-release

for CXX in g++ clang++; do
  $CXX $FLAGS benchmarks/lf_queue_benchmark.cpp -o ./cmake-build-release/lf_queue_benchmark_$CXX
  echo "== $CXX $($CXX -dumpversion)"
  ./cmake-build-release/lf_queue_benchmark_$CXX "$@"
done
