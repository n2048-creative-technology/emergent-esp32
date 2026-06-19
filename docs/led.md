# LED Behavior Documentation

This document describes the expected behavior of the single on-board LED on each device, which visually represents the device's cellular automaton state.

## Overview

Each device has **one white LED** (controlled via FastLED) that reflects the device's **current state value** (`selfState.value`). The LED uses a **smooth transition** effect to gradually change between ON (white) and OFF (black) states, rather than instant switching.

## Hardware Configuration

- **LED Type**: Single addressable RGB LED (WS2812 or similar)
- **Data Pin**: Defined as `DATA_PIN 10` in `main.cpp`
- **Number of LEDs**: `NUM_LEDS 1` (single LED per device)
- **Color Order**: GRB (FastLED default)

## State Mapping

| Device State (`selfState.value`) | LED Color | Brightness |
|----------------------------------|-----------|------------|
| `false` (OFF) | Black | 0 (off) |
| `true` (ON) | White | 255 (full) |

## Transition Behavior

The LED **does not change instantaneously**. Instead, it uses a **linear interpolation (lerp) algorithm** to smoothly transition between states.

### Transition Mechanism

The transition is controlled by the following logic in the main loop:

```cpp
// Determine target brightness from current state
float targetBrightness = selfState.value ? 255.0f : 0.0f;

// Smoothly interpolate current brightness toward target
currentBrightness += (targetBrightness - currentBrightness) * transitionSpeed;

// Clamp and cast to valid LED range
uint8_t smoothedBrightness = constrain(static_cast<uint8_t>(currentBrightness), 0, 255);
leds[0] = CRGB::blend(CRGB::Black, CRGB::White, smoothedBrightness);
FastLED.show();
```

### Transition Characteristics

- **Transition Type**: Exponential decay toward target (first-order low-pass filter)
- **Smoothness**: The LED brightness follows a natural, easing curve
- **Direction Reversal**: If the state changes during a transition, the LED **seamlessly reverses direction** at its current brightness level

### Configuration

The transition speed is controlled by the `transitionSpeed` parameter in `config.h`:

```cpp
constexpr float transitionSpeed = 0.002;  // Default: slow, smooth transitions
```

| `transitionSpeed` Value | Transition Time (approx.) | Perceived Speed |
|-------------------------|----------------------------|------------------|
| `0.001` | ~5 seconds | Very slow, subtle |
| `0.002` | ~2.5 seconds | Slow, smooth |
| `0.01` | ~0.5 seconds | Moderate |
| `0.05` | ~0.1 seconds | Fast |
| `0.10` | ~0.05 seconds | Very fast (almost instant) |

**Note**: The actual transition time depends on the initial brightness difference. A change from 0 to 255 at `transitionSpeed = 0.002` takes approximately **2-3 seconds** to complete.

## Expected Visual Behavior

### Scenario 1: State Changes from OFF to ON
1. Device state is `false` → LED is **black (off)**
2. `selfState.value` changes to `true`
3. LED **begins fading from black to white**
4. After ~2-3 seconds (with default `transitionSpeed`), LED reaches **full white brightness**
5. LED remains white while state is `true`

### Scenario 2: State Changes from ON to OFF
1. Device state is `true` → LED is **white (full brightness)**
2. `selfState.value` changes to `false`
3. LED **begins fading from white to black**
4. After ~2-3 seconds (with default `transitionSpeed`), LED turns **off**
5. LED remains off while state is `false`

### Scenario 3: State Oscillates Rapidly
1. State changes from OFF to ON → LED starts fading to white
2. After 1 second, state changes back to OFF
3. LED **seamlessly reverses direction** from its current brightness (e.g., 50% brightness)
4. LED continues fading toward black from that intermediate point
5. No abrupt jumps or resets occur

### Scenario 4: State Stays Stable
1. If `selfState.value` remains constant (either `true` or `false`), the LED maintains its brightness
2. `currentBrightness` asymptotically approaches the target (255 or 0)
3. The LED appears stable at its final brightness

## LED Update Frequency

The LED is updated in the main loop at every iteration, which runs continuously. The `FastLED.show()` command is called every loop iteration, ensuring the LED reflects the current `smoothedBrightness` value.

Since the main loop runs frequently (typically hundreds of times per second), the LED brightness updates appear **smooth and continuous** to the human eye.

## Debugging and Verification

To verify LED behavior:

1. **Check current state**: The device's state can be read from `selfState.value`
2. **Monitor brightness**: The `currentBrightness` variable (float) shows the current interpolation value
3. **Serial output**: Enable `kEnableSerialDebug` to see state changes and LED updates

### Example Debug Output
With `kEnableSerialDebug` enabled, the full state printout includes:
```
self mac=... value=1 kernel=[...] activations=[...]
```
The `value=1` indicates the state is ON, and the LED should be transitioning to or maintaining white.

## Relationship to Cellular Automaton

The LED serves as a **visual representation** of the device's state in the cellular automaton network:

- **Black (OFF)**: The device's state is `false` according to the cellular automaton rules
- **White (ON)**: The device's state is `true` according to the cellular automaton rules

The smooth transitions allow for:
- Visual feedback of state changes propagating through the network
- Easy observation of oscillation patterns
- Aesthetic, organic visualizations of the automaton's behavior

## Customization Options

### Changing Transition Speed
Modify `transitionSpeed` in `config.h`:
```cpp
constexpr float transitionSpeed = 0.01;  // Faster transitions
```

### Changing LED Color
To use a color other than white when ON:
```cpp
// Replace CRGB::White with your desired color
leds[0] = CRGB::blend(CRGB::Black, CRGB::Red, smoothedBrightness);
```

### Changing Number of LEDs
Modify `NUM_LEDS` and expand the `leds` array if using multiple LEDs.

## Troubleshooting

| Symptom | Likely Cause | Solution |
|---------|--------------|----------|
| LED stays off | `selfState.value` is false, or LED not connected | Check `selfState.value` and wiring |
| LED stays on | `selfState.value` is true | Check cellular automaton rules |
| No smooth transition | `transitionSpeed` too high | Reduce `transitionSpeed` in config.h |
| LED flickers | State oscillating rapidly | Check neighbor states and rules |
| LED wrong color | Color mapping issue | Verify `CRGB::blend` parameters |

## Summary

The LED provides a **smooth, visual indication** of the device's cellular automaton state:
- **OFF (Black)**: Device state is `false`
- **ON (White)**: Device state is `true`
- **Transitions**: Smooth fades between states, controlled by `transitionSpeed`
- **Reversible**: If state changes mid-transition, the LED reverses direction seamlessly

This behavior allows for intuitive observation of the network's dynamic state patterns, making it easy to debug and demonstrate the cellular automaton's operation.
