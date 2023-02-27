#include <cstdio>
#include <cstdint>
#include <cstdlib>

enum class Side : int16_t { BUY = 1, SELL = -1 };

int main() {
  const auto fill_side = (rand() % 2 ? Side::BUY : Side::SELL);
  const int fill_qty = 10;
  printf("fill_side:%s fill_qty:%d.\n", (fill_side == Side::BUY ? "BUY" : (fill_side == Side::SELL ? "SELL" : "INVALID")), fill_qty);

  { // with branching
    int last_buy_qty = 0, last_sell_qty = 0, position = 0;

    if (fill_side == Side::BUY) { position += fill_qty; last_buy_qty = fill_qty;
    } else if (fill_side == Side::SELL) { position -= fill_qty; last_sell_qty = fill_qty; }

    printf("With branching - position:%d last-buy:%d last-sell:%d.\n", position, last_buy_qty, last_sell_qty);
  }

  { // without branching
    int last_qty[3] = {0, 0, 0}, position = 0;

    auto sideToInt = [](Side side) noexcept { return static_cast<int16_t>(side); };

    const auto int_fill_side = sideToInt(fill_side);
    position += int_fill_side * fill_qty;
    last_qty[int_fill_side + 1] = fill_qty;

    printf("Without branching - position:%d last-buy:%d last-sell:%d.\n", position, last_qty[sideToInt(Side::BUY) + 1], last_qty[sideToInt(Side::SELL) + 1]);
  }
}