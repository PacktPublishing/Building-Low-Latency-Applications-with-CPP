#include <cstdio>

class RuntimeExample {
public:
  virtual void placeOrder() {
    printf("RuntimeExample::placeOrder()\n");
  }
};

class SpecificRuntimeExample : public RuntimeExample {
public:
  void placeOrder() override {
    printf("SpecificRuntimeExample::placeOrder()\n");
  }
};

template<typename actual_type>
class CRTPExample {
public:
  void placeOrder() {
    static_cast<actual_type *>(this)->actualPlaceOrder();
  }

  void actualPlaceOrder() {
    printf("CRTPExample::actualPlaceOrder()\n");
  }
};

class SpecificCRTPExample : public CRTPExample<SpecificCRTPExample> {
public:
  void actualPlaceOrder() {
    printf("SpecificCRTPExample::actualPlaceOrder()\n");
  }
};

int main(int, char **) {
  RuntimeExample *runtime_example = new SpecificRuntimeExample();
  runtime_example->placeOrder();

  CRTPExample <SpecificCRTPExample> crtp_example;
  crtp_example.placeOrder();

  return 0;
}
