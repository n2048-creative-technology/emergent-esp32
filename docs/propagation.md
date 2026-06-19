# Kernel and Activation Propagation Mechanism

This document describes how new cellular automaton **kernels** and **activation rules** propagate through the network of devices.

## Overview

The system uses a **gossip-based propagation protocol** where each device broadcasts its complete state (including kernel and activation configurations) to all peers. Devices adopt new configurations from their neighbors based on **sequence numbers**, ensuring the most recent updates eventually spread across the entire network.

## Data Structures

Each `DeviceState` contains:

### Kernel Configuration
- `kernel[9]`: Array of 9 floating-point values (3×3 convolution kernel)
- `kernelSequence`: Incrementing counter that tracks kernel version

### Activation Rules
- `activations[8]`: Array of activation conditions (operator + threshold value)
- `activationCount`: Number of active activation rules (0-8)
- `activationSequence`: Incrementing counter that tracks activation version

## Propagation Mechanism

### 1. Broadcast Transmission
Every `kBroadcastIntervalMs` (default: 250ms), each device broadcasts its complete `DeviceState` via ESP-NOW:

```cpp
void broadcastState(uint32_t nowMs) {
  updateSelfStateMetadata(nowMs, ++sequenceNumber);
  esp_now_send(kBroadcastMac, reinterpret_cast<uint8_t *>(&selfState), sizeof(selfState));
}
```

This broadcast includes:
- Current kernel and its sequence number
- Current activation rules and their sequence number
- Current state value and its sequence number

### 2. Reception and Storage
When a device receives a broadcast, it stores the peer's state via `storePeerObservation()`:

```cpp
void storePeerObservation(const uint8_t *mac, int8_t rssi, const DeviceState &state) {
  // Stores the received state, including kernel and activations
  peers[index].advertisedState = state;
}
```

### 3. Neighbor Selection
During the main loop, the device selects its `kClosestPeerCount` (default: 8) nearest neighbors based on RSSI (see [neighbors.md](./neighbors.md)).

### 4. Configuration Adoption
The `processState()` function (in `state_processing.h`) receives:
- The device's own state (`selfState`)
- The closest neighbors' states (`closestDevices`)

**Configuration propagation occurs when a device detects a newer version from its neighbors:**

#### Kernel Propagation
A device adopts a neighbor's kernel if:
- The neighbor's `kernelSequence` > device's current `kernelSequence`
- The device decides to update (implementation-specific in `processState()`)

When adopted:
```cpp
memcpy(selfState.kernel, neighborState.kernel, sizeof(selfState.kernel));
selfState.kernelSequence = neighborState.kernelSequence;  // Or increment
```

#### Activation Propagation
A device adopts a neighbor's activation rules if:
- The neighbor's `activationSequence` > device's current `activationSequence`

When adopted:
```cpp
memcpy(selfState.activations, neighborState.activations, sizeof(selfState.activations));
selfState.activationCount = neighborState.activationCount;
selfState.activationSequence = neighborState.activationSequence;
```

### 5. Sequence Number Semantics

| Field | Purpose | Incremented When |
|-------|---------|------------------|
| `kernelSequence` | Tracks kernel version | Local kernel is updated via serial command or preset |
| `activationSequence` | Tracks activation rules version | Local activations are updated via serial command or preset |
| `valueSequence` | Tracks state value | Local state value changes |

Sequence numbers use **monotonically increasing counters**, ensuring that:
- Newer updates always have higher sequence numbers
- Conflicts are resolved by always preferring the higher sequence number
- Updates propagate reliably without requiring acknowledgments

## Local Update Triggers

Kernels and activations can be updated locally via serial commands, which **increment the sequence numbers**:

### Kernel Update
```cpp
// Serial command: "kernel 0,1,0,1,-4,1,0,1,0"
selfState.kernel[index++] = atof(token);
// ... parse all 9 values ...
selfState.kernelSequence++;  // Critical: marks this as a new version
```

### Activation Update
```cpp
// Serial command: "activations 4,2.0,0,-2.0"
selfState.activations[count].op = op;
selfState.activations[count].value = val;
selfState.activationCount = count;
selfState.activationSequence++;  // Marks activations as updated
```

### Preset Loading
```cpp
// Serial command: "preset rule30"
if (loadPreset(selfState, presetName)) {
  selfState.kernelSequence++;
  selfState.activationSequence++;
}
```

## Propagation Dynamics

### Wavefront Propagation
When a device receives a new configuration:
1. It updates its own kernel/activations and increments the local sequence number
2. On the next broadcast, it shares the new configuration with all peers
3. Neighbors receive the broadcast and adopt if the sequence number is higher
4. The update cascades through the network at the speed of the broadcast interval

### Propagation Speed
- **Hop-to-hop delay**: 1 × `kBroadcastIntervalMs` (250ms default)
- **Network-wide propagation**: O(diameter) × 250ms
- In a dense network with 8 neighbors per device, updates can reach all devices in 2-3 hops

### Example Propagation

Device A receives a new kernel via serial (sequence 5):
```
t=0ms:   A updates kernel (seq=5)
t=250ms: A broadcasts state with kernel seq=5
t=250ms: B, C, D receive from A, adopt kernel (seq=5)
t=500ms: B, C, D broadcast with kernel seq=5
t=500ms: E, F, G receive from B/C/D, adopt kernel (seq=5)
t=750ms: E, F, G broadcast with kernel seq=5
... and so on
```

Within ~1-2 seconds, the entire connected network adopts the new configuration.

## Conflict Resolution

When a device receives **competing updates** (different kernels/activations with the same or different sequence numbers):

1. **Higher sequence number always wins** (most recent update)
2. **Same sequence number**: Current configuration is retained (no change)
3. **Lower sequence number**: Ignored (already have newer version)

This ensures **eventual consistency** across the network.

## Configuration Presets

The system includes built-in presets (defined in `state_processing.h`):
- `conway`: Conway's Game of Life rules
- `rule30`: Rule 30 cellular automaton
- `majority`: Majority vote among neighbors
- `and`: Logical AND of neighbors
- `or`: Logical OR of neighbors

Each preset sets both the kernel and activation rules, then increments both sequence numbers.

## Key Properties

### Reliability
- **At-least-once delivery**: Broadcasts may be received by all peers or some subset
- **Eventual consistency**: All devices will eventually converge to the highest sequence number
- **No single point of failure**: Any device can introduce a new configuration

### Scalability
- **Local decisions only**: Each device independently decides whether to adopt updates
- **No central coordination**: The network self-organizes
- **Bounded overhead**: Each broadcast carries the full state (fixed size)

### Resilience
- **Temporary disconnections**: Devices retain their last-known configuration
- **Rejoin behavior**: When a device rejoins, it receives the latest configurations from its new neighbors
- **Sequence number persistence**: Sequence numbers persist across reboots (stored in `selfState`)

## Relationship with Cellular Automaton State

The propagation of kernels and activations is **orthogonal** to the propagation of the cellular automaton state value:

| Aspect | Kernel/Activation Propagation | State Value Propagation |
|--------|-------------------------------|--------------------------|
| Trigger | Sequence number comparison | Cellular automaton rules |
| Frequency | On every broadcast reception | Every `kProcessIntervalMs` (250ms) |
| Scope | Configuration update | State computation |
| Direction | Gossip (push) | Cellular automaton (local computation) |

Both mechanisms work together to enable:
1. **Dynamic reconfiguration**: Change the rules of the automaton across the network
2. **Pattern formation**: State values propagate according to the current rules
3. **Self-organization**: The network adapts its behavior based on local interactions

## Design Rationale

This gossip-based propagation with sequence numbers provides:

1. **Simplicity**: No complex routing or acknowledgment protocols
2. **Robustness**: Works despite message loss or network partitions
3. **Decentralization**: No leader or central controller required
4. **Efficiency**: Minimal overhead (piggybacks on existing broadcasts)
5. **Flexibility**: Any device can initiate configuration changes

The combination of sequence numbers and periodic broadcasts ensures that configuration updates reliably propagate through the network while maintaining the cellular automaton's emergent behavior.
