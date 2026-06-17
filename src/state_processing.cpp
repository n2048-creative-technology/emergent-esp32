#include "state_processing.h"
#include <cmath>
#include <cstring>

// Predefined kernel and activation configurations for cellular automata
// Kernel: weights for [n0, n1, ..., n7, self]
// Activations: array of {op, value} pairs evaluated against weighted sum

// Conway's Game of Life: born at 3 neighbors, survives at 2-3 neighbors
// Uses self-weight=9s as offset to encode current state in sum
const float kKernelConway[] = {1, 1, 1, 1, 1, 1, 1, 1, 9};
const Activation kActivationsConway[] = {{2, 3.0f}, {2, 11.0f}, {2, 12.0f}};  // sum==3 OR sum==11 OR sum==12
const uint8_t kActivationsConwayCount = 3;

// Rule 30: Classic 1D CA approximated for 2D (XOR-like behavior)
const float kKernelRule30[] = {1, 1, 1, 0, 0, 0, 0, 0, 0};
const Activation kActivationsRule30[] = {{2, 1.0f}, {2, 2.0f}};  // sum==1 OR sum==2 (simplified)
const uint8_t kActivationsRule30Count = 2;

// Majority: 1 if at least 5 of 8 neighbors are 1
const float kKernelMajority[] = {1, 1, 1, 1, 1, 1, 1, 1, 0};
const Activation kActivationsMajority[] = {{3, 4.5f}};  // sum >= 5
const uint8_t kActivationsMajorityCount = 1;

// AND: 1 only if ALL neighbors are 1
const float kKernelAnd[] = {1, 1, 1, 1, 1, 1, 1, 1, 0};
const Activation kActivationsAnd[] = {{2, 8.0f}};  // sum == 8
const uint8_t kActivationsAndCount = 1;

// OR: 1 if ANY neighbor is 1
const float kKernelOr[] = {1, 1, 1, 1, 1, 1, 1, 1, 0};
const Activation kActivationsOr[] = {{3, 0.5f}};  // sum >= 1
const uint8_t kActivationsOrCount = 1;

void initState(DeviceState &state) {
  state.value = esp_random() % 2;  // Random 0 or 1
  state.kernelSequence = 0;
  state.valueSequence = 0;
  state.activationSequence = 0;
  state.activationCount = 0;

  // Initialize kernel with random floats in range [-1, 1]
  for (size_t i = 0; i < kKernelSize; ++i) {
    state.kernel[i] = (esp_random() % 20001 - 10000) / 10000.0f;
  }
}

bool loadPresetKernel(const char* presetName, float kernel[kKernelSize]) {
  if (strcmp(presetName, "conway") == 0) {
    for (size_t i = 0; i < kKernelSize; ++i) kernel[i] = kKernelConway[i];
    return true;
  } else if (strcmp(presetName, "rule30") == 0) {
    for (size_t i = 0; i < kKernelSize; ++i) kernel[i] = kKernelRule30[i];
    return true;
  } else if (strcmp(presetName, "majority") == 0) {
    for (size_t i = 0; i < kKernelSize; ++i) kernel[i] = kKernelMajority[i];
    return true;
  } else if (strcmp(presetName, "and") == 0) {
    for (size_t i = 0; i < kKernelSize; ++i) kernel[i] = kKernelAnd[i];
    return true;
  } else if (strcmp(presetName, "or") == 0) {
    for (size_t i = 0; i < kKernelSize; ++i) kernel[i] = kKernelOr[i];
    return true;
  }
  return false;
}

