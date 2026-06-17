#ifndef STATE_PROCESSING_H
#define STATE_PROCESSING_H

#include <cstddef>
#include <cstdint>

#include <esp_now.h>
#include <esp_random.h>

constexpr size_t kKernelSize = 9;
constexpr size_t kMaxActivations = 8;

struct Activation {
  uint8_t op;    // 0="<", 1="<=", 2="==", 3=">=", 4=">"
  float value;
};

struct DeviceState {
  uint32_t magic;
  uint8_t version;
  uint8_t mac[ESP_NOW_ETH_ALEN];
  uint32_t sequence;
  uint32_t uptimeMs;
  int8_t txPower;
  float temperature;
  bool value;  // Binary: 0 or 1
  uint32_t kernelSequence;
  uint32_t valueSequence;
  uint32_t activationSequence;
  float kernel[kKernelSize];
  Activation activations[kMaxActivations];
  uint8_t activationCount;
};

struct ClosestDeviceState {
  bool valid;
  int8_t rssi;
  uint32_t ageMs;
  DeviceState state;
};

void initState(DeviceState &state);
void resetStateValue(DeviceState &state);
void processState(DeviceState &ownState, const ClosestDeviceState closest[],
                  size_t closestCount);
bool loadPresetKernel(const char* presetName, float kernel[kKernelSize]);
bool loadPreset(DeviceState &state, const char* presetName);

#endif
