// Air Blade 125 dashboard for the Cheap Yellow Display 2.8" (ESP32-2432S028R).
//
// Receives DashPacket over ESP-NOW from the K-Line reader. When no packet arrives
// for a few seconds it runs a demo ride so the screen can be checked on the bench.
//
// Serial commands (115200 baud):
//   s  dump a screenshot (raw RGB565, see tools/screenshot.py)
//   d  toggle forced demo mode

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "dash_packet.h"

#ifndef ESPNOW_CHANNEL
#define ESPNOW_CHANNEL 1
#endif

TFT_eSPI tft;
TFT_eSprite speedSpr(&tft);
TFT_eSprite rpmSpr(&tft);
TFT_eSprite tileSpr(&tft);
TFT_eSprite badgeSpr(&tft);

// ---------- Theme ----------
static uint16_t C_BG, C_CARD, C_TRACK, C_TEXT, C_MUTED, C_ACCENT, C_WARN, C_BAD, C_GOOD;

static void initColors() {
  C_BG = tft.color565(8, 10, 14);
  C_CARD = tft.color565(24, 28, 36);
  C_TRACK = tft.color565(36, 42, 52);
  C_TEXT = tft.color565(240, 243, 247);
  C_MUTED = tft.color565(130, 140, 155);
  C_ACCENT = tft.color565(0, 200, 255);
  C_WARN = tft.color565(255, 176, 32);
  C_BAD = tft.color565(255, 77, 79);
  C_GOOD = tft.color565(61, 220, 132);
}

// ---------- Layout (landscape 320x240) ----------
static const int GX = 118, GY = 130;   // gauge centre
static const int GR = 104, GIR = 90;   // ring outer / inner radius
static const int SEGMENTS = 40;        // 250 rpm each
static const int RPM_MAX = 10000;
static const int SWEEP_START = 60;     // TFT_eSPI arc angles: 0 = 6 o'clock, clockwise
static const int SEG_STEP = 6;         // 40 * 6 = 240 degree sweep
static const int SEG_LEN = 4;          // 2 degree gap between segments

static const int TILE_X = 236, TILE_W = 80;
static const int TILE_Y[3] = {28, 100, 172};
static const int TILE_H = 66;

// ---------- Data ----------
struct DashData {
  uint16_t rpm;
  uint8_t kmh;
  uint8_t tps;
  int16_t ect;
  int16_t iat;
  float batt;
};

enum Source { SRC_DEMO, SRC_LIVE };

static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
static DashPacket rxPacket;
static volatile uint32_t rxMillis = 0;
static volatile uint32_t rxCount = 0;
static bool forceDemo = false;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onEspNowRecv(const esp_now_recv_info_t *, const uint8_t *data, int len) {
#else
static void onEspNowRecv(const uint8_t *, const uint8_t *data, int len) {
#endif
  if (len != sizeof(DashPacket)) return;
  const DashPacket *p = (const DashPacket *)data;
  if (p->magic != DASH_MAGIC || p->version != DASH_VERSION) return;
  portENTER_CRITICAL(&rxMux);
  rxPacket = *p;
  rxMillis = millis();
  rxCount++;
  portEXIT_CRITICAL(&rxMux);
}

static void initEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(onEspNowRecv);
  Serial.printf("ESP-NOW listening on channel %d, MAC %s\n", ESPNOW_CHANNEL, WiFi.macAddress().c_str());
}

