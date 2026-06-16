# Firefly Proximity Network

A decentralized mesh firmware for ESP32-C3 devices that enables emergent swarm behavior through local peer-to-peer communication using ESP-NOW.

## Overview

Each device operates as an independent node in a peer-to-peer network, broadcasting its state and observing nearby neighbors. The system demonstrates emergent collective behavior through simple local rules, with no central coordinator.

## Features

### Core Communication
- **ESP-NOW Protocol**: Peer-to-peer WiFi communication without an access point
- **Broadcast Mesh**: Each device broadcasts to all neighbors on a configurable WiFi channel
- **State Propagation**: Devices share their state (values, TX power, chip temperature, uptime) with the network

### Adaptive Radio
- **Automatic TX Power Control**: Dynamically adjusts transmit power to maintain 8-10 neighbors
- **Power Range**: 0-100% of max (21 dBm / 84 units), adjustable in config
- **Closed-Loop Control**: Measures neighbor count, adjusts in 1 dBm steps every 5 seconds

### Distributed State
- **8 State Values**: Each device maintains 8 integer values that propagate through the network
- **Weighted Averaging**: Values converge toward the average of neighboring devices (own*3/4 + neighbor_avg*1/4)
- **Random Initialization**: Each device starts with random values in range [-10000, 10000]

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
1. Each device broadcasts its `DeviceState` every `kBroadcastIntervalMs` (1000ms)
2. `DeviceState` includes:
   - Protocol headers (magic, version)
   - Device identity (MAC address)
   - Sequence number and uptime
   - Current TX power (in ESP32 units)
   - 8 state values
3. Receiving devices:
   - Store sender in peer list with RSSI
   - Process state values through weighted averaging
   - Adjust own TX power based on peer count

### Visualization Output
Serial output (115200 baud) shows:
```
---- full state ----
self mac=AA:BB:CC:DD:EE:FF seq=42 uptime=120 tx=42 values=[5, -45, -50, 120, 100, 200, 0, 0]
neighbor[0] rssi=-45 age=120 state mac=11:22:33:44:55:66 seq=41 uptime=118 tx=38 values=[6, -42, ...]
neighbor[1] rssi=-50 age=200 state mac=...
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
- **Dependencies**: ESP-IDF WiFi/ESP-NOW stack

## Technical Notes

### ESP-NOW Details
- Uses broadcast MAC address: `FF:FF:FF:FF:FF:FF`
- Frame size: ~52 bytes (well under 250 byte limit)
- No encryption (for maximum compatibility)

### TX Power Units
- ESP32 uses units of 0.25 dBm
- Max: 84 units = 21 dBm = 125 mW
- Adjustment: ±4 units = ±1 dBm per step

### Memory Usage
- Peer storage: 24 peers × ~100 bytes = ~2.4 KB RAM
- State storage: Minimal (single DeviceState struct)
- Total RAM: Well under ESP32-C3's 384 KB

## License

Open source for research and experimentation.