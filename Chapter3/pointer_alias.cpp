void func(int *a, int *b, int n) {
  for (int i = 0; i < n; ++i) {
    a[i] = *b;
  }
}

void func_restrict(int *__restrict a, int *__restrict b, int n) {
  for (int i = 0; i < n; ++i) {
    a[i] = *b;
  }
}

int main() {
  int a[10], b;
  func(a, &b, 10);
  func_restrict(a, &b, 10);
}