// Demo ride: idle -> accelerate -> cruise -> brake -> idle, 20 s loop.
static DashData demoData(uint32_t ms) {
  float t = (ms % 20000) / 1000.0f;
  float rpm, kmh, tps;
  if (t < 3) {
    rpm = 1550 + 40 * sinf(t * 7);
    kmh = 0;
    tps = 0;
  } else if (t < 8) {
    float k = (t - 3) / 5;
    rpm = 1600 + 5800 * sqrtf(k);
    kmh = 62 * k;
    tps = 20 + 10 * k;
  } else if (t < 13) {
    rpm = 5300 + 120 * sinf(t * 3);
    kmh = 58 + 2 * sinf(t);
    tps = 12;
  } else if (t < 17) {
    float k = (t - 13) / 4;
    rpm = 5300 - 3700 * k;
    kmh = 58 * (1 - k);
    tps = 0;
  } else {
    rpm = 1550 + 40 * sinf(t * 7);
    kmh = 0;
    tps = 0;
  }
  DashData d;
  d.rpm = (uint16_t)rpm;
  d.kmh = (uint8_t)(kmh + 0.5f);
  d.tps = (uint8_t)tps;
  d.ect = min<int>(88, 30 + ms / 1500);  // warms up over the first ~90 s
  d.iat = 31;
  d.batt = rpm > 1000 ? 14.4f + 0.1f * sinf(t) : 12.6f;
  return d;
}

// ---------- Drawing helpers ----------
static uint16_t blend(uint16_t a, uint16_t b, float k) {
  uint8_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  uint8_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  uint8_t r = ar + (br - ar) * k, g = ag + (bg - ag) * k, bl = ab + (bb - ab) * k;
  return (r << 11) | (g << 5) | bl;
}

static uint16_t segmentColor(int i) {
  int segRpm = (i + 1) * (RPM_MAX / SEGMENTS);
  if (segRpm > 8000) return C_BAD;
  if (segRpm > 6500) return C_WARN;
  return blend(tft.color565(0, 140, 255), tft.color565(0, 230, 190), segRpm / 6500.0f);
}

static void drawSegment(int i, bool lit) {
  int a0 = SWEEP_START + i * SEG_STEP;
  tft.drawSmoothArc(GX, GY, GR, GIR, a0, a0 + SEG_LEN, lit ? segmentColor(i) : C_TRACK, C_BG, false);
}

// Point on a circle using TFT_eSPI arc angles (0 = bottom, clockwise).
static void arcPoint(int r, float deg, int &x, int &y) {
  float a = deg * DEG_TO_RAD;
  x = GX - (int)(r * sinf(a));
  y = GY + (int)(r * cosf(a));
}

static void drawDegree(TFT_eSprite &s, int x, int y, uint16_t c) {
  s.drawCircle(x, y, 2, c);
  s.drawCircle(x, y, 1, c);
}

static void drawStaticFrame() {
  tft.fillScreen(C_BG);

  // Title
  tft.setTextColor(C_MUTED, C_BG);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("AIR BLADE 125", 8, 6, 2);

  // Gauge track and labels
  for (int i = 0; i < SEGMENTS; i++) drawSegment(i, false);
  tft.setTextDatum(MC_DATUM);
  for (int k = 0; k <= 10; k += 2) {
    int x, y;
    arcPoint(GIR - 12, SWEEP_START + k * 24, x, y);
    tft.setTextColor(k >= 8 ? C_BAD : C_MUTED, C_BG);
    tft.drawNumber(k, x, y, 2);
  }
  // Minor ticks between labels
  for (int k = 0; k <= 20; k++) {
    int x0, y0, x1, y1;
    float deg = SWEEP_START + k * 12;
    arcPoint(GIR - 3, deg, x0, y0);
    arcPoint(GIR - (k % 2 ? 5 : 7), deg, x1, y1);
    tft.drawLine(x0, y0, x1, y1, k >= 16 ? C_BAD : C_MUTED);
  }
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("x1000", GX, GY + 92, 1);

  // Tiles
  const char *labels[3] = {"ENGINE", "BATTERY", "THROTTLE"};
  for (int i = 0; i < 3; i++) {
    tft.fillSmoothRoundRect(TILE_X, TILE_Y[i], TILE_W, TILE_H, 10, C_CARD, C_BG);
    tft.setTextColor(C_MUTED, C_CARD);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(labels[i], TILE_X + 9, TILE_Y[i] + 7, 2);
  }
}

