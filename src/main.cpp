#include <Arduino.h>
#include <WiFi.h>

#include <ESPCPUTemp.h>
#include <esp_err.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <cmath>
#include <cstring>

#include "config.h"
#include "state_processing.h"


#include <FastLED.h>

// How many leds in your strip?
#define NUM_LEDS 1

// For led chips like WS2812, which have a data line, ground, and power, you just
// need to define DATA_PIN.  For led chipsets that are SPI based (four wires - data, clock,
// ground, and power), like the LPD8806 define both DATA_PIN and CLOCK_PIN
// Clock pin only needed for SPI based chipsets when not using hardware SPI
#define DATA_PIN 3

CRGB leds[NUM_LEDS];

float currentBrightness = 0;  // Tracks actual LED brightness (0.0–255.0)


// Serial command buffer
constexpr size_t kSerialBufferSize = 128;
char serialBuffer[kSerialBufferSize] = {};
size_t serialBufferPos = 0;

namespace {

const uint8_t kBroadcastMac[ESP_NOW_ETH_ALEN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct KnownPeer {
  bool valid;
  uint8_t mac[ESP_NOW_ETH_ALEN];
  int8_t rssi;
  uint32_t lastSeenMs;
  DeviceState advertisedState;
};

static_assert(sizeof(DeviceState) <= ESP_NOW_MAX_DATA_LEN,
              "DeviceState must fit inside a single ESP-NOW frame");

uint8_t selfMac[ESP_NOW_ETH_ALEN] = {};
DeviceState selfState = {};
ClosestDeviceState closestDevices[kClosestPeerCount] = {};
KnownPeer peers[kMaxKnownPeers] = {};
uint32_t sequenceNumber = 0;
uint32_t nextBroadcastMs = 0;
uint32_t nextPrintMs = 0;
uint32_t nextProcessMs = 0;
portMUX_TYPE peerMux = portMUX_INITIALIZER_UNLOCKED;
int8_t currentTxPower = 0;
uint32_t lastTempReadMs = 0;
ESPCPUTemp tempSensor;

bool macEquals(const uint8_t *a, const uint8_t *b) {
  return memcmp(a, b, ESP_NOW_ETH_ALEN) == 0;
}

void copyMac(uint8_t *dst, const uint8_t *src) {
  memcpy(dst, src, ESP_NOW_ETH_ALEN);
}

String macToString(const uint8_t *mac) {
  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buffer);
}

void clearDeviceState(DeviceState &state) {
  memset(&state, 0, sizeof(state));
  state.magic = kProtocolMagic;
  state.version = kProtocolVersion;
  state.txPower = 0;
  state.temperature = 0.0f;
  state.value = false;
  state.kernelSequence = 0;
  state.valueSequence = 0;
  state.activationSequence = 0;
  state.activationCount = 0;
}

void clearClosestDevice(ClosestDeviceState &device) {
  device.valid = false;
  device.rssi = -127;
  device.ageMs = UINT32_MAX;
  clearDeviceState(device.state);
}

bool peerIsFresh(const KnownPeer &peer, uint32_t nowMs) {
  return peer.valid && (nowMs - peer.lastSeenMs) <= kPeerTimeoutMs;
}

int findPeerIndex(const uint8_t *mac) {
  for (size_t i = 0; i < kMaxKnownPeers; ++i) {
    if (peers[i].valid && macEquals(peers[i].mac, mac)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int findReusablePeerIndex(uint32_t nowMs) {
  for (size_t i = 0; i < kMaxKnownPeers; ++i) {
    if (!peers[i].valid || !peerIsFresh(peers[i], nowMs)) {
      return static_cast<int>(i);
    }
  }

  size_t weakestIndex = 0;
  for (size_t i = 1; i < kMaxKnownPeers; ++i) {
    if (peers[i].rssi < peers[weakestIndex].rssi) {
      weakestIndex = i;
    }
  }
  return static_cast<int>(weakestIndex);
}

void storePeerObservation(const uint8_t *mac, int8_t rssi,
                          const DeviceState &state) {
  const uint32_t nowMs = millis();

  portENTER_CRITICAL(&peerMux);
  int index = findPeerIndex(mac);
  if (index < 0) {
    index = findReusablePeerIndex(nowMs);
    peers[index].valid = true;
    copyMac(peers[index].mac, mac);
  }

  peers[index].rssi = rssi;
  peers[index].lastSeenMs = nowMs;
  peers[index].advertisedState = state;
  portEXIT_CRITICAL(&peerMux);
}

void adjustTxPower(uint32_t nowMs) {

  if (nowMs - lastAdjustMs < kAdjustIntervalMs) return;
  lastAdjustMs = nowMs;

  int peerCount = 0;
  portENTER_CRITICAL(&peerMux);
  for (const KnownPeer& peer : peers) {
    if (peerIsFresh(peer, nowMs)) peerCount++;
  }
  portEXIT_CRITICAL(&peerMux);

  if (peerCount > kTargetMaxPeers) {
    currentTxPower = max<int8_t>(0, currentTxPower - kPowerStep);
    esp_wifi_set_max_tx_power(currentTxPower);
  } else if (peerCount < kTargetMinPeers) {
    currentTxPower = min<int8_t>(84, currentTxPower + kPowerStep);
    esp_wifi_set_max_tx_power(currentTxPower);
  }
}

void getClosestDevices(ClosestDeviceState closest[], size_t closestCount,
                       uint32_t nowMs) {
  int bestIndexes[kClosestPeerCount];
  for (size_t i = 0; i < kClosestPeerCount; ++i) {
    bestIndexes[i] = -1;
  }

  for (size_t i = 0; i < closestCount; ++i) {
    clearClosestDevice(closest[i]);
  }

  portENTER_CRITICAL(&peerMux);
  for (size_t i = 0; i < kMaxKnownPeers; ++i) {
    if (!peerIsFresh(peers[i], nowMs)) {
      continue;
    }

    for (size_t slot = 0; slot < closestCount; ++slot) {
      if (bestIndexes[slot] < 0 ||
          peers[i].rssi > peers[bestIndexes[slot]].rssi) {
        for (size_t move = closestCount - 1; move > slot; --move) {
          bestIndexes[move] = bestIndexes[move - 1];
        }
        bestIndexes[slot] = static_cast<int>(i);
        break;
      }
    }
  }

  for (size_t slot = 0; slot < closestCount; ++slot) {
    const int index = bestIndexes[slot];
    if (index < 0) {
      continue;
    }

    closest[slot].valid = true;
    closest[slot].rssi = peers[index].rssi;
    closest[slot].ageMs = nowMs - peers[index].lastSeenMs;
    closest[slot].state = peers[index].advertisedState;
  }
  portEXIT_CRITICAL(&peerMux);
}

void updateSelfStateMetadata(uint32_t nowMs, uint32_t stateSequence) {
  selfState.magic = kProtocolMagic;
  selfState.version = kProtocolVersion;
  copyMac(selfState.mac, selfMac);
  selfState.sequence = stateSequence;
  selfState.uptimeMs = nowMs;
  selfState.txPower = currentTxPower;

  if (nowMs - lastTempReadMs >= kTempReadIntervalMs) {
    lastTempReadMs = nowMs;
    if (tempSensor.tempAvailable()) {
      float temp = tempSensor.getTemp();
      if (!std::isnan(temp)) {
        selfState.temperature = temp;
      }
    }
  }
}

void expireOldPeers(uint32_t nowMs) {
  portENTER_CRITICAL(&peerMux);
  for (KnownPeer &peer : peers) {
    if (peer.valid && !peerIsFresh(peer, nowMs)) {
      peer.valid = false;
      clearDeviceState(peer.advertisedState);
    }
  }
  portEXIT_CRITICAL(&peerMux);
}

void onEspNowReceive(const esp_now_recv_info_t *info, const uint8_t *data,
                     int dataLen) {
  if (info == nullptr || info->src_addr == nullptr || data == nullptr ||
      dataLen != static_cast<int>(sizeof(DeviceState))) {
    return;
  }

  DeviceState receivedState;
  memcpy(&receivedState, data, sizeof(receivedState));

  if (receivedState.magic != kProtocolMagic ||
      receivedState.version != kProtocolVersion ||
      macEquals(info->src_addr, selfMac)) {
    return;
  }

  int8_t rssi = -127;
  if (info->rx_ctrl != nullptr) {
    rssi = static_cast<int8_t>(info->rx_ctrl->rssi);
  }

  storePeerObservation(info->src_addr, rssi, receivedState);
}

void addBroadcastPeer() {
  if (esp_now_is_peer_exist(kBroadcastMac)) {
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  copyMac(peerInfo.peer_addr, kBroadcastMac);
  peerInfo.channel = kWifiChannel;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;

  const esp_err_t result = esp_now_add_peer(&peerInfo);
  if (result != ESP_OK) {
    Serial.printf("Failed to add broadcast ESP-NOW peer: %s\n",
                  esp_err_to_name(result));
  }
}

void broadcastState(uint32_t nowMs) {
  updateSelfStateMetadata(nowMs, ++sequenceNumber);

  const esp_err_t result =
      esp_now_send(kBroadcastMac, reinterpret_cast<uint8_t *>(&selfState),
                   sizeof(selfState));
  if (result != ESP_OK) {
    Serial.printf("ESP-NOW broadcast failed: %s\n", esp_err_to_name(result));
  }
}

void printDeviceState(const char *label, const DeviceState &state) {
#if kEnableSerialDebug
  Serial.printf("%s mac=%s seq=%lu uptime=%lu tx=%d temp=%.1fC value=%d kseq=%lu vseq=%lu aseq=%lu kernel=[", label,
                macToString(state.mac).c_str(),
                static_cast<unsigned long>(state.sequence),
                static_cast<unsigned long>(state.uptimeMs),
                state.txPower, state.temperature, state.value ? 1 : 0,
                static_cast<unsigned long>(state.kernelSequence),
                static_cast<unsigned long>(state.valueSequence),
                static_cast<unsigned long>(state.activationSequence));
  for (size_t i = 0; i < kKernelSize; ++i) {
    if (i > 0) {
      Serial.print(", ");
    }
    Serial.print(state.kernel[i], 4);
  }
  Serial.print("] activations=[");
  for (uint8_t i = 0; i < state.activationCount; ++i) {
    if (i > 0) {
      Serial.print(", ");
    }
    const char* opStr = "";
    switch(state.activations[i].op) {
      case 0: opStr = "<"; break;
      case 1: opStr = "<="; break;
      case 2: opStr = "=="; break;
      case 3: opStr = ">="; break;
      case 4: opStr = ">"; break;
    }
    Serial.printf("%s%.2f", opStr, state.activations[i].value);
  }
  Serial.println("]");
#endif
}

void printFullState(uint32_t nowMs) {
  ClosestDeviceState closest[kClosestPeerCount];
  getClosestDevices(closest, kClosestPeerCount, nowMs);

  Serial.println("---- full state ----");
  printDeviceState("self", selfState);

  for (size_t i = 0; i < kClosestPeerCount; ++i) {
    Serial.printf("neighbor[%u] ", static_cast<unsigned>(i));
    if (!closest[i].valid) {
      Serial.println("empty");
      continue;
    }

    Serial.printf("rssi=%d age=%lu ", closest[i].rssi,
                  static_cast<unsigned long>(closest[i].ageMs));
    printDeviceState("state", closest[i].state);
  }
  Serial.println("--------------------");
}

void setupWifiAndEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  delay(100);

//  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM); // save power but still receive beacons regularly to maintain connection and get RSSI updates
  esp_wifi_set_channel(kWifiChannel, WIFI_SECOND_CHAN_NONE);

  // Set initial TX power (ESP32 uses units of 0.25 dBm, max is 84 = 21 dBm)
  currentTxPower = static_cast<int8_t>(84 * kInitialTxPowerPercentage / 100);
  if (esp_wifi_set_max_tx_power(currentTxPower) != ESP_OK) {
    Serial.printf("Failed to set initial TX power to %d%% (%d units)\n",
                  kInitialTxPowerPercentage, currentTxPower);
  }

  if (esp_wifi_get_mac(WIFI_IF_STA, selfMac) != ESP_OK) {
    Serial.println("Failed to read STA MAC address");
  }

  const esp_err_t initResult = esp_now_init();
  if (initResult != ESP_OK) {
    Serial.printf("ESP-NOW init failed: %s\n", esp_err_to_name(initResult));
    return;
  }

  addBroadcastPeer();
  const esp_err_t callbackResult = esp_now_register_recv_cb(onEspNowReceive);
  if (callbackResult != ESP_OK) {
    Serial.printf("ESP-NOW receive callback failed: %s\n",
                  esp_err_to_name(callbackResult));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);


  // Lower CPU frequency for power savings
  setCpuFrequencyMhz(80);  // Down from 160MHz

  
  for (KnownPeer &peer : peers) {
    clearDeviceState(peer.advertisedState);
  }
  for (ClosestDeviceState &closest : closestDevices) {
    clearClosestDevice(closest);
  }
  clearDeviceState(selfState);
  initState(selfState);

  // Initialize internal temperature sensor
  if (!tempSensor.begin()) {
    Serial.println("Warning: Failed to initialize temperature sensor");
  }

  setupWifiAndEspNow();
  updateSelfStateMetadata(millis(), sequenceNumber);
  Serial.printf("ESP32 proximity node started on WiFi channel %u, MAC %s\n",
                kWifiChannel, macToString(selfMac).c_str());


  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);  // GRB ordering is assumed

}

bool parseMacAddress(const char* str, uint8_t mac[ESP_NOW_ETH_ALEN]) {
  int values[6];
  if (sscanf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
             &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; ++i) {
    mac[i] = static_cast<uint8_t>(values[i]);
  }
  return true;
}

bool macMatchesConst(const uint8_t a[ESP_NOW_ETH_ALEN], const uint8_t b[ESP_NOW_ETH_ALEN]) {
  return memcmp(a, b, ESP_NOW_ETH_ALEN) == 0;
}

void handleSerialInput() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBufferPos > 0) {
        serialBuffer[serialBufferPos] = '\0';
        
        // Parse command: "kernel k0,k1,...,k8"
        if (strncmp(serialBuffer, "kernel ", 7) == 0) {
          char* token = strtok(serialBuffer + 7, ",");
          int index = 0;
          while (token != nullptr && index < kKernelSize) {
            selfState.kernel[index++] = atof(token);
            token = strtok(nullptr, ",");
          }
          if (index == kKernelSize) {
            selfState.kernelSequence++;
#if kEnableSerialEssential
            Serial.println("Kernel updated!");
#endif
          } else {
#if kEnableSerialEssential
            Serial.println("Error: Need exactly 9 kernel values");
#endif
          }
        } else if (strncmp(serialBuffer, "preset ", 7) == 0) {
          // Load preset with kernel AND activations: "preset conway", "preset rule30", etc.
          char* presetName = serialBuffer + 7;
          if (loadPreset(selfState, presetName)) {
            selfState.kernelSequence++;
            selfState.activationSequence++;
#if kEnableSerialEssential
            Serial.print("Preset loaded: ");
            Serial.println(presetName);
#endif
          } else {
#if kEnableSerialEssential
            Serial.println("Error: Unknown preset. Use: conway, rule30, majority, and, or");
#endif
          }
        } else if (strcmp(serialBuffer, "reset") == 0) {
          resetStateValue(selfState);
#if kEnableSerialEssential
          Serial.println("Value reset!");
#endif
        } else if (strncmp(serialBuffer, "activations ", 12) == 0) {
          // Format: "activations op1,val1,op2,val2,..."
          // op: 0="<", 1="<=", 2="==", 3=">=", 4=">"
          char* token = strtok(serialBuffer + 12, ",");
          uint8_t count = 0;
          while (token != nullptr && count < kMaxActivations) {
            int op = atoi(token);
            token = strtok(nullptr, ",");
            if (token == nullptr) break;
            float val = atof(token);
            token = strtok(nullptr, ",");
            if (op >= 0 && op <= 4) {
              selfState.activations[count].op = static_cast<uint8_t>(op);
              selfState.activations[count].value = val;
              count++;
            }
          }
          if (count > 0) {
            selfState.activationCount = count;
            selfState.activationSequence++;
#if kEnableSerialEssential
            Serial.print("Activations set (");
            Serial.print(count);
            Serial.println(" activations)");
#endif
          } else {
#if kEnableSerialEssential
            Serial.println("Error: No valid conditions");
#endif
          }
        } else if (strncmp(serialBuffer, "set ", 4) == 0) {
          // Format: "set AA:BB:CC:DD:EE:FF 0" or "set AA:BB:CC:DD:EE:FF 1"
          char* macStr = serialBuffer + 4;
          char* spacePos = strchr(macStr, ' ');
          if (spacePos) {
            *spacePos = '\0';
            uint8_t targetMac[ESP_NOW_ETH_ALEN];
            if (parseMacAddress(macStr, targetMac) &&
                macMatchesConst(targetMac, selfMac)) {
              int newValue = atoi(spacePos + 1);
              if (newValue == 0 || newValue == 1) {
                selfState.value = static_cast<bool>(newValue);
                selfState.valueSequence++;
#if kEnableSerialEssential
                Serial.print("State set to ");
                Serial.println(newValue);
#endif
              } else {
#if kEnableSerialEssential
                Serial.println("Error: Value must be 0 or 1");
#endif
              }
            } else {
#if kEnableSerialEssential
              Serial.println("Error: MAC address mismatch or invalid format");
#endif
            }
          } else {
#if kEnableSerialEssential
            Serial.println("Error: Usage: set AA:BB:CC:DD:EE:FF 0\1");
#endif
          }
        }
        serialBufferPos = 0;
      }
    } else if (serialBufferPos < kSerialBufferSize - 1) {
      serialBuffer[serialBufferPos++] = c;
    }
  }
}

