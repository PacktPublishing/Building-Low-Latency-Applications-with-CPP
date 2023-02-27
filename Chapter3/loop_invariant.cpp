#include <cstdlib>

int main() {
  auto doSomething = [](double r) noexcept { return 3.14 * r * r; };
  [[maybe_unused]] int a[100], b = rand();

  // original
  for(auto i = 0; i < 100; ++i)
    a[i] = (doSomething(50) + b * 2) + 1;

  // loop invariant
  auto temp = (doSomething(50) + b * 2) + 1;
  for(auto i = 0; i < 100; ++i)
    a[i] = temp;
}