// ---------- Dynamic parts (each cached so only changes are redrawn) ----------
static int litSegments = 0;

static void updateGauge(float rpm) {
  int target = constrain((int)(rpm / (RPM_MAX / SEGMENTS) + 0.5f), 0, SEGMENTS);
  while (litSegments < target) drawSegment(litSegments++, true);
  while (litSegments > target) drawSegment(--litSegments, false);
}

static int lastSpeed = -2;
static String lastCenterMsg = "x";

// Speed in the gauge centre; msg replaces it when there is no data.
static void updateSpeed(int kmh, const String &msg) {
  if (kmh == lastSpeed && msg == lastCenterMsg) return;
  lastSpeed = kmh;
  lastCenterMsg = msg;
  speedSpr.fillSprite(C_BG);
  speedSpr.setTextDatum(MC_DATUM);
  if (msg.length()) {
    speedSpr.setTextColor(C_WARN, C_BG);
    speedSpr.drawString(msg, speedSpr.width() / 2, 26, 2);
    speedSpr.setTextColor(C_MUTED, C_BG);
    speedSpr.drawString("--", speedSpr.width() / 2, 52, 4);
  } else {
    speedSpr.setTextColor(C_TEXT, C_BG);
    speedSpr.drawNumber(kmh, speedSpr.width() / 2, 34, 7);
    speedSpr.setTextColor(C_MUTED, C_BG);
    speedSpr.drawString("km/h", speedSpr.width() / 2, 70, 2);
  }
  speedSpr.pushSprite(GX - speedSpr.width() / 2, GY - 52);
}

static int lastRpmShown = -1;

static void updateRpmText(int rpm, bool valid) {
  int shown = valid ? (rpm / 10) * 10 : -2;
  if (shown == lastRpmShown) return;
  lastRpmShown = shown;
  rpmSpr.fillSprite(C_BG);
  uint16_t c = rpm > 8000 ? C_BAD : rpm > 6500 ? C_WARN : C_TEXT;
  char buf[12];
  if (valid) snprintf(buf, sizeof(buf), "%d", shown);
  else snprintf(buf, sizeof(buf), "----");
  // Number + small "rpm", centred together.
  int wNum = rpmSpr.textWidth(buf, 4), wUnit = rpmSpr.textWidth("rpm", 2);
  int x = (rpmSpr.width() - wNum - 4 - wUnit) / 2;
  rpmSpr.setTextDatum(BL_DATUM);
  rpmSpr.setTextColor(valid ? c : C_MUTED, C_BG);
  rpmSpr.drawString(buf, x, rpmSpr.height(), 4);
  rpmSpr.setTextColor(C_MUTED, C_BG);
  rpmSpr.drawString("rpm", x + wNum + 4, rpmSpr.height() - 3, 2);
  rpmSpr.pushSprite(GX - rpmSpr.width() / 2, GY + 28);
}

static String lastTile[3] = {"x", "x", "x"};

// Value + unit inside a tile. unit "C" draws a degree sign.
static void updateTile(int idx, const String &value, const char *unit, uint16_t color, int barPct = -1) {
  String key = value + unit + String(color) + String(barPct);
  if (key == lastTile[idx]) return;
  lastTile[idx] = key;

  tileSpr.fillSprite(C_CARD);
  tileSpr.setTextDatum(BL_DATUM);
  tileSpr.setTextColor(color, C_CARD);
  int w = tileSpr.drawString(value, 0, 30, 4);
  tileSpr.setTextColor(C_MUTED, C_CARD);
  if (strcmp(unit, "C") == 0) {
    drawDegree(tileSpr, w + 5, 9, C_MUTED);
    tileSpr.drawString("C", w + 9, 28, 2);
  } else {
    tileSpr.drawString(unit, w + 3, 28, 2);
  }
  if (barPct >= 0) {
    tileSpr.fillRoundRect(0, 34, 62, 5, 2, C_TRACK);
    int bw = map(constrain(barPct, 0, 100), 0, 100, 0, 62);
    if (bw > 0) tileSpr.fillRoundRect(0, 34, max(bw, 4), 5, 2, C_ACCENT);
  }
  tileSpr.pushSprite(TILE_X + 9, TILE_Y[idx] + 24);
}

