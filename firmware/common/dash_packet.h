#pragma once
#include <stdint.h>

// ESP-NOW packet broadcast by the K-Line reader to the CYD dashboard.
// Shared by both firmwares (included via -I ../common); bump DASH_VERSION when changing it.

#define DASH_MAGIC 0xAB12
#define DASH_VERSION 1

enum DashStatus : uint8_t {
  DASH_OK = 0,          // fresh ECU data
  DASH_CONNECTING = 1,  // waking the ECU
  DASH_NO_ECHO = 2,     // no K-Line signal (key off / wiring)
  DASH_INIT_FAIL = 3,   // ECU did not answer
  DASH_LOST = 4,        // connection dropped, retrying
};

struct __attribute__((packed)) DashPacket {
  uint16_t magic;    // DASH_MAGIC
  uint8_t version;   // DASH_VERSION
  uint8_t status;    // DashStatus
  uint32_t seq;      // increments every packet
  uint16_t rpm;
  uint8_t kmh;
  uint8_t tps;
  int16_t ect;       // engine coolant, C
  int16_t iat;       // intake air, C
  uint16_t battMv;   // battery, millivolts
};
