#include <cstdio>
#include <cstdint>
#include <cstddef>

struct PoorlyAlignedData {
  char c;
  uint16_t u;
  double d;
  int16_t i;
};

struct WellAlignedData {
  double d;
  uint16_t u;
  int16_t i;
  char c;
};

#pragma pack(push, 1)
struct PackedData {
  double d;
  uint16_t u;
  int16_t i;
  char c;
};
#pragma pack(pop)

int main() {
  printf("PoorlyAlignedData c:%lu u:%lu d:%lu i:%lu size:%lu\n",
         offsetof(struct PoorlyAlignedData,c), offsetof(struct PoorlyAlignedData,u), offsetof(struct PoorlyAlignedData,d), offsetof(struct PoorlyAlignedData,i), sizeof(PoorlyAlignedData));
  printf("WellAlignedData d:%lu u:%lu i:%lu c:%lu size:%lu\n",
         offsetof(struct WellAlignedData,d), offsetof(struct WellAlignedData,u), offsetof(struct WellAlignedData,i), offsetof(struct WellAlignedData,c), sizeof(WellAlignedData));
  printf("PackedData d:%lu u:%lu i:%lu c:%lu size:%lu\n",
         offsetof(struct PackedData,d), offsetof(struct PackedData,u), offsetof(struct PackedData,i), offsetof(struct PackedData,c), sizeof(PackedData));
}