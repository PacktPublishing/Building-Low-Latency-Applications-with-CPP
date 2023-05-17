#include "thread_utils.h"

auto dummyFunction(int a, int b, bool sleep) {
  std::cout << "dummyFunction(" << a << "," << b << ")" << std::endl;
  std::cout << "dummyFunction output=" << a + b << std::endl;

  if(sleep) {
    std::cout << "dummyFunction sleeping..." << std::endl;

    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(5s);
  }

  std::cout << "dummyFunction done." << std::endl;
}

int main(int, char **) {
  using namespace Common;

  auto t1 = createAndStartThread(-1, "dummyFunction1", dummyFunction, 12, 21, false);
  auto t2 = createAndStartThread(1, "dummyFunction2", dummyFunction, 15, 51, true);

  std::cout << "main waiting for threads to be done." << std::endl;
  t1->join();
  t2->join();
  std::cout << "main exiting." << std::endl;

  return 0;
}
