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
// Allow clearing fault codes from the web page (build with -DENABLE_DTC_CLEAR=0 for a read-only firmware).
#ifndef ENABLE_DTC_CLEAR
#define ENABLE_DTC_CLEAR 1
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

// Read request "72 05 <a> <b> <cks>": tables use a = 0x71, b = table;
// fault codes use a = 0x74 (current) / 0x73 (stored), b = page.
int readRequest(uint8_t a, uint8_t b, uint8_t *buf, size_t maxLen) {
  uint8_t req[5] = {0x72, 0x05, a, b, 0};
  req[4] = checksum(req, 4);
  sendFrame(req, sizeof(req));
  return readFrame(buf, maxLen);
}

int readTable(uint8_t table, uint8_t *buf, size_t maxLen) { return readRequest(0x71, table, buf, maxLen); }

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

// ---------------- Fault codes (read only, never cleared) ----------------
// Layout from the HondaECU project: each page answers "74|73 <page> ?? m1 s1 m2 s2 m3 s3",
// i.e. up to 3 codes per page as (main, sub) bytes, 00 = empty slot. Pages 1..11.
// Not yet verified with a real fault on this bike: the raw pages are kept for checking.

static const int DTC_PAGES = 11;
static const int DTC_MAX = 16;
static const uint32_t DTC_INTERVAL_MS = 30000;

struct DtcList {
  uint8_t count;
  uint8_t codes[DTC_MAX][2];
};

struct DtcState {
  bool valid;          // at least one complete sweep done
  uint32_t updatedMs;  // when the last sweep finished
  char error[32];      // why the last sweep stopped early, if it did
  DtcList current;     // table 0x74
  DtcList stored;      // table 0x73
  uint8_t rawCount;
  char raw[2 * DTC_PAGES][48];
};

DtcState dtc = {};
portMUX_TYPE dtcMux = portMUX_INITIALIZER_UNLOCKED;

// Clearing fault codes: requested by POST /dtc/clear, carried out by the K-Line task.
// Command from the HondaECU project: "72 05 60 03 <cks>". The Air Blade 2023 ECU answers
// "02 05 60 01 98" and does clear the codes (HondaECU only expects 00), so the reply just
// means "accepted"; success is decided by re-reading the fault tables afterwards.
enum ClearState : uint8_t { CLEAR_IDLE, CLEAR_PENDING, CLEAR_SENT, CLEAR_OK, CLEAR_FAILED };
struct ClearStatus {
  ClearState state;
  uint32_t atMs;  // when the state last changed
  char msg[64];
};
ClearStatus clearStatus = {CLEAR_IDLE, 0, ""};

static void setClear(ClearState state, const char *msg) {
  portENTER_CRITICAL(&dtcMux);
  clearStatus.state = state;
  clearStatus.atMs = millis();
  strlcpy(clearStatus.msg, msg, sizeof(clearStatus.msg));
  portEXIT_CRITICAL(&dtcMux);
}

static void addCode(DtcList &list, uint8_t main, uint8_t sub) {
  for (int i = 0; i < list.count; i++)
    if (list.codes[i][0] == main && list.codes[i][1] == sub) return;
  if (list.count < DTC_MAX) {
    list.codes[list.count][0] = main;
    list.codes[list.count][1] = sub;
    list.count++;
  }
}

static String codeList(const DtcList &list) {
  if (list.count == 0) return "none";
  String s;
  char b[8];
  for (int i = 0; i < list.count; i++) {
    snprintf(b, sizeof(b), "%02d-%02d", list.codes[i][0], list.codes[i][1]);
    s += (i ? " " : "") + String(b);
  }
  return s;
}

// The sweep reads one page per polling cycle so live data keeps flowing.
struct DtcSweep {
  int step = -1;  // -1 idle, 0..21 = page index (0x74 pages first, then 0x73)
  bool verbose = false;
  uint32_t lastEnd = 0;
  DtcState work = {};
};

static void startDtcSweep(DtcSweep &s, bool verbose) {
  s.step = 0;
  s.verbose = verbose;
  s.work = {};
}

