// Honda K-Line reader v2 for Air Blade 125 (2023) DLC.
//
// Hardware (default): ESP32-C3 SuperMini + L9637D board
//   GPIO4 -> L9637D TX (pin 4), GPIO3 <- L9637D RX (pin 1), L9637D VCC = 3.3V
//   The L9637D is non-inverting: TX low drives K-Line low.
// Old hardware (env wrover_2n2222): 2N2222 stage inverts TX, set K_TX_INVERTED=1.
//
// K-Line is polled in its own task; the phone dashboard is served over a WiFi
// access point (SSID AB125, http://192.168.4.1) and polls /data for JSON.
// Every reading is also broadcast over ESP-NOW (DashPacket) for the CYD dashboard.

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_now.h>
#include "driver/uart.h"
#include "dash_packet.h"
#include "dashboard.h"

#ifndef K_RX_PIN
#define K_RX_PIN 3
#endif
#ifndef K_TX_PIN
#define K_TX_PIN 4
#endif
#ifndef K_TX_INVERTED
#define K_TX_INVERTED 0
#endif

static const char *AP_SSID = "AB125";
static const char *AP_PASS = "ab125kline";  // WPA2 needs at least 8 characters
static const int AP_CHANNEL = 1;             // ESP-NOW uses the same channel; must match the CYD
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static const uart_port_t K_UART = UART_NUM_1;
static const uint32_t K_BAUD = 10400;

HardwareSerial KLine(1);
WebServer server(80);

// Latest decoded values, written by the K-Line task and read by the web server.
struct LiveData {
  bool valid;
  uint16_t rpm;
  uint8_t kmh;
  uint8_t tps;
  int16_t ect;
  int16_t iat;
  float batt;
  uint32_t updatedMs;
  const char *status;
};

LiveData live = {false, 0, 0, 0, 0, 0, 0, 0, "starting"};
portMUX_TYPE liveMux = portMUX_INITIALIZER_UNLOCKED;

void setStatus(const char *status) {
  portENTER_CRITICAL(&liveMux);
  live.status = status;
  if (strcmp(status, "ok") != 0) live.valid = false;
  portEXIT_CRITICAL(&liveMux);
}

void out(const String &s) { Serial.print(s); }

void outln(const String &s = "") { out(s + "\n"); }

String hex(const uint8_t *d, size_t n) {
  String s;
  char b[4];
  for (size_t i = 0; i < n; i++) {
    snprintf(b, sizeof(b), "%02X ", d[i]);
    s += b;
  }
  return s;
}

uint8_t checksum(const uint8_t *d, size_t n) {
  uint8_t s = 0;
  for (size_t i = 0; i < n; i++) s += d[i];
  return (uint8_t)(0x100 - s);
}

// Drive the K-Line level directly through the TX pin (UART detached).
void setKLine(bool high) { digitalWrite(K_TX_PIN, (high != (bool)K_TX_INVERTED) ? HIGH : LOW); }

// K-Line is a single wire, so every byte we send is echoed back on RX.
// Returns false if the echo is missing or differs (wiring problem).
bool sendFrame(const uint8_t *d, size_t n) {
  while (KLine.available()) KLine.read();
  KLine.write(d, n);
  KLine.flush();
  uint8_t echo[64];
  uint32_t t = millis();
  size_t got = 0;
  while (got < n && got < sizeof(echo) && millis() - t < 50) {
    if (KLine.available()) echo[got++] = KLine.read();
    else delay(1);
  }
  if (got == n && memcmp(echo, d, n) == 0) return true;
  outln("Echo mismatch: sent " + hex(d, n) + "| got " + hex(echo, got) +
        "| RX level=" + String(digitalRead(K_RX_PIN)));
  return false;
}

// Response frame: [hdr][len][...][cks], len = total frame length.
// Returns length (>0), 0 = no reply, -1 = bad length, -2 = bad checksum, -3 = incomplete.
int readFrame(uint8_t *buf, size_t maxLen, uint32_t timeout = 150) {
  uint32_t t = millis();
  size_t n = 0;
  while (millis() - t < timeout) {
    if (!KLine.available()) {
      delay(1);
      continue;
    }
    buf[n++] = KLine.read();
    if (n >= 2 && (buf[1] < 3 || buf[1] > maxLen)) return -1;
    if (n >= 2 && n == buf[1]) {
      uint8_t s = 0;
      for (size_t i = 0; i < n; i++) s += buf[i];
      return s == 0 ? (int)n : -2;
    }
    t = millis();
  }
  return n == 0 ? 0 : -3;
}

