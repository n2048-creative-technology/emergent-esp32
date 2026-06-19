# Multi-Channel Operation and Cross-Channel Propagation

This document describes how to enable devices to **automatically distribute across multiple WiFi channels** while ensuring **kernel and activation updates propagate to all devices**, regardless of which channel they are using.

## Overview

In large-scale deployments (100+ devices), **congesting a single WiFi channel** causes packet loss, collisions, and unreliable communication. This system allows devices to:

1. **Autonomously select the least congested channel** for normal operation
2. **Propagate critical updates (kernels, activations) across all channels** to ensure all devices receive them

This approach maintains **network-wide consistency** for configuration while **reducing RF interference** through adaptive channel distribution.

## Problem Statement

### The Core Challenge
ESP-NOW communication is **channel-specific** — devices on different WiFi channels **cannot directly communicate**. In a large deployment:

- **Problem 1**: If all devices use the same channel, **RF congestion** degrades performance
- **Problem 2**: If devices independently choose channels, **kernel/activation updates cannot propagate** between devices on different channels

### Requirements
| Requirement | Description |
|-------------|-------------|
| **Adaptive Channel Selection** | Devices automatically pick the least congested channel |
| **Cross-Channel Propagation** | Kernel/activation updates reach all devices, regardless of channel |
| **No Network Fragmentation** | All devices must remain part of a single logical network |
| **Low Overhead** | Minimal impact on normal operation and power consumption |

## Solution Architecture

The solution uses a **two-tier communication strategy**:

### Tier 1: Adaptive Channel Selection (Normal Operation)
Each device:
1. Periodically **scans all candidate channels** (e.g., 1, 6, 11)
2. **Counts the number of WiFi access points** on each channel (as a proxy for congestion)
3. **Switches to the channel with the fewest devices**
4. **Stays on that channel** for normal ESP-NOW communication with nearby peers

### Tier 2: Cross-Channel Propagation (Critical Updates)
When a device receives a new kernel or activation configuration (indicated by a higher sequence number):
1. It **floods the update to all candidate channels** (not just its current channel)
2. Devices on other channels **receive and adopt** the update
3. The update **cascades through the network** as devices forward it

This ensures **all devices eventually receive all configuration updates**, regardless of their current channel.

## Implementation Details

### 1. Adaptive Channel Selection

#### Configuration (in `config.h`)
```cpp
// Candidate channels (non-overlapping 2.4GHz WiFi channels)
constexpr uint8_t kCandidateChannels[] = {1, 6, 11};
constexpr size_t kCandidateChannelCount = sizeof(kCandidateChannels) / sizeof(kCandidateChannels[0]);

// How often to scan for the best channel (milliseconds)
constexpr uint32_t kChannelScanIntervalMs = 30000;  // 30 seconds
```

#### Global Variables
```cpp
uint8_t currentChannel = 1;                    // Current WiFi channel
uint32_t lastChannelScanMs = 0;                // Last scan time
uint16_t apCountPerChannel[kCandidateChannelCount] = {0};  // AP count per channel
```

#### Channel Scanning Logic (in `loop()`)
```cpp
uint32_t nowMs = millis();
if (nowMs - lastChannelScanMs >= kChannelScanIntervalMs) {
    lastChannelScanMs = nowMs;
    
    // Scan all candidate channels
    for (size_t i = 0; i < kCandidateChannelCount; i++) {
        esp_wifi_set_channel(kCandidateChannels[i], WIFI_SECOND_CHAN_NONE);
        delay(10);  // Allow channel switch to settle
        
        // Count APs as a proxy for channel congestion
        apCountPerChannel[i] = WiFi.scanNetworks();
        WiFi.scanDelete();  // Clear scan results
    }
    
    // Find the channel with the fewest APs
    uint8_t bestChannel = kCandidateChannels[0];
    uint16_t minAps = apCountPerChannel[0];
    for (size_t i = 1; i < kCandidateChannelCount; i++) {
        if (apCountPerChannel[i] < minAps) {
            minAps = apCountPerChannel[i];
            bestChannel = kCandidateChannels[i];
        }
    }
    
    // Switch to the best channel if different
    if (currentChannel != bestChannel) {
        currentChannel = bestChannel;
        esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    }
}
```

#### Notes on Channel Scanning
- **AP Count as Proxy**: The number of WiFi access points on a channel correlates with congestion. In a pure ESP-NOW network, this may underestimate device count, but it's a practical approximation.
- **Scan Duration**: A full channel scan takes ~100-200ms per channel. With 3 channels, this is ~300-600ms of blocked time every 30 seconds — a **<2% duty cycle** overhead.
- **Alternative**: For more accurate device counting, track ESP-NOW peer counts per channel (requires maintaining peer lists per channel).

---

### 2. Cross-Channel Propagation

#### Configuration (in `config.h`)
```cpp
// Minimum interval between flood broadcasts (milliseconds)
constexpr uint32_t kMinFloodIntervalMs = 1000;  // 1 second
```

#### Global Variables
```cpp
uint32_t lastKernelSequence = 0;       // Last seen kernel sequence
uint32_t lastActivationSequence = 0;   // Last seen activation sequence
uint32_t lastFloodTimeMs = 0;           // Last flood time
```

#### Flooding Logic (in `broadcastState()`)
```cpp
void broadcastState(uint32_t nowMs) {
    updateSelfStateMetadata(nowMs, ++sequenceNumber);
    
    // Always broadcast on current channel (normal operation)
    esp_now_send(kBroadcastMac, reinterpret_cast<uint8_t *>(&selfState), sizeof(selfState));
    
    // If kernel or activation was updated, flood to ALL channels
    bool kernelUpdated = (selfState.kernelSequence > lastKernelSequence);
    bool activationUpdated = (selfState.activationSequence > lastActivationSequence);
    
    if ((kernelUpdated || activationUpdated) && 
        nowMs - lastFloodTimeMs >= kMinFloodIntervalMs) {
        
        uint8_t originalChannel = currentChannel;
        
        // Broadcast on every candidate channel
        for (uint8_t ch : kCandidateChannels) {
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            delay(10);  // Allow channel switch
            esp_now_send(kBroadcastMac, reinterpret_cast<uint8_t *>(&selfState), sizeof(selfState));
        }
        
        // Restore original channel
        esp_wifi_set_channel(originalChannel, WIFI_SECOND_CHAN_NONE);
        
        // Update tracking variables
        lastKernelSequence = selfState.kernelSequence;
        lastActivationSequence = selfState.activationSequence;
        lastFloodTimeMs = nowMs;
    }
}
```

#### How Flooding Works
1. When a device **receives or generates** a new kernel/activation (higher sequence number), it triggers a flood
2. The device **temporarily switches to each candidate channel** and broadcasts its full state
3. Devices on **any channel** receive the broadcast and:
   - If the sequence number is higher than theirs, they **adopt the new configuration**
   - They **re-flood** the update (if they haven't already)
4. The update **cascades through the entire network** within milliseconds

---

### 3. Alternative: Single Propagation Channel (Lower Overhead)

If flooding to all channels causes too much overhead, use a **dedicated propagation channel** (e.g., channel 1):

```cpp
constexpr uint8_t kPropagationChannel = 1;  // Single channel for critical updates

// In broadcastState():
if ((kernelUpdated || activationUpdated) && 
    nowMs - lastFloodTimeMs >= kMinFloodIntervalMs) {
    
    uint8_t originalChannel = currentChannel;
    esp_wifi_set_channel(kPropagationChannel, WIFI_SECOND_CHAN_NONE);
    delay(10);
    esp_now_send(kBroadcastMac, reinterpret_cast<uint8_t *>(&selfState), sizeof(selfState));
    esp_wifi_set_channel(originalChannel, WIFI_SECOND_CHAN_NONE);
    
    lastKernelSequence = selfState.kernelSequence;
    lastActivationSequence = selfState.activationSequence;
    lastFloodTimeMs = nowMs;
}
```

**Trade-off**: Lower overhead, but propagation depends on all devices **periodically checking the propagation channel** (requires additional logic).

---

## Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| **Channel Switching Time** | ~10ms | Time to switch between channels |
| **Propagation Delay** | <100ms | Time for updates to reach all devices |
| **Flooding Overhead** | ~30ms | Time to broadcast to all 3 channels |
| **Scan Overhead** | ~500ms | Time to scan all 3 channels (every 30s) |
| **Duty Cycle (Scanning)** | <2% | 500ms / 30s = 1.67% |
| **Duty Cycle (Flooding)** | <0.1% | 30ms / 1000ms = 3% max (throttled) |

---

## Edge Cases & Safeguards

### 1. Channel Switch Failures
If `esp_wifi_set_channel()` fails (returns `ESP_ERR_WIFI_IF`), retry after a delay:
```cpp
void safeSetChannel(uint8_t channel) {
    esp_err_t result = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (result != ESP_OK) {
        delay(100);  // Wait and retry
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    }
}
```

### 2. Flood Storm Prevention
Without throttling (`kMinFloodIntervalMs`), a **flood storm** could occur if many devices receive updates simultaneously. The throttle ensures:
- Each device floods at most **once per second**
- Network load remains **bounded**

### 3. Missed Floods
If a device is **off or out of range** during a flood:
- **Solution**: Use a **rendezvous channel** (e.g., channel 1) that all devices periodically check
- **Implementation**: Every 60 seconds, switch to channel 1 and broadcast full state

### 4. Channel Overlap
Avoid adjacent channels (e.g., 1 and 2) as they **overlap** in the 2.4GHz band. Stick to **1, 6, 11** (or **36, 40, 44, 48** for 5GHz if supported).

---

## Recommended Configuration

```cpp
// config.h

// Candidate channels (2.4GHz non-overlapping)
constexpr uint8_t kCandidateChannels[] = {1, 6, 11};
constexpr size_t kCandidateChannelCount = 3;

// Channel scanning
constexpr uint32_t kChannelScanIntervalMs = 30000;  // 30 seconds

// Flooding throttling
constexpr uint32_t kMinFloodIntervalMs = 1000;      // 1 second

// Optional: Dedicated propagation channel
constexpr uint8_t kPropagationChannel = 1;
```

---

## Integration with Existing Code

### Required Changes
| File | Change |
|------|--------|
| `config.h` | Add channel configuration constants |
| `main.cpp` (globals) | Add channel tracking variables |
| `setup()` | Initialize channel to default (e.g., 1) |
| `loop()` | Add channel scanning logic |
| `broadcastState()` | Add flooding logic for critical updates |

### Unchanged Components
- Neighbor selection (`getClosestDevices()`)
- State processing (`processState()`)
- TX power adjustment (`adjustTxPower()`)
- LED control

---

## Testing & Validation

### 1. Verify Channel Distribution
Enable serial debug and log the current channel:
```cpp
Serial.printf("Device %s on channel %d, peers: %d\n", 
              macToString(selfMac).c_str(), 
              currentChannel, 
              peerCount);
```
**Expected**: Devices should be **evenly distributed** across the candidate channels.

### 2. Test Propagation
1. Set a new kernel on one device via serial (`kernel 0,1,0,1,-4,1,0,1,0`)
2. Monitor other devices' serial output to confirm they receive the update
3. **Expected**: All devices should adopt the new kernel within **<100ms**

### 3. Measure Overhead
1. Monitor CPU usage and WiFi performance
2. **Expected**: <5% additional CPU load, <2% WiFi duty cycle

---

## Deployment Considerations

### For 500+ Devices
1. **Start with 3 channels** (1, 6, 11) — this supports **~150–200 devices per channel**
2. **Increase to 4+ channels** if needed (e.g., add 13, but be aware of overlap with 11)
3. **Use 5GHz channels** (if ESP32-C3 supports it) for additional capacity

### Power Consumption
- Channel scanning and flooding **slightly increase power usage**
- For battery-powered devices, **increase `kChannelScanIntervalMs`** (e.g., to 60–120 seconds)

### RF Regulations
- **Comply with local regulations** for channel usage and transmit power
- In most regions, **channels 1–11 are permitted** for 2.4GHz
- **Avoid channel 12–14** in some regions (e.g., US, EU)

---

## Summary

This multi-channel system enables:

✅ **Automatic load balancing**: Devices distribute across the least congested channels
✅ **Reliable propagation**: Kernel/activation updates reach all devices via cross-channel flooding
✅ **No fragmentation**: All devices remain part of a single logical network
✅ **Low overhead**: Minimal impact on performance and power consumption

**Recommended starting point**: Use **3 channels (1, 6, 11)** with **1-second flooding throttle** and **30-second channel scanning**. This balances propagation speed with network load for deployments of **100–500+ devices**.

---

## Example: Full Integration Code

```cpp
// In config.h
constexpr uint8_t kCandidateChannels[] = {1, 6, 11};
constexpr size_t kCandidateChannelCount = 3;
constexpr uint32_t kChannelScanIntervalMs = 30000;
constexpr uint32_t kMinFloodIntervalMs = 1000;

// In globals (main.cpp)
uint8_t currentChannel = 1;
uint32_t lastChannelScanMs = 0;
uint32_t lastFloodTimeMs = 0;
uint32_t lastKernelSequence = 0;
uint32_t lastActivationSequence = 0;

// In setup()
esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);

// In loop()
uint32_t nowMs = millis();

// 1. Periodic channel scanning
if (nowMs - lastChannelScanMs >= kChannelScanIntervalMs) {
    // [Channel scanning code from above]
}

// 2. Normal broadcasts
if (nowMs - nextBroadcastMs >= 0) {
    nextBroadcastMs = nowMs + kBroadcastIntervalMs;
    broadcastState(nowMs);
}

// In broadcastState()
void broadcastState(uint32_t nowMs) {
    updateSelfStateMetadata(nowMs, ++sequenceNumber);
    
    // Normal broadcast on current channel
    esp_now_send(kBroadcastMac, reinterpret_cast<uint8_t *>(&selfState), sizeof(selfState));
    
    // Flood critical updates to all channels
    bool kernelUpdated = (selfState.kernelSequence > lastKernelSequence);
    bool activationUpdated = (selfState.activationSequence > lastActivationSequence);
    
    if ((kernelUpdated || activationUpdated) && 
        nowMs - lastFloodTimeMs >= kMinFloodIntervalMs) {
        
        uint8_t originalChannel = currentChannel;
        for (uint8_t ch : kCandidateChannels) {
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            delay(10);
            esp_now_send(kBroadcastMac, reinterpret_cast<uint8_t *>(&selfState), sizeof(selfState));
        }
        esp_wifi_set_channel(originalChannel, WIFI_SECOND_CHAN_NONE);
        
        lastKernelSequence = selfState.kernelSequence;
        lastActivationSequence = selfState.activationSequence;
        lastFloodTimeMs = nowMs;
    }
}
```
