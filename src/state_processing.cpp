#include "state_processing.h"

void initState(DeviceState &state) {
  for (size_t i = 0; i < kStateValueCount; ++i) {
    state.values[i] = static_cast<int32_t>(esp_random() % 20001) - 10000;
  }
}

void processState(DeviceState &ownState, const ClosestDeviceState closest[],
                  size_t closestCount) {
  int64_t neighborValueTotals[kStateValueCount] = {};
  int32_t neighborCount = 0;

  for (size_t i = 0; i < closestCount; ++i) {
    if (!closest[i].valid) {
      continue;
    }

    ++neighborCount;
    for (size_t value = 0; value < kStateValueCount; ++value) {
      neighborValueTotals[value] += closest[i].state.values[value];
    }
  }

  if (neighborCount == 0) {
    return;
  }

  for (size_t value = 0; value < kStateValueCount; ++value) {
    const int32_t neighborAverage =
        static_cast<int32_t>(neighborValueTotals[value] / neighborCount);
    ownState.values[value] = ((ownState.values[value] * 3) + neighborAverage) / 4;
  }
}