void loop() {
  const uint32_t nowMs = millis();
  expireOldPeers(nowMs);
  adjustTxPower(nowMs);
  handleSerialInput();

  if (static_cast<int32_t>(nowMs - nextProcessMs) >= 0) {
    nextProcessMs = nowMs + kProcessIntervalMs;
    getClosestDevices(closestDevices, kClosestPeerCount, nowMs);
    updateSelfStateMetadata(nowMs, sequenceNumber);
    processState(selfState, closestDevices, kClosestPeerCount);
  }

  if (static_cast<int32_t>(nowMs - nextBroadcastMs) >= 0) {
    nextBroadcastMs = nowMs + kBroadcastIntervalMs;
    broadcastState(nowMs);
  }

  if (static_cast<int32_t>(nowMs - nextPrintMs) >= 0) {
    nextPrintMs = nowMs + kPrintIntervalMs;
    printFullState(nowMs);
  }

  
  // LED adjustment with simple transition effect
  // Determine target brightness from state
  float targetBrightness = selfState.value ? 255.0f : 0.0f;

  // Smoothly interpolate current brightness toward target
  currentBrightness += (targetBrightness - currentBrightness) * transitionSpeed;

  // Clamp to valid range (0–255) and cast to uint8_t for LED
  uint8_t smoothedBrightness = constrain(static_cast<uint8_t>(currentBrightness), 0, 255);
  leds[0] = CRGB::blend(CRGB::Black, CRGB::White, smoothedBrightness);
  FastLED.show();


}
