#include "state_processing.h"
#include <cmath>

void initState(DeviceState &state) {
  // Initialize value with random float in range [-1, 1]
  state.value = (esp_random() % 20001 - 10000) / 10000.0f;
  state.rulesSequence = 0;
  state.valueSequence = 0;

  // Initialize rules with random floats in range [-1, 1]
  for (size_t i = 0; i < kRulesCount; ++i) {
    state.rules[i] = (esp_random() % 20001 - 10000) / 10000.0f;
  }
}


void processState(DeviceState &ownState, const ClosestDeviceState closest[],
                  size_t closestCount) {
  // First, check for newer rules from neighbors and propagate
  for (size_t i = 0; i < closestCount; ++i) {
    if (closest[i].valid && closest[i].state.rulesSequence > ownState.rulesSequence) {
      // Copy rules from neighbor
      ownState.rulesSequence = closest[i].state.rulesSequence;
      for (size_t j = 0; j < kRulesCount; ++j) {
        ownState.rules[j] = closest[i].state.rules[j];
      }
    }
  }

  // Check for reset propagation from neighbors
  for (size_t i = 0; i < closestCount; ++i) {
    if (closest[i].valid && closest[i].state.valueSequence > ownState.valueSequence) {
      // Neighbor has newer value reset - propagate it
      ownState.valueSequence = closest[i].state.valueSequence;
      resetStateValue(ownState);
    }
  }

  // Build array: [neighbor0.value, neighbor1.value, ..., neighbor7.value, ownState.value]
  float neighborValues[kRulesCount] = {};
  for (size_t i = 0; i < closestCount && i < kRulesCount - 1; ++i) {
    if (closest[i].valid) {
      neighborValues[i] = closest[i].state.value;
    }
  }
  // Last element is own value
  neighborValues[kRulesCount - 1] = ownState.value;

  // Calculate dot product: sum(rules[i] * neighborValues[i])
  float newValue = 0.0f;
  for (size_t i = 0; i < kRulesCount; ++i) {
    newValue += ownState.rules[i] * neighborValues[i];
  }

  // Check for NaN or Inf
  if (std::isnan(newValue) || std::isinf(newValue)) {
    // Reset to initial random value in range [-1, 1]
    newValue = (esp_random() % 20001 - 10000) / 10000.0f;
  }

  ownState.value = newValue;
}

void resetStateValue(DeviceState &state) {
  state.value = (esp_random() % 20001 - 10000) / 10000.0f;
  state.valueSequence++;
}