static void finishDtcSweep(DtcSweep &s, const char *error) {
  s.step = -1;
  s.lastEnd = millis();
  s.work.valid = error == nullptr;
  s.work.updatedMs = millis();
  if (error) strlcpy(s.work.error, error, sizeof(s.work.error));
  outln("DTC current: " + codeList(s.work.current) + " | stored: " + codeList(s.work.stored) +
        (error ? String(" (stopped: ") + error + ")" : ""));
  portENTER_CRITICAL(&dtcMux);
  dtc = s.work;
  ClearStatus c = clearStatus;
  portEXIT_CRITICAL(&dtcMux);

  // First sweep after a clear command decides whether it worked.
  if (c.state == CLEAR_SENT) {
    if (error) setClear(CLEAR_FAILED, "verify-failed");
    else if (s.work.current.count == 0 && s.work.stored.count == 0) setClear(CLEAR_OK, c.msg);
    else setClear(CLEAR_FAILED, "still-present");
    outln(String("DTC clear result: ") + (clearStatus.state == CLEAR_OK ? "cleared" : clearStatus.msg));
  }
}

// Returns false if the ECU stopped answering.
static bool stepDtcSweep(DtcSweep &s) {
  uint8_t table = s.step < DTC_PAGES ? 0x74 : 0x73;
  uint8_t page = s.step % DTC_PAGES + 1;
  uint8_t buf[64];
  int n = readRequest(table, page, buf, sizeof(buf));
  if (n <= 0) {
    outln("DTC " + hex(&table, 1) + "page " + String(page) + ": no reply (code=" + String(n) + ")");
    finishDtcSweep(s, "no reply");
    return false;
  }

  String raw = hex(buf, n);
  if (s.work.rawCount < 2 * DTC_PAGES) strlcpy(s.work.raw[s.work.rawCount++], raw.c_str(), 48);

  // Payload after header + length, without checksum: d[0] = table, d[1] = page.
  const uint8_t *d = buf + 2;
  int len = n - 3;
  bool any = false;
  if (len >= 3 && d[0] == table && d[1] == page) {
    for (int j = 3; j + 1 < len && j <= 7; j += 2) {
      if (d[j] == 0) continue;
      addCode(table == 0x74 ? s.work.current : s.work.stored, d[j], d[j + 1]);
      any = true;
    }
  } else {
    outln("DTC " + hex(&table, 1) + "page " + String(page) + ": unexpected reply " + raw);
  }
  if (s.verbose || any) outln("DTC " + hex(&table, 1) + "p" + String(page) + ": " + raw);

  if (++s.step >= 2 * DTC_PAGES) finishDtcSweep(s, nullptr);
  return true;
}

// Only "60 03" is sent: other sub-commands of 0x60 are unknown and never used.
static void clearDtc() {
  portENTER_CRITICAL(&liveMux);
  uint16_t rpm = live.rpm;
  bool valid = live.valid;
  portEXIT_CRITICAL(&liveMux);
  if (!valid || rpm != 0) {
    outln("DTC clear refused: engine running or no ECU data");
    setClear(CLEAR_FAILED, "engine-running");
    return;
  }

  // Resend only when the ECU did not answer at all; any answer is final.
  for (int attempt = 1; attempt <= 3; attempt++) {
    uint8_t buf[64];
    int n = readRequest(0x60, 0x03, buf, sizeof(buf));
    String raw = n > 0 ? hex(buf, n) : "no reply (code=" + String(n) + ")";
    outln("DTC clear attempt " + String(attempt) + ": " + raw);
    if (n <= 0) {
      setClear(CLEAR_FAILED, "no-reply");
      delay(200);
      continue;
    }
    const uint8_t *d = buf + 2;  // payload: d[0] = 0x60, d[1] = result (00 or 01 seen so far)
    int len = n - 3;
    if (len >= 2 && d[0] == 0x60 && d[1] <= 0x01) setClear(CLEAR_SENT, raw.c_str());
    else setClear(CLEAR_FAILED, ("rejected: " + raw).c_str());
    return;
  }
}