bool wakeupECU() {
  KLine.end();
  pinMode(K_TX_PIN, OUTPUT);
  setKLine(true);  // idle
  delay(300);
  setKLine(false);  // K-Line low for 70 ms
  delay(70);
  setKLine(true);
  KLine.begin(K_BAUD, SERIAL_8N1, K_RX_PIN, K_TX_PIN);
  if (K_TX_INVERTED) uart_set_line_inverse(K_UART, UART_SIGNAL_TXD_INV);
  delay(130);

  const uint8_t wake[] = {0xFE, 0x04, 0xFF, 0xFF};
  if (!sendFrame(wake, sizeof(wake))) {
    outln("Bad echo on RX: check K-Line wiring / 12V supply / 3.3V to L9637D");
    setStatus("no-echo");
    return false;
  }
  delay(200);

  const uint8_t init[] = {0x72, 0x05, 0x00, 0xF0, 0x99};
  sendFrame(init, sizeof(init));
  uint8_t buf[64];
  int n = readFrame(buf, sizeof(buf));
  if (n > 0) {
    outln("INIT OK: " + hex(buf, n));
    return true;
  }
  outln("INIT FAIL, code=" + String(n));
  setStatus("init-fail");
  return false;
}

int readTable(uint8_t table, uint8_t *buf, size_t maxLen) {
  uint8_t req[5] = {0x72, 0x05, 0x71, table, 0};
  req[4] = checksum(req, 4);
  sendFrame(req, sizeof(req));
  return readFrame(buf, maxLen);
}

void scanTables() {
  const uint8_t tables[] = {0x10, 0x11, 0x12, 0x13, 0x17, 0x20, 0x21, 0xD0, 0xD1};
  uint8_t buf[64];
  for (uint8_t t : tables) {
    int n = readTable(t, buf, sizeof(buf));
    outln("Table " + hex(&t, 1) + "-> " + (n > 0 ? hex(buf, n) : "none (" + String(n) + ")"));
    delay(50);
  }
}

// Air Blade 2023 ECU answers live data in table 0x17 (0x10-0x13 are empty).
// Layout decoded from engine-off / idle / rev / rear-wheel-spin logs:
//   d[0..1] RPM (big endian)        d[2]  TPS sensor (x 5/256 V)
//   d[3]    TPS opening             d[6]/d[7] IAT sensor / IAT (C + 40)
//   d[8]/d[9] ECT sensor / ECT (C + 40)
//   d[10]   battery (V x 10)        d[15] speed (km/h)
//   d[11..14] not identified yet (d[11..12] tracks fueling, d[14] drops when throttle opens)
void handleLiveData(const uint8_t *buf, int n) {
  const uint8_t *d = buf + 4;
  int len = n - 5;
  if (len < 16) {
    outln("Short frame: " + hex(buf, n));
    return;
  }
  portENTER_CRITICAL(&liveMux);
  live.valid = true;
  live.rpm = (d[0] << 8) | d[1];
  live.kmh = d[15];
  live.tps = d[3];
  live.ect = (int)d[9] - 40;
  live.iat = (int)d[7] - 40;
  live.batt = d[10] / 10.0f;
  live.updatedMs = millis();
  live.status = "ok";
  portEXIT_CRITICAL(&liveMux);

  outln("rpm=" + String((d[0] << 8) | d[1]) +
        " km/h=" + String(d[15]) +
        " tps=" + String(d[3]) +
        " ect=" + String((int)d[9] - 40) + "C" +
        " iat=" + String((int)d[7] - 40) + "C" +
        " batt=" + String(d[10] / 10.0, 1) + "V" +
        " | raw " + hex(d, len));
}

void klineTask(void *) {
  bool connected = false;
  for (;;) {
    if (!connected) {
      // Keep showing the last failure reason while retrying.
      if (strcmp(live.status, "no-echo") != 0 && strcmp(live.status, "init-fail") != 0) setStatus("connecting");
      connected = wakeupECU();
      if (!connected) {
        delay(2000);
        continue;
      }
      scanTables();
    }

    uint8_t buf[64];
    int n = readTable(0x17, buf, sizeof(buf));
    if (n <= 0) {
      outln("Lost connection (code=" + String(n) + "), retrying...");
      setStatus("lost");
      connected = false;
      continue;
    }
    handleLiveData(buf, n);
    delay(100);
  }
}

