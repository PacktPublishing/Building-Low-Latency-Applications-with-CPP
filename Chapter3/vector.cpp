#include <cstddef>

int main() {
  const size_t size = 1024;
  [[maybe_unused]] float x[size], a[size], b[size];

  // no vectorization
  for (size_t i = 0; i < size; ++i) {
    x[i] = a[i] + b[i];
  }

  // vectorization
  for (size_t i = 0; i < size; i += 4) {
    x[i] = a[i] + b[i];
    x[i + 1] = a[i + 1] + b[i + 1];
    x[i + 2] = a[i + 2] + b[i + 2];
    x[i + 3] = a[i + 3] + b[i + 3];
  }
}