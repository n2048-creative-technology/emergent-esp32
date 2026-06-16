#ifndef CONFIG_H
#define CONFIG_H

#include <cstddef>
#include <cstdint>

// Protocol
constexpr uint32_t kProtocolMagic = 0x46464C59;  // "FFLY"
constexpr uint8_t kProtocolVersion = 1;

// Radio
constexpr uint8_t kWifiChannel = 1;
constexpr int kInitialTxPowerPercentage = 10;  // Starting TX power % (0-100)

// TX power adjustment
uint32_t lastAdjustMs = 0;
constexpr uint32_t kAdjustIntervalMs = 5000;
constexpr int kTargetMaxPeers = 10; // 10
constexpr int kTargetMinPeers = 2; // 8
constexpr int8_t kPowerStep = 4;

// Temperature reading
constexpr uint32_t kTempReadIntervalMs = 5000;  // Read every 5s

// Timing
constexpr uint32_t kBroadcastIntervalMs = 250;
constexpr uint32_t kPrintIntervalMs = 1000;
constexpr uint32_t kProcessIntervalMs = 250;
constexpr uint32_t kPeerTimeoutMs = 10000;

// Peer management
constexpr size_t kMaxKnownPeers = 24;
constexpr size_t kClosestPeerCount = 8;

#endif
