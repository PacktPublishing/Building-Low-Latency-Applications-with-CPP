int main() {
  [[maybe_unused]] int a[100];

  // original
  for(auto i = 0; i < 100; ++i)
    a[i] = i * 10 + 12;

  // optimized
  int temp = 12;
  for(auto i = 0; i < 100; ++i) {
    a[i] = temp;
    temp += 10;
  }
}