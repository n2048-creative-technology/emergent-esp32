# Firefly Proximity Network

A decentralized mesh firmware for ESP32-C3 devices that enables emergent swarm behavior through local peer-to-peer communication using ESP-NOW.

## Overview

Each device operates as an independent node in a peer-to-peer network, broadcasting its state and observing nearby neighbors. The system demonstrates emergent collective behavior through simple local rules, with no central coordinator.

## Features

### Core Communication
- **ESP-NOW Protocol**: Peer-to-peer WiFi communication without an access point
- **Broadcast Mesh**: Each device broadcasts to all neighbors on a configurable WiFi channel
- **State Propagation**: Devices share their state (value, rules, TX power, chip temperature, uptime) with the network

### Adaptive Radio
- **Automatic TX Power Control**: Dynamically adjusts transmit power to maintain 8-10 neighbors
- **Power Range**: 0-100% of max (21 dBm / 84 units), adjustable in config
- **Closed-Loop Control**: Measures neighbor count, adjusts in 1 dBm steps every 5 seconds

### Distributed State
- **Single State Value**: Each device maintains a single float value
- **Rules Array**: 9 coefficients that define how neighbor values influence own value
- **Dot Product Calculation**: `value = Σ(rules[i] × inputs[i])` where inputs = [n0.value, n1.value, ..., n7.value, own.value]
- **Random Initialization**: Devices start with random values and rules

### Peer Management
- **Neighbor Tracking**: Each device tracks up to 24 known peers
- **Freshness Timeout**: Peers expire after 10 seconds of silence
- **Closest Selection**: Top 8 peers by RSSI used for state processing
- **RSSI Metrics**: Tracks strongest RSSI, average RSSI, and neighbor count

## Configuration

Edit `src/config.h` to customize behavior:

```cpp
// Protocol
kProtocolMagic    - Packet identification (0x46464C59 = "FFLY")
kProtocolVersion  - Protocol version for compatibility

// Radio
kWifiChannel          - WiFi channel (1-13)
kInitialTxPowerPercentage - Starting TX power % (0-100)

// Timing
kBroadcastIntervalMs   - State broadcast interval (ms)
kPrintIntervalMs       - Debug print interval (ms)
kProcessIntervalMs     - State processing interval (ms)
kPeerTimeoutMs         - Peer freshness timeout (ms)

// Peer Management
kMaxKnownPeers     - Maximum tracked peers per device
kClosestPeerCount  - Number of closest peers for state processing
```

## Expected Hive Behavior (Hundreds of Devices)

### Network Topology
- **Local Clusters**: Each device maintains connections to its 8-10 closest neighbors (by RSSI)
- **Dynamic Range**: TX power auto-adjusts so each device reaches approximately 8-10 neighbors
- **No Global View**: Devices only know about their immediate neighbors, not the entire swarm

### Emergent Patterns
1. **Self-Organization**: Devices automatically form local neighborhoods based on physical proximity
2. **Power Stabilization**: TX power converges to the minimum needed for 8-10 neighbors, reducing channel congestion
3. **State Convergence**: The 8 state values gradually converge across connected regions through weighted averaging
4. **Resilience**: Network automatically reconfigures when devices move, power off, or are added/removed

### Channel Dynamics
- **Spatial Reuse**: Non-overlapping clusters can operate simultaneously on the same channel
- **Collision Handling**: ESP-NOW broadcast storms are mitigated by:
  - Limited peer tracking (24 max)
  - Adaptive TX power (reduces range, increases spatial reuse)
  - Periodic (not continuous) broadcasting
- **Scalability Limit**: With hundreds of devices in close proximity:
  - Channel 1 will experience high collision rates
  - Each device only tracks its ~24 closest peers
  - Effective network diameter: ~3-4 hops for full propagation

### Data Flow
1. Each device broadcasts its `DeviceState` every `kBroadcastIntervalMs`
2. `DeviceState` includes:
   - Protocol headers (magic, version)
   - Device identity (MAC address)
   - Sequence numbers (main, rules, value) and uptime
   - Current TX power (in ESP32 units)
   - Chip temperature
   - Single state value
   - 9 coefficients (rules array)
3. Receiving devices:
   - Store sender in peer list with RSSI
   - Propagate rules if neighbor has newer `rulesSequence`
   - Propagate value reset if neighbor has newer `valueSequence`
   - Compute new value as dot product of rules and neighbor values
   - Adjust own TX power based on peer count

### Visualization Output
Serial output (115200 baud) shows:
```
---- full state ----
self mac=AA:BB:CC:DD:EE:FF seq=42 uptime=120 tx=42 temp=35.7C value=1.2345 rseq=5 vseq=3 rules=[0.12, -0.34, ...]
neighbor[0] rssi=-45 age=120 state mac=11:22:33:44:55:66 seq=41 uptime=118 tx=38 temp=36.1C value=2.3456 rseq=5 vseq=3 rules=[0.12, -0.34, ...]
...
--------------------
```

## Building & Flashing

### PlatformIO
```bash
pio run -t upload
pio device monitor
```

### Target Hardware
- **Board**: Seeed Studio XIAO ESP32-C3
- **Framework**: Arduino-ESP32
- **Dependencies**: ESP-IDF WiFi/ESP-NOW stack, ESPCPUTemp library

## Usage

### Serial Commands
Send commands via serial monitor (115200 baud):

| Command | Description |
|---------|-------------|
| `rules r0,r1,r2,r3,r4,r5,r6,r7,r8` | Set 9 rule coefficients (e.g., `rules 0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9`) |
| `reset` | Reset state value to random, propagates to all neighbors |

Rules and resets propagate automatically through the mesh — no need to send to each device individually.

## Technical Notes

### ESP-NOW Details
- Uses broadcast MAC address: `FF:FF:FF:FF:FF:FF`
- Frame size: ~76 bytes (well under 250 byte limit)
- No encryption (for maximum compatibility)

### State Calculation
- `value = rules[0]×n0.value + rules[1]×n1.value + ... + rules[8]×own.value`
- Input vector: [neighbor0.value, neighbor1.value, ..., neighbor7.value, own.value]
- NaN/Inf protection: Auto-resets to random value

### TX Power Units
- ESP32 uses units of 0.25 dBm
- Max: 84 units = 21 dBm = 125 mW
- Adjustment: ±4 units = ±1 dBm per step

### Sequence Numbers
- `sequence`: Main message counter
- `rulesSequence`: Increments when rules are updated via serial
- `valueSequence`: Increments when value is reset via serial
- Higher sequence numbers propagate automatically through the network

### Memory Usage
- Peer storage: 24 peers × ~100 bytes = ~2.4 KB RAM
- State storage: ~80 bytes per DeviceState
- Total RAM: Well under ESP32-C3's 384 KB

## License

Open source for research and experimentation.