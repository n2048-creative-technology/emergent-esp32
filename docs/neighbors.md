# Neighbor Selection Mechanism

This document describes how the system selects the closest neighboring devices from the pool of known peers.

## Overview

The system maintains a dynamic list of the **8 nearest neighbors** (configurable via `kClosestPeerCount` in `config.h`) based on **received signal strength (RSSI)**. This selection happens periodically during the main loop execution.

## Data Structures

### KnownPeer
Stores information about all discovered devices:
- `valid`: Boolean flag indicating if the peer entry is active
- `mac`: The device's MAC address (6 bytes)
- `rssi`: Received Signal Strength Indicator (higher = stronger/closer signal)
- `lastSeenMs`: Timestamp of the most recent message received from this peer
- `advertisedState`: The latest state data broadcast by the peer

### ClosestDeviceState
Represents a selected neighbor in the final sorted list:
- `valid`: Whether this slot contains a valid neighbor
- `rssi`: The signal strength of this neighbor
- `ageMs`: Time elapsed since the last message was received from this neighbor
- `state`: The peer's advertised state data

## Selection Process

The neighbor selection is performed by the `getClosestDevices()` function, which implements the following algorithm:

### 1. Filtering Fresh Peers
A peer is considered **eligible** only if it is:
- Marked as `valid`
- Has been seen within the timeout period (`kPeerTimeoutMs`, default: 10,000ms)

The `peerIsFresh()` function performs this check:
```cpp
bool peerIsFresh(const KnownPeer &peer, uint32_t nowMs) {
  return peer.valid && (nowMs - peer.lastSeenMs) <= kPeerTimeoutMs;
}
```

### 2. Selection Algorithm
The function uses an **insertion sort** approach to maintain the top N peers by RSSI:

1. Initialize an array of indices (`bestIndexes`) with -1 (invalid)
2. For each known peer (up to `kMaxKnownPeers`, default: 24):
   - Skip if the peer is not fresh
   - Compare the peer's RSSI against each slot in the current top list
   - If the peer's RSSI is higher than a slot's RSSI:
     - Shift all lower slots down by one position
     - Insert the peer's index at the appropriate position
     - Break (each peer appears at most once)
3. Populate the output array with data from the selected peers

### 3. Output
The function fills the `ClosestDeviceState` array with:
- The peer's validity status
- Its RSSI value
- Its age (time since last message)
- Its complete advertised state

## Key Characteristics

- **Metric**: RSSI (Received Signal Strength Indicator) is the sole metric for proximity
  - Higher RSSI values indicate stronger signals and thus closer devices
  - RSSI is typically negative (e.g., -50 is stronger than -80)

- **Freshness**: Only peers that have communicated within `kPeerTimeoutMs` (10 seconds) are considered

- **Sort Order**: Neighbors are sorted in **descending order** by RSSI
  - Slot 0 contains the neighbor with the strongest signal
  - Slot N-1 contains the neighbor with the weakest signal among the selected group

- **Thread Safety**: The selection process is protected by a critical section (`portENTER_CRITICAL`/`portEXIT_CRITICAL`) to prevent race conditions when accessing the shared peer list

- **Dynamic Updates**: The peer list is continuously updated as new ESP-NOW messages arrive via the `onEspNowReceive()` callback, which calls `storePeerObservation()` to add or update peer entries

## Configuration Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `kMaxKnownPeers` | 24 | Maximum number of peers to track |
| `kClosestPeerCount` | 8 | Number of nearest neighbors to select |
| `kPeerTimeoutMs` | 10,000 | Time window (ms) for considering a peer "fresh" |

## Usage in State Processing

The selected nearest neighbors are passed to the `processState()` function, which uses their states to compute the next state of the current device. This enables the cellular automaton behavior where each device's state depends on its local neighborhood.

## Example

If 15 peers are currently known and 10 of them have been seen within the last 10 seconds:
1. All 15 peers are checked for freshness
2. Only the 10 fresh peers are considered eligible
3. The 8 peers with the highest RSSI values are selected
4. These 8 are sorted by RSSI (highest to lowest) and returned in the `ClosestDeviceState` array
