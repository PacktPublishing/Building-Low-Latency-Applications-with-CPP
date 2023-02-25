#include <cstdio>
#include <vector>

struct Order {
  int id;
  double price;
};

class InheritanceOrderBook : public std::vector<Order> {
};

class CompositionOrderBook {
  std::vector<Order> orders_;

public:
  auto size() const noexcept {
    return orders_.size();
  }
};

int main() {
  InheritanceOrderBook i_book;
  CompositionOrderBook c_book;

  printf("InheritanceOrderBook::size():%lu CompositionOrderBook:%lu\n", i_book.size(), c_book.size());
}