#ifndef STATE_PROCESSING_H
#define STATE_PROCESSING_H

#include <cstddef>
#include <cstdint>

#include <esp_now.h>
#include <esp_random.h>

constexpr size_t kStateValueCount = 8;

struct DeviceState {
  uint32_t magic;
  uint8_t version;
  uint8_t mac[ESP_NOW_ETH_ALEN];
  uint32_t sequence;
  uint32_t uptimeMs;
  int8_t txPower;
  float temperature;
  int32_t values[kStateValueCount];
};

struct ClosestDeviceState {
  bool valid;
  int8_t rssi;
  uint32_t ageMs;
  DeviceState state;
};

void initState(DeviceState &state);
void processState(DeviceState &ownState, const ClosestDeviceState closest[],
                  size_t closestCount);

#endif