static String lastBadge = "x";

static void updateBadge(const char *text, uint16_t color) {
  String key = String(text) + String(color);
  if (key == lastBadge) return;
  lastBadge = key;
  badgeSpr.fillSprite(C_BG);
  badgeSpr.fillSmoothRoundRect(0, 0, badgeSpr.width(), badgeSpr.height(), 8, C_CARD, C_BG);
  badgeSpr.fillSmoothCircle(11, badgeSpr.height() / 2, 4, color, C_CARD);
  badgeSpr.setTextDatum(ML_DATUM);
  badgeSpr.setTextColor(color, C_CARD);
  badgeSpr.drawString(text, 20, badgeSpr.height() / 2 + 1, 2);
  badgeSpr.pushSprite(320 - badgeSpr.width() - 4, 3);
}

// ---------- Screenshot over serial ----------
static void sendScreenshot() {
  const int w = tft.width(), h = tft.height();
  static uint16_t line[320];
  Serial.printf("\nSCREENSHOT %d %d\n", w, h);
  for (int y = 0; y < h; y++) {
    tft.readRect(0, y, w, 1, line);
    Serial.write((uint8_t *)line, w * 2);
  }
  Serial.println("\nEND");
}

// Panel colour test: 4 quadrants, each pre-compensated for one panel setting.
//   A = as is, B = panel inverts colours, C = panel swaps red/blue, D = both.
// The quadrant that looks correct (black background, true colours) shows which fix is needed.
static uint16_t swapRB(uint16_t c) { return ((c & 0x1F) << 11) | (c & 0x07E0) | (c >> 11); }

static void showPanelTest() {
  const char *names[4] = {"A", "B", "C", "D"};
  const uint16_t bars[4] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW};
  const char *barNames[4] = {"RED", "GREEN", "BLUE", "YELLOW"};
  for (int q = 0; q < 4; q++) {
    bool inv = q == 1 || q == 3, bgr = q >= 2;
    auto fix = [&](uint16_t c) {
      if (bgr) c = swapRB(c);
      return inv ? (uint16_t)~c : c;
    };
    int x0 = (q % 2) * 160, y0 = (q / 2) * 120;
    tft.fillRect(x0, y0, 160, 120, fix(TFT_BLACK));
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(fix(TFT_WHITE), fix(TFT_BLACK));
    tft.drawString(names[q], x0 + 6, y0 + 4, 4);
    for (int i = 0; i < 4; i++) {
      tft.fillRect(x0 + 40, y0 + 6 + i * 27, 112, 22, fix(bars[i]));
      tft.setTextColor(fix(TFT_BLACK), fix(bars[i]));
      tft.drawString(barNames[i], x0 + 46, y0 + 9 + i * 27, 2);
    }
  }
  tft.drawRect(0, 0, 320, 240, TFT_WHITE);
  Serial.println("Panel test shown; send any other key or reset to return");
  while (!Serial.available()) delay(20);
  Serial.read();
  drawStaticFrame();
  litSegments = 0;
  lastSpeed = -2;
  lastRpmShown = -1;
  lastBadge = "x";
  for (auto &t : lastTile) t = "x";
}

static void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 's') sendScreenshot();
    if (c == 'c') {
      // Readback calibration: draw pure colours, read them back, print raw values.
      const uint16_t cols[4] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE};
      for (int i = 0; i < 4; i++) {
        tft.fillRect(i * 10, 230, 10, 10, cols[i]);
        uint16_t v;
        tft.readRect(i * 10 + 5, 235, 1, 1, &v);
        Serial.printf("drawn %04X read %04X\n", cols[i], v);
      }
      Serial.printf("RDDID %02X %02X %02X\n", tft.readcommand8(0x04, 1), tft.readcommand8(0x04, 2),
                    tft.readcommand8(0x04, 3));
    }
    if (c == 'p') showPanelTest();
    if (c == 'd') {
      forceDemo = !forceDemo;
      Serial.printf("forceDemo=%d\n", forceDemo);
    }
  }
}