void klineTask(void *) {
  bool connected = false;
  DtcSweep sweep;
  for (;;) {
    if (!connected) {
      // Keep showing the last failure reason while retrying.
      if (strcmp(live.status, "no-echo") != 0 && strcmp(live.status, "init-fail") != 0) setStatus("connecting");
      sweep.step = -1;
      connected = wakeupECU();
      if (!connected) {
        delay(2000);
        continue;
      }
      scanTables();
      startDtcSweep(sweep, true);  // log every raw page once per connection
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

    if (clearStatus.state == CLEAR_PENDING) {
      if (millis() - clearStatus.atMs > 5000) {
        // Requested while the ECU was not answering: never act on a stale request.
        setClear(CLEAR_FAILED, "timeout");
      } else {
        delay(20);
        clearDtc();
        startDtcSweep(sweep, true);  // re-read right away so the page shows the result
      }
    }

    if (sweep.step < 0 && millis() - sweep.lastEnd >= DTC_INTERVAL_MS) startDtcSweep(sweep, false);
    if (sweep.step >= 0) {
      delay(20);
      stepDtcSweep(sweep);
    }
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

// {"valid":true,"age":1234,"error":"","current":["07-01"],"stored":[],"raw":["02 0C 74 01 ..."]}
void handleDtc() {
  static DtcState d;  // static: too big for the web server's stack
  portENTER_CRITICAL(&dtcMux);
  d = dtc;
  portEXIT_CRITICAL(&dtcMux);

  auto codes = [](const DtcList &list) {
    String s = "[";
    char b[10];
    for (int i = 0; i < list.count; i++) {
      snprintf(b, sizeof(b), "%s\"%02d-%02d\"", i ? "," : "", list.codes[i][0], list.codes[i][1]);
      s += b;
    }
    return s + "]";
  };

  String json = "{\"valid\":" + String(d.valid ? "true" : "false") +
                ",\"age\":" + String(d.updatedMs ? millis() - d.updatedMs : 0) +
                ",\"error\":\"" + d.error + "\"" +
                ",\"current\":" + codes(d.current) +
                ",\"stored\":" + codes(d.stored) + ",\"raw\":[";
  for (int i = 0; i < d.rawCount; i++) json += (i ? ",\"" : "\"") + String(d.raw[i]) + "\"";

  portENTER_CRITICAL(&dtcMux);
  ClearStatus c = clearStatus;
  portEXIT_CRITICAL(&dtcMux);
  static const char *names[] = {"idle", "pending", "sent", "ok", "failed"};
  json += "],\"clearEnabled\":" + String(ENABLE_DTC_CLEAR ? "true" : "false") +
          ",\"clear\":{\"state\":\"" + names[c.state] + "\",\"msg\":\"" + c.msg +
          "\",\"age\":" + String(c.atMs ? millis() - c.atMs : 0) + "}}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

// POST /dtc/clear: only with the key on, engine stopped and the ECU answering.
void handleDtcClear() {
  auto reply = [](int code, const char *state, const char *msg) {
    server.send(code, "application/json", String("{\"state\":\"") + state + "\",\"msg\":\"" + msg + "\"}");
  };
  if (!ENABLE_DTC_CLEAR) return reply(403, "failed", "disabled");

  portENTER_CRITICAL(&liveMux);
  LiveData d = live;
  portEXIT_CRITICAL(&liveMux);
  if (!d.valid || millis() - d.updatedMs > 1500) return reply(409, "failed", "no-ecu");
  if (d.rpm != 0) return reply(409, "failed", "engine-running");
  if (clearStatus.state == CLEAR_PENDING || clearStatus.state == CLEAR_SENT) return reply(409, "pending", "busy");

  outln("DTC clear requested from web page");
  setClear(CLEAR_PENDING, "");
  reply(202, "pending", "");
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
  server.on("/dtc", HTTP_GET, handleDtc);
  server.on("/dtc/clear", HTTP_POST, handleDtcClear);
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