void handleData() {
  portENTER_CRITICAL(&liveMux);
  LiveData d = live;
  portEXIT_CRITICAL(&liveMux);

  char json[192];
  snprintf(json, sizeof(json),
           "{\"valid\":%s,\"status\":\"%s\",\"age\":%lu,\"rpm\":%u,\"kmh\":%u,"
           "\"tps\":%u,\"ect\":%d,\"iat\":%d,\"batt\":%.1f}",
           d.valid ? "true" : "false", d.status,
           (unsigned long)(d.valid ? millis() - d.updatedMs : 0),
           d.rpm, d.kmh, d.tps, d.ect, d.iat, d.batt);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

static uint8_t statusCode(const char *status) {
  if (strcmp(status, "ok") == 0) return DASH_OK;
  if (strcmp(status, "no-echo") == 0) return DASH_NO_ECHO;
  if (strcmp(status, "init-fail") == 0) return DASH_INIT_FAIL;
  if (strcmp(status, "lost") == 0) return DASH_LOST;
  return DASH_CONNECTING;
}

void initEspNow() {
  if (esp_now_init() != ESP_OK) {
    outln("ESP-NOW init failed");
    return;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST_MAC, 6);
  peer.channel = AP_CHANNEL;
  peer.ifidx = WIFI_IF_AP;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) outln("ESP-NOW add peer failed");
  else outln("ESP-NOW broadcasting on channel " + String(AP_CHANNEL));
}

// Send on every new reading, and at least every 250 ms so status changes get through.
void broadcastLive() {
  static uint32_t lastUpdate = 0, lastSend = 0, seq = 0;
  portENTER_CRITICAL(&liveMux);
  LiveData d = live;
  portEXIT_CRITICAL(&liveMux);

  uint32_t now = millis();
  if (d.updatedMs == lastUpdate && now - lastSend < 250) return;
  lastUpdate = d.updatedMs;
  lastSend = now;

  DashPacket p = {};
  p.magic = DASH_MAGIC;
  p.version = DASH_VERSION;
  p.status = d.valid ? DASH_OK : statusCode(d.status);
  p.seq = seq++;
  p.rpm = d.rpm;
  p.kmh = d.kmh;
  p.tps = d.tps;
  p.ect = d.ect;
  p.iat = d.iat;
  p.battMv = (uint16_t)(d.batt * 1000);
  esp_now_send(BROADCAST_MAC, (const uint8_t *)&p, sizeof(p));
}

// Status LED: solid = live data, fast blink = connecting/reconnecting,
// slow blink = ECU not answering, short flash every 2 s = no K-Line signal.
void updateLed() {
#ifdef STATUS_LED_PIN
  portENTER_CRITICAL(&liveMux);
  bool valid = live.valid;
  const char *status = live.status;
  portEXIT_CRITICAL(&liveMux);

  uint32_t t = millis();
  bool on;
  if (valid) on = true;
  else if (strcmp(status, "no-echo") == 0) on = t % 2000 < 80;
  else if (strcmp(status, "init-fail") == 0) on = t % 1000 < 500;
  else on = t % 200 < 100;
  digitalWrite(STATUS_LED_PIN, on != (bool)STATUS_LED_ACTIVE_LOW ? HIGH : LOW);
#endif
}

void setup() {
  Serial.begin(115200);
#ifdef STATUS_LED_PIN
  pinMode(STATUS_LED_PIN, OUTPUT);
#endif
  delay(500);
  outln("=== Honda K-Line reader v2 ===");
  outln("RX=GPIO" + String(K_RX_PIN) + " TX=GPIO" + String(K_TX_PIN) +
        (K_TX_INVERTED ? " (TX inverted, 2N2222)" : " (L9637D)"));

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL);
#ifdef WIFI_TX_POWER
  WiFi.setTxPower(WIFI_TX_POWER);
#endif
  WiFi.setSleep(false);
  outln("WiFi AP '" + String(AP_SSID) + "' -> http://" + WiFi.softAPIP().toString());

  server.on("/", []() { server.send_P(200, "text/html", DASHBOARD_HTML); });
  server.on("/data", handleData);
  server.begin();
  initEspNow();

  xTaskCreate(klineTask, "kline", 8192, nullptr, 1, nullptr);
}

void loop() {
  server.handleClient();
  broadcastLive();
  updateLed();
  delay(2);
}