// Load preset with both kernel AND activations for full CA behavior
bool loadPreset(DeviceState &state, const char* presetName) {
  if (strcmp(presetName, "conway") == 0) {
    for (size_t i = 0; i < kKernelSize; ++i) state.kernel[i] = kKernelConway[i];
    state.activationCount = kActivationsConwayCount;
    for (size_t i = 0; i < kActivationsConwayCount; ++i) {
      state.activations[i] = kActivationsConway[i];
    }
    return true;
  } else if (strcmp(presetName, "rule30") == 0) {
    for (size_t i = 0; i < kKernelSize; ++i) state.kernel[i] = kKernelRule30[i];
    state.activationCount = kActivationsRule30Count;
    for (size_t i = 0; i < kActivationsRule30Count; ++i) {
      state.activations[i] = kActivationsRule30[i];
    }
    return true;
  } else if (strcmp(presetName, "majority") == 0) {
    for (size_t i = 0; i < kKernelSize; ++i) state.kernel[i] = kKernelMajority[i];
    state.activationCount = kActivationsMajorityCount;
    for (size_t i = 0; i < kActivationsMajorityCount; ++i) {
      state.activations[i] = kActivationsMajority[i];
    }
    return true;
  } else if (strcmp(presetName, "and") == 0) {
    for (size_t i = 0; i < kKernelSize; ++i) state.kernel[i] = kKernelAnd[i];
    state.activationCount = kActivationsAndCount;
    for (size_t i = 0; i < kActivationsAndCount; ++i) {
      state.activations[i] = kActivationsAnd[i];
    }
    return true;
  } else if (strcmp(presetName, "or") == 0) {
    for (size_t i = 0; i < kKernelSize; ++i) state.kernel[i] = kKernelOr[i];
    state.activationCount = kActivationsOrCount;
    for (size_t i = 0; i < kActivationsOrCount; ++i) {
      state.activations[i] = kActivationsOr[i];
    }
    return true;
  }
  return false;
}


void processState(DeviceState &ownState, const ClosestDeviceState closest[],
                  size_t closestCount) {
  // First, check for newer kernel from neighbors and propagate
  for (size_t i = 0; i < closestCount; ++i) {
    if (closest[i].valid && closest[i].state.kernelSequence > ownState.kernelSequence) {
      ownState.kernelSequence = closest[i].state.kernelSequence;
      for (size_t j = 0; j < kKernelSize; ++j) {
        ownState.kernel[j] = closest[i].state.kernel[j];
      }
    }
  }

  // Check for newer activations from neighbors and propagate
  for (size_t i = 0; i < closestCount; ++i) {
    if (closest[i].valid && closest[i].state.activationSequence > ownState.activationSequence) {
      ownState.activationSequence = closest[i].state.activationSequence;
      ownState.activationCount = closest[i].state.activationCount;
      for (size_t j = 0; j < kMaxActivations; ++j) {
        ownState.activations[j] = closest[i].state.activations[j];
      }
    }
  }

  // Check for reset propagation from neighbors
  for (size_t i = 0; i < closestCount; ++i) {
    if (closest[i].valid && closest[i].state.valueSequence > ownState.valueSequence) {
      ownState.valueSequence = closest[i].state.valueSequence;
      resetStateValue(ownState);
    }
  }

  // Build binary input: [n0.value, n1.value, ..., n7.value, own.value]
  float neighborValues[kKernelSize] = {};
  for (size_t i = 0; i < closestCount && i < kKernelSize - 1; ++i) {
    neighborValues[i] = closest[i].valid ? static_cast<float>(closest[i].state.value) : 0.0f;
  }
  neighborValues[kKernelSize - 1] = static_cast<float>(ownState.value);

  // Calculate weighted sum
  float sum = 0.0f;
  for (size_t i = 0; i < kKernelSize; ++i) {
    sum += ownState.kernel[i] * neighborValues[i];
  }

  // Evaluate activations: next state = 1 if ANY activation matches
  if (ownState.activationCount > 0) {
    bool nextValue = false;
    for (uint8_t i = 0; i < ownState.activationCount; ++i) {
      const Activation& a = ownState.activations[i];
      switch (a.op) {
        case 0: nextValue = (sum < a.value); break;
        case 1: nextValue = (sum <= a.value); break;
        case 2: nextValue = (sum == a.value); break;
        case 3: nextValue = (sum >= a.value); break;
        case 4: nextValue = (sum > a.value); break;
        default: nextValue = false;
      }
      if (nextValue) break;  // OR logic: any match wins
    }
    ownState.value = nextValue;
  } else {
    // Default: threshold at 0
    ownState.value = sum > 0.0f;
  }
}

void resetStateValue(DeviceState &state) {
  state.value = esp_random() % 2;  // Random 0 or 1
  state.valueSequence++;
}
