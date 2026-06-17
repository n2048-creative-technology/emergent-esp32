# Firefly Proximity Network

A decentralized mesh firmware for ESP32-C3 devices that enables emergent swarm behavior through local peer-to-peer communication using ESP-NOW.

## Overview

Each device operates as an independent node in a peer-to-peer network, broadcasting its state and observing nearby neighbors. The system demonstrates emergent collective behavior through simple local interactions, with no central coordinator.

## Features

### Core Communication
- **ESP-NOW Protocol**: Peer-to-peer WiFi communication without an access point
- **Broadcast Mesh**: Each device broadcasts to all neighbors on a configurable WiFi channel
- **State Propagation**: Devices share their state (value, kernel, TX power, chip temperature, uptime) with the network

### Adaptive Radio
- **Automatic TX Power Control**: Dynamically adjusts transmit power to maintain 8-10 neighbors
- **Power Range**: 0-100% of max (21 dBm / 84 units), adjustable in config
- **Closed-Loop Control**: Measures neighbor count, adjusts in 1 dBm steps every 5 seconds

### Distributed State
- **Binary State Value**: Each device maintains a single boolean value (0 or 1)
- **Kernel Array**: 9 float coefficients that define how neighbor values influence own value (convolution kernel)
- **Activation Functions**: Up to 8 activations that determine next state based on weighted sum
  - Each activation: operator (0="<", 1="<=", 2="==", 3=">=", 4=">") + threshold value
  - Next state = 1 if ANY activation is true (OR logic)
  - Falls back to threshold at 0 if no activations set
  - **Presets include both kernel and activations** for classic cellular automata behavior
- **Thresholded Calculation**: `value = (Σ(kernel[i] × inputs[i]) > 0) ? 1 : 0` where inputs = [n0.value, n1.value, ..., n7.value, own.value]
- **Random Initialization**: Devices start with random binary values and random kernel weights

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
   - Sequence numbers (main, kernel, value, activation) and uptime
   - Current TX power (in ESP32 units)
   - Chip temperature
   - Single state value (binary 0/1)
   - 9 coefficients (kernel array)
   - Up to 8 activations for state transitions (operator + threshold)
3. Receiving devices:
   - Store sender in peer list with RSSI
   - Propagate kernel if neighbor has newer `kernelSequence`
   - Propagate activations if neighbor has newer `activationSequence`
   - Propagate value reset if neighbor has newer `valueSequence`
   - Compute new value using kernel and activations
   - Adjust own TX power based on peer count

### Visualization Output
Serial output (115200 baud) shows:
```
---- full state ----
self mac=AA:BB:CC:DD:EE:FF seq=42 uptime=120 tx=42 temp=35.7C value=1 kseq=5 vseq=3 aseq=2 kernel=[0.12, -0.34, ...] activations=[<2.00, ==4.00]
neighbor[0] rssi=-45 age=120 state mac=11:22:33:44:55:66 seq=41 uptime=118 tx=38 temp=36.1C value=0 kseq=5 vseq=3 aseq=2 kernel=[0.12, -0.34, ...] activations=[<2.00, ==4.00]
...
--------------------
```

### Serial Output Field Reference

**Self line fields:**
| Field | Unit | Description |
|-------|------|-------------|
| `mac` | - | Device MAC address (unique identifier) |
| `seq` | - | Main sequence number (message counter) |
| `uptime` | ms | Time since device boot |
| `tx` | units | Current TX power (ESP32 units: 84 = 21 dBm = 125 mW) |
| `temp` | °C | Chip temperature from internal sensor |
| `value` | - | Current binary state: 0 or 1 |
| `kseq` | - | Kernel sequence number (increments when kernel is updated) |
| `vseq` | - | Value sequence number (increments when value is reset) |
| `aseq` | - | Activation sequence number (increments when activations are updated) |
| `kernel` | - | Array of 9 kernel weights [k0, k1, ..., k8] for neighbors + self |
| `activations` | - | Array of activation conditions (op,value pairs) |

**Neighbor line fields:**
| Field | Unit | Description |
|-------|------|-------------|
| `neighbor[N]` | - | Neighbor index (0-7, top 8 by RSSI) |
| `rssi` | dBm | Received signal strength (negative dBm: -45 is strong, -80 is weak) |
| `age` | ms | Time since last message received from this neighbor (stale if >10,000ms) |
| `state` | - | Start of neighbor's DeviceState fields |
| `mac` | - | Neighbor's MAC address |
| `seq` | - | Neighbor's main sequence number |
| `uptime` | ms | Neighbor's uptime |
| `tx` | units | Neighbor's TX power |
| `temp` | °C | Neighbor's temperature |
| `value` | - | Neighbor's current binary state |
| `kseq` | - | Neighbor's kernel sequence number |
| `vseq` | - | Neighbor's value sequence number |
| `aseq` | - | Neighbor's activation sequence number |
| `kernel` | - | Neighbor's kernel weights |
| `activations` | - | Neighbor's activation conditions |

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
| `kernel k0,k1,...,k8` | Set 9 kernel coefficients (e.g., `kernel 1,1,1,1,1,1,1,1,0`) |
| `preset <name>` | Load predefined CA: `conway` (Game of Life), `rule30`, `majority`, `and`, `or`. Sets both kernel and activations for classic behavior |
| `activations op1,val1,op2,val2,...` | Set up to 8 activations. op: 0="<", 1="<=", 2="==", 3=">=", 4=">". Example: `activations 0,2,2,4,2,12` (sum < 2 OR sum == 4 OR sum == 12) |
| `reset` | Reset state to random 0/1, propagates to all neighbors |
| `set AA:BB:CC:DD:EE:FF 0\1` | Set own state to 0 or 1 (only works for self MAC) |

