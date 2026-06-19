# Transmission Power Adjustment Mechanism

This document describes how the system dynamically adjusts its WiFi transmission power to maintain an optimal number of neighboring devices.

## Overview

The system implements a **closed-loop feedback controller** that automatically adjusts the ESP32's transmission power based on the number of active peers in range. This ensures each device maintains connectivity with a target number of neighbors (default: 2-10 peers) while minimizing power consumption.

## Initial Setup

At startup, the transmission power is initialized based on `kInitialTxPowerPercentage`:

```cpp
currentTxPower = static_cast<int8_t>(84 * kInitialTxPowerPercentage / 100);
```

- **ESP32 TX Power Units**: The ESP32 uses units of **0.25 dBm**
- **Maximum Power**: 84 units = **21 dBm** (the hardware maximum)
- **Default Initial Power**: 10% of maximum = 8.4 units (≈ 2.1 dBm)

## Feedback Loop

The `adjustTxPower()` function runs periodically (every `kAdjustIntervalMs`, default: 5000ms) and performs the following:

### 1. Check Adjustment Interval
```cpp
if (nowMs - lastAdjustMs < kAdjustIntervalMs) return;
```
The adjustment only occurs every 5 seconds to avoid rapid fluctuations.

### 2. Count Fresh Peers
```cpp
int peerCount = 0;
portENTER_CRITICAL(&peerMux);
for (const KnownPeer& peer : peers) {
  if (peerIsFresh(peer, nowMs)) peerCount++;
}
portEXIT_CRITICAL(&peerMux);
```
Counts how many peers have been seen within the `kPeerTimeoutMs` window (10,000ms).

### 3. Apply Adjustment Rules

| Condition | Action | Rationale |
|-----------|--------|-----------|
| `peerCount > kTargetMaxPeers` (default: 10) | **Decrease** power by `kPowerStep` (default: 4 units) | Too many peers → reduce range to save power |
| `peerCount < kTargetMinPeers` (default: 2) | **Increase** power by `kPowerStep` (default: 4 units) | Too few peers → increase range to find more |
| Between min and max | **No change** | Optimal peer count maintained |

```cpp
if (peerCount > kTargetMaxPeers) {
  currentTxPower = max<int8_t>(0, currentTxPower - kPowerStep);
  esp_wifi_set_max_tx_power(currentTxPower);
} else if (peerCount < kTargetMinPeers) {
  currentTxPower = min<int8_t>(84, currentTxPower + kPowerStep);
  esp_wifi_set_max_tx_power(currentTxPower);
}
```

### 4. Clamping
The transmission power is always clamped to the valid range:
- **Minimum**: 0 units (0 dBm)
- **Maximum**: 84 units (21 dBm)

## Configuration Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `kInitialTxPowerPercentage` | 10 | Starting TX power as % of max (0-100) |
| `kAdjustIntervalMs` | 5000 | Time between power adjustments (ms) |
| `kTargetMaxPeers` | 10 | Upper threshold: reduce power if exceeded |
| `kTargetMinPeers` | 2 | Lower threshold: increase power if below |
| `kPowerStep` | 4 | Adjustment increment (in 0.25 dBm units) |

## Algorithm Behavior

### Power Reduction (Too Many Peers)
When more than 10 peers are detected:
1. Current power is **decreased by 4 units** (≈ 1 dBm)
2. New power is applied immediately via `esp_wifi_set_max_tx_power()`
3. This reduces the device's radio range, causing some distant peers to fall out of range

### Power Increase (Too Few Peers)
When fewer than 2 peers are detected:
1. Current power is **increased by 4 units** (≈ 1 dBm)
2. New power is applied immediately
3. This extends the device's radio range, potentially discovering new peers

### Hysteresis
The system uses a **dead band** between 2-10 peers where no adjustment occurs. This prevents:
- Rapid power oscillations
- Unnecessary adjustments for minor peer count fluctuations
- Excessive power changes in stable environments

## Example Scenario

1. **Start**: Power = 8 units (2 dBm), 0 peers detected
2. **t=5s**: Only 1 peer → power increases to 12 units (3 dBm)
3. **t=10s**: Still 1 peer → power increases to 16 units (4 dBm)
4. **t=15s**: Now 5 peers → **no change** (within 2-10 range)
5. **t=20s**: 12 peers detected → power decreases to 12 units (3 dBm)
6. **t=25s**: 11 peers → power decreases to 8 units (2 dBm)
7. **t=30s**: 8 peers → **no change** (within range)

## Technical Details

### ESP32 TX Power
- The ESP32's `esp_wifi_set_max_tx_power()` function accepts values in **0.25 dBm units**
- Valid range: **0 to 84** (0 dBm to 21 dBm)
- Each unit = 0.25 dBm, so 4 units = 1 dBm

### Thread Safety
The peer counting is protected by a critical section (`portENTER_CRITICAL`/`portEXIT_CRITICAL`) to prevent race conditions when accessing the shared peer list.

### Integration
The `adjustTxPower()` function is called in the main loop at every iteration, but only performs adjustments every `kAdjustIntervalMs` milliseconds.

## Design Rationale

This adaptive power control serves multiple purposes:

1. **Power Efficiency**: Devices use the minimum power necessary to maintain connectivity
2. **Network Stability**: Prevents all devices from transmitting at maximum power, reducing interference
3. **Scalability**: In dense networks, devices automatically reduce range to avoid overwhelming each other
4. **Reliability**: In sparse networks, devices increase range to maintain connectivity
5. **Autonomy**: No manual configuration needed; the system self-optimizes

## Relationship with Neighbor Selection

The TX power adjustment works in tandem with the neighbor selection mechanism:
- **Neighbor selection** determines which peers are "closest" based on RSSI
- **TX power adjustment** controls how many peers are within range
- Together, they ensure each device maintains an optimal local neighborhood for the cellular automaton to function properly