// ---------- Main ----------
static float shownRpm = 0;

static void bootSweep() {
  for (int i = 0; i < SEGMENTS; i++) {
    drawSegment(i, true);
    delay(12);
  }
  for (int i = SEGMENTS - 1; i >= 0; i--) {
    drawSegment(i, false);
    delay(8);
  }
  litSegments = 0;
}

void setup() {
  Serial.begin(115200);
  // CYD RGB LED is active low: keep it off.
  for (int pin : {4, 16, 17}) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }

  tft.init();
  tft.setRotation(1);
  initColors();

  uint8_t id1 = tft.readcommand8(0xD3, 2), id2 = tft.readcommand8(0xD3, 3);
  Serial.printf("\n=== AB125 dashboard ===\nDisplay ID: %02X%02X (9341 = ILI9341)\n", id1, id2);

  speedSpr.createSprite(140, 82);
  rpmSpr.createSprite(110, 28);
  tileSpr.createSprite(66, 40);
  badgeSpr.createSprite(78, 20);

  drawStaticFrame();
  bootSweep();
  initEspNow();
}

void loop() {
  handleSerial();

  uint32_t now = millis();
  DashPacket p;
  uint32_t rxAt, count;
  portENTER_CRITICAL(&rxMux);
  p = rxPacket;
  rxAt = rxMillis;
  count = rxCount;
  portEXIT_CRITICAL(&rxMux);

  bool haveLink = count > 0 && now - rxAt < 3000 && !forceDemo;
  DashData d;
  bool valid;
  String centerMsg;

  if (!haveLink) {
    d = demoData(now);
    valid = true;
    updateBadge("DEMO", C_WARN);
  } else {
    d.rpm = p.rpm;
    d.kmh = p.kmh;
    d.tps = p.tps;
    d.ect = p.ect;
    d.iat = p.iat;
    d.batt = p.battMv / 1000.0f;
    valid = p.status == DASH_OK && now - rxAt < 1500;
    if (valid) {
      updateBadge("LIVE", C_GOOD);
    } else {
      updateBadge("ECU", C_BAD);
      switch (p.status) {
        case DASH_NO_ECHO: centerMsg = "NO K-LINE"; break;
        case DASH_INIT_FAIL: centerMsg = "ECU NO REPLY"; break;
        case DASH_LOST: centerMsg = "RECONNECTING"; break;
        default: centerMsg = "CONNECTING"; break;
      }
    }
  }

  // Ease the gauge towards the target so it sweeps smoothly between packets.
  float target = valid ? d.rpm : 0;
  shownRpm += (target - shownRpm) * 0.35f;
  updateGauge(shownRpm);
  updateSpeed(d.kmh, valid ? "" : centerMsg);
  updateRpmText(d.rpm, valid);

  if (valid) {
    uint16_t ectColor = d.ect > 105 ? C_BAD : d.ect > 98 ? C_WARN : C_TEXT;
    updateTile(0, String(d.ect), "C", ectColor);
    bool lowCharge = d.rpm > 1000 && d.batt < 13.0f;
    uint16_t battColor = (d.batt > 15.0f || d.batt < 11.8f) ? C_BAD : lowCharge ? C_WARN : C_TEXT;
    updateTile(1, String(d.batt, 1), "V", battColor);
    updateTile(2, String(d.tps), "", C_TEXT, d.tps * 100 / 30);
  } else {
    updateTile(0, "--", "C", C_MUTED);
    updateTile(1, "--", "V", C_MUTED);
    updateTile(2, "--", "", C_MUTED, 0);
  }

  delay(16);
}