Kernel, activations, and resets propagate automatically through the mesh — no need to send to each device individually.

## Graphical User Interface

A Python-based GUI (`firefly_gui.py`) is available for easy configuration and monitoring:

### Features
- **Serial Monitor Tab**: Real-time display of device output with connect/disconnect controls
- **Kernel Tab**: Set all 9 kernel weights with convenient preset buttons
- **Activations Tab**: Dynamically add/remove activation conditions with op/value pairs
- **Presets Tab**: Load predefined cellular automata (conway, rule30, majority, and, or) with descriptions

### Installation
```bash
pip install pyserial
```

### Usage
```bash
python3 firefly_gui.py
```

### Screenshot / Layout
- **Serial Monitor**: Select port, connect, view real-time output
- **Kernel**: 9 input fields for kernel weights, preset buttons (All 1, All 0, Neighbors Only)
- **Activations**: Add/remove activation rows, each with operator (0-4) and value
- **Presets**: Dropdown to select preset, displays description, buttons to load preset into UI or send directly to device

### Workflow
1. Connect to device via Serial Monitor tab
2. Configure kernel weights in Kernel tab (or load preset)
3. Configure activation conditions in Activations tab (or load preset)
4. Send configuration to device
5. Monitor device behavior in real-time

### Cellular Automata Presets

| Preset | Kernel | Activations | Behavior |
|--------|--------|-------------|----------|
| `conway` | Neighbors weighted as 1, self weighted as 10 | sum == 3 OR sum == 12 OR sum == 13 | **Conway's Game of Life**: Born at exactly 3 neighbors, survives at 2-3 neighbors |
| `rule30` | First 3 neighbors weighted as 1 | sum == 1 OR sum == 2 | Simplified Rule 30 approximation for 2D |
| `majority` | All 8 neighbors weighted as 1 | sum >= 5 | Becomes 1 if majority (5+) of neighbors are 1 |
| `and` | All 8 neighbors weighted as 1 | sum == 8 | Becomes 1 only if ALL 8 neighbors are 1 |
| `or` | All 8 neighbors weighted as 1 | sum >= 1 | Becomes 1 if ANY neighbor is 1 |

**Note**: The `conway` preset uses self-weight=10 as an offset to encode the current cell state in the weighted sum, allowing birth (sum=3) and survival (sum=12 or 13) activations to be distinguished.

## Technical Notes

### ESP-NOW Details
- Uses broadcast MAC address: `FF:FF:FF:FF:FF:FF`
- Frame size: ~148 bytes (well under 250 byte limit)
- No encryption (for maximum compatibility)

### State Calculation
- `sum = kernel[0]×n0.value + kernel[1]×n1.value + ... + kernel[8]×own.value`
- Without activations: `value = (sum > 0) ? 1 : 0` (default threshold at 0)
- With activations: `value = 1` if ANY activation matches (OR logic), else 0
- Input vector: [neighbor0.value, neighbor1.value, ..., neighbor7.value, own.value]
- All values are binary (0 or 1), kernel weights are floats (typically 0-1 for counting, or -1 to 1 for inhibition/excitation)
- Activations: each has operator (0-4: <, <=, ==, >=, >) and threshold value, evaluated against weighted sum
- **Conway preset trick**: self-weight=10 encodes current state in sum, enabling state-dependent transition activation

### TX Power Units
- ESP32 uses units of 0.25 dBm
- Max: 84 units = 21 dBm = 125 mW
- Adjustment: ±4 units = ±1 dBm per step

### Sequence Numbers
- `sequence`: Main message counter
- `kernelSequence`: Increments when kernel is updated via serial
- `activationSequence`: Increments when activations are updated via serial
- `valueSequence`: Increments when value is reset via serial
- Higher sequence numbers propagate automatically through the network

### Memory Usage
- Peer storage: 24 peers × ~100 bytes = ~2.4 KB RAM
- State storage: ~80 bytes per DeviceState
- Total RAM: Well under ESP32-C3's 384 KB

## License

Open source for research and experimentation.