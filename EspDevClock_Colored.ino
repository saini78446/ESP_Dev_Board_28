#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <sys/time.h>

// ================= Your credentials (edit this block) =================
struct WifiCred { const char* ssid; const char* pass; };

// Up to ~16 networks. The clock joins the strongest one it can see and
// re-picks automatically when you move floors. Leave unused ones as "".
const WifiCred WIFI_LIST[] = {
  { "TpLinkGround",       "ABC" },
  { "Tp-Link_CA1F",   "DEF" }
};

const int WIFI_COUNT = sizeof(WIFI_LIST) / sizeof(WIFI_LIST[0]);

// The READ key from notifications.php (not the write key)
const char* NOTIF_URL = "https://MYDomain.xyz/notifications.php?a=poll&key=chance_ce_dance_mar_le";

TFT_eSPI tft;
WiFiMulti wifiMulti;

// ================= Config =================
const bool INVERT_DISPLAY = false;   // your screen showed a negative image: false fixes it. Set true if colours ever look inverted again.

const char* NTP1 = "pool.ntp.org";
const char* NTP2 = "time.google.com";
const char* TZ_UK = "GMT0BST,M3.5.0/1,M10.5.0/2";
const char* TZ_IN = "IST-5:30";

const uint32_t POLL_MS      = 30000;   // notification check interval
const int      WEAK_RSSI    = -85;     // below this for ~30s -> look for a better known network
const uint32_t NTP_TIMEOUT_MS = 20000;

// Night dimming (IST hours). Extends backlight life, easier on the eyes.
const int BRIGHT_DAY   = 230;          // 0-255
const int BRIGHT_NIGHT = 25;
const int NIGHT_START  = 23;           // 23:00 ...
const int NIGHT_END    = 7;            // ... until 07:00

// Reliability
const bool DAILY_REBOOT   = false;     // optional 04:00 IST restart (off: a reboot with no WiFi means no clock until WiFi returns)
const int  REBOOT_HOUR_IST = 4;
const uint32_t LOW_HEAP   = 30000;     // restart if free heap stays below this for ~30s

// ================= Layout (320x240) =================
const int CX = 160;

const int HERO_Y  = 32;    // big IST time (font 7 @2x = 96px)
const int COLON_W = 36;
const int SEC_Y   = 134, SEC_X = 14, SEC_W = 292;
const int DATE_Y  = 146;

const int CARD_X = 14, CARD_Y = 168, CARD_W = 292, CARD_H = 36;
const int BAR_X  = 14, BAR_Y  = 210, BAR_W  = 292, BAR_H  = 26;

const int BELL_X = 292, BELL_Y = 15;
const int WIFI_X = 232;

// ================= Colours =================
const uint16_t C_BG    = TFT_BLACK;
const uint16_t C_IN    = 0xFD20;   // amber
const uint16_t C_UK    = TFT_CYAN;
const uint16_t C_DIM   = 0x39E7;
const uint16_t C_LABEL = 0x8410;
const uint16_t C_CARD  = 0x10C4;   // dark navy
const uint16_t C_ALERT = TFT_RED;
const uint16_t C_OK    = 0x07E0;

// ================= State =================
int    heroDW = 0, heroX = 0;
time_t lastMinute = -1;
int    barFill = 0;
int    lastParity = -1;

int    shownCount = 0;
String shownMsg = "";

bool   curWifi = false;
int    curLevel = 0;
String curSsid = "";
uint32_t lastStatusMs = 0;
int    lowHeapCount = 0;

int    blCur = BRIGHT_DAY, blTarget = BRIGHT_DAY;

// network task -> UI hand-off (UI only reads when pollReady is true)
volatile bool pollReady = false;
volatile int  pollCount = 0;
char pollMsg[96] = "";

// ================= Backlight =================
void backlightBegin() {
#ifdef TFT_BL
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(TFT_BL, 5000, 8);
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
#endif
#endif
}

void backlightSet(int v) {
#ifdef TFT_BL
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(TFT_BL, v);
#else
  ledcWrite(0, v);
#endif
#endif
}

// ================= Network =================
bool syncTime() {
  configTime(0, 0, NTP1, NTP2);   // system clock stays UTC
  uint32_t start = millis();
  while (time(nullptr) < 1700000000) {
    if (millis() - start > NTP_TIMEOUT_MS) return false;
    delay(250);
  }
  return true;
}

// Response: line 1 = count, line 2 = latest message
bool fetchNotifications(int &count, char* msgOut, size_t msgLen) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  if (!http.begin(client, NOTIF_URL)) return false;

  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String body = http.getString();
  http.end();

  body.trim();
  if (body.length() == 0 || !isDigit(body[0])) return false;

  int nl = body.indexOf('\n');
  count = body.substring(0, nl < 0 ? body.length() : nl).toInt();
  String m = nl < 0 ? "" : body.substring(nl + 1);
  m.trim();
  strlcpy(msgOut, m.c_str(), msgLen);
  return true;
}

// Runs on core 0: WiFi supervision + notification polling.
// Slow TLS / reconnects here never freeze the display.
void netTask(void*) {
  uint32_t lastPoll = 0;
  bool polled = false;
  int weak = 0;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      weak = 0;
      wifiMulti.run(10000);                       // joins best known network in range
    } else {
      if (WiFi.RSSI() < WEAK_RSSI) {
        if (++weak >= 6) {                        // weak for ~30s: re-pick best network
          weak = 0;
          WiFi.disconnect();
          wifiMulti.run(10000);
        }
      } else {
        weak = 0;
      }

      if (!pollReady && (!polled || millis() - lastPoll >= POLL_MS)) {
        polled = true;
        lastPoll = millis();
        int c; char m[96];
        if (fetchNotifications(c, m, sizeof(m))) {
          pollCount = c;
          strlcpy(pollMsg, m, sizeof(pollMsg));
          pollReady = true;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

// ================= Drawing =================
void showStatus(const char* text) {
  tft.fillScreen(C_BG);
  tft.setTextPadding(0);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_IN, C_BG);
  tft.drawString("WORLD CLOCK", CX, 95, 4);
  tft.setTextColor(C_LABEL, C_BG);
  tft.drawString(text, CX, 135, 2);
}

void toZone(const char* tz, time_t t, struct tm* out) {
  setenv("TZ", tz, 1);
  tzset();
  localtime_r(&t, out);
}

String fitText(String s, int maxW, int font) {
  if (tft.textWidth(s, font) <= maxW) return s;
  while (s.length() > 1 && tft.textWidth(s + "...", font) > maxW) s.remove(s.length() - 1);
  return s + "...";
}

void drawWifi() {
  const int base = 24;
  tft.fillRect(WIFI_X - 2, 4, 34, 24, C_BG);
  for (int i = 0; i < 4; i++) {
    int h = 6 + i * 5;                                   // 6, 11, 16, 21
    uint16_t c;
    if (!curWifi)            c = (i == 0) ? C_ALERT : C_DIM;
    else                     c = (i < curLevel) ? TFT_WHITE : C_DIM;
    tft.fillRoundRect(WIFI_X + i * 8, base - h, 6, h, 1, c);
  }
}

void drawBell(bool active, int count) {
  const int cx = BELL_X, cy = BELL_Y;
  uint16_t c = active ? C_ALERT : C_DIM;

  tft.fillRect(cx - 22, 0, 44, 31, C_BG);

  tft.fillCircle(cx, cy - 10, 2, c);                                      // knob
  tft.fillCircle(cx, cy - 3, 7, c);                                       // dome
  tft.fillTriangle(cx - 7, cy - 1, cx + 7, cy - 1, cx + 10, cy + 7, c);   // body
  tft.fillTriangle(cx - 7, cy - 1, cx - 10, cy + 7, cx + 10, cy + 7, c);
  tft.fillRoundRect(cx - 11, cy + 6, 22, 3, 1, c);                        // rim
  tft.fillCircle(cx, cy + 12, 2, c);                                      // clapper

  if (active) {
    char buf[3];
    if (count > 9) strcpy(buf, "9+"); else snprintf(buf, sizeof(buf), "%d", count);
    tft.fillCircle(cx + 10, cy - 8, 9, C_BG);                             // cut-out ring
    tft.fillCircle(cx + 10, cy - 8, 7, TFT_WHITE);                        // badge
    tft.setTextDatum(MC_DATUM);
    tft.setTextPadding(0);
    tft.setTextColor(C_ALERT, TFT_WHITE);
    tft.drawString(buf, cx + 10, cy - 8, 1);
  }
}

void drawNotifBar(bool active, const String &msg) {
  tft.setTextPadding(0);
  tft.fillRect(BAR_X, BAR_Y, BAR_W, BAR_H, C_BG);
  int y = BAR_Y + BAR_H / 2;

  if (active) {
    tft.fillRoundRect(BAR_X, BAR_Y, BAR_W, BAR_H, 8, C_ALERT);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_WHITE, C_ALERT);
    String t = fitText(msg.length() ? msg : "New notification", BAR_W - 24, 2);
    tft.drawString(t, BAR_X + 12, y, 2);       // drawn twice, 1px apart = bold
    tft.drawString(t, BAR_X + 13, y, 2);
  } else if (!curWifi) {
    tft.fillCircle(BAR_X + 8, y, 3, C_IN);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_IN, C_BG);
    tft.drawString("Searching for WiFi...", BAR_X + 20, y, 2);
  } else {
    tft.fillCircle(BAR_X + 8, y, 3, C_OK);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_LABEL, C_BG);
    tft.drawString("All clear", BAR_X + 20, y, 2);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString(fitText(curSsid, 150, 2), BAR_X + BAR_W - 6, y, 2);
  }
}

void drawStaticLayout() {
  tft.fillScreen(C_BG);

  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  tft.setTextColor(C_IN, C_BG);
  tft.drawString("INDIA", 14, 4, 4);
  int tw = tft.textWidth("INDIA", 4);
  tft.setTextColor(C_LABEL, C_BG);
  tft.drawString("IST", 14 + tw + 6, 13, 2);

  tft.fillRect(SEC_X, SEC_Y, SEC_W, 3, C_DIM);                         // seconds track

  tft.fillRoundRect(CARD_X, CARD_Y, CARD_W, CARD_H, 10, C_CARD);       // UK card
  tft.fillRoundRect(CARD_X + 10, CARD_Y + 8, 4, CARD_H - 16, 2, C_UK); // accent stripe
}

void drawHero(const struct tm &t) {
  char hh[3], mm[3];
  snprintf(hh, sizeof(hh), "%02d", t.tm_hour);
  snprintf(mm, sizeof(mm), "%02d", t.tm_min);

  tft.setTextSize(2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, C_BG);
  tft.setTextPadding(heroDW);
  tft.drawString(hh, heroX, HERO_Y, 7);
  tft.drawString(mm, heroX + heroDW + COLON_W, HERO_Y, 7);
  tft.setTextSize(1);
  tft.setTextPadding(0);
}

void drawColon(bool on) {
  int cx = heroX + heroDW + COLON_W / 2;
  uint16_t c = on ? C_IN : C_DIM;
  tft.fillCircle(cx, HERO_Y + 30, 5, c);
  tft.fillCircle(cx, HERO_Y + 66, 5, c);
}

void drawDate(const struct tm &t) {
  char date[40];
  strftime(date, sizeof(date), "%A, %d %B %Y", &t);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_LABEL, C_BG);
  tft.setTextPadding(SEC_W);
  tft.drawString(date, CX, DATE_Y, 2);
  tft.setTextPadding(0);
}

void drawUK(const struct tm &t) {
  char ukTime[8];
  strftime(ukTime, sizeof(ukTime), "%H:%M", &t);
  int mid = CARD_Y + CARD_H / 2;
  int lx = CARD_X + 24;

  tft.fillRect(CARD_X + 18, CARD_Y + 3, CARD_W - 30, CARD_H - 6, C_CARD);
  tft.setTextPadding(0);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, C_CARD);
  tft.drawString("UNITED KINGDOM", lx, mid, 2);
  int tw = tft.textWidth("UNITED KINGDOM", 2);
  tft.setTextColor(C_LABEL, C_CARD);
  tft.drawString(t.tm_isdst > 0 ? "BST" : "GMT", lx + tw + 8, mid, 2);

  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(C_UK, C_CARD);
  tft.drawString(ukTime, CARD_X + CARD_W - 14, mid, 4);
}

// Fills the progress bar a few pixels per second; blinks the colon
void updateSeconds(int sec) {
  int w = (sec + 1) * SEC_W / 60;
  if (w < barFill) {                                   // new minute
    tft.fillRect(SEC_X, SEC_Y, SEC_W, 3, C_DIM);
    barFill = 0;
  }
  if (w > barFill) {
    tft.fillRect(SEC_X + barFill, SEC_Y, w - barFill, 3, C_IN);
    barFill = w;
  }

  int parity = sec & 1;
  if (parity != lastParity) {
    lastParity = parity;
    drawColon(parity == 0);
  }
}

// Checks WiFi + heap every 10s; redraws only if something changed
void statusCheck() {
  bool ok = WiFi.status() == WL_CONNECTED;
  int level = 0;
  String ssid = "";
  if (ok) {
    int r = WiFi.RSSI();
    level = r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : 1;
    ssid = WiFi.SSID();
  }

  bool wifiChanged = (ok != curWifi) || (level != curLevel);
  bool ssidChanged = (ssid != curSsid) || (ok != curWifi);
  curWifi = ok; curLevel = level; curSsid = ssid;

  if (wifiChanged) drawWifi();
  if (ssidChanged && shownCount == 0) drawNotifBar(false, "");

  if (ESP.getFreeHeap() < LOW_HEAP) lowHeapCount++; else lowHeapCount = 0;
  if (lowHeapCount >= 3) ESP.restart();                // safety net for 24x7 running
}

// ================= Arduino =================
void setup() {
  setCpuFrequencyMhz(80);
  WiFi.persistent(false);       // never write WiFi settings to flash (24x7 friendly)
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false); // WiFiMulti handles reconnecting
  for (int i = 0; i < WIFI_COUNT; i++) {
    if (WIFI_LIST[i].ssid[0] != '\0') wifiMulti.addAP(WIFI_LIST[i].ssid, WIFI_LIST[i].pass);
  }

  tft.init();
  backlightBegin();
  backlightSet(BRIGHT_DAY);
  tft.setRotation(1);
  tft.invertDisplay(INVERT_DISPLAY);

  // measure big-digit width once so the layout centres itself
  tft.setTextSize(2);
  heroDW = tft.textWidth("00", 7);
  tft.setTextSize(1);
  heroX = (320 - (2 * heroDW + COLON_W)) / 2;

  showStatus("Searching for WiFi...");
  while (wifiMulti.run(10000) != WL_CONNECTED) { /* keeps trying every known network */ }

  showStatus("Syncing time...");
  while (!syncTime()) {
    showStatus("Time sync failed, retrying...");
    delay(2000);
    showStatus("Syncing time...");
  }

  curWifi = true;
  curLevel = 3;
  curSsid = WiFi.SSID();

  drawStaticLayout();
  drawBell(false, 0);
  drawWifi();
  drawNotifBar(false, "");

  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0);
}

void loop() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  time_t now = tv.tv_sec;

  // once a minute: hero time, date, UK card, brightness target
  if (now / 60 != lastMinute) {
    lastMinute = now / 60;
    struct tm tmIN, tmUK;
    toZone(TZ_IN, now, &tmIN);
    drawHero(tmIN);
    drawDate(tmIN);
    toZone(TZ_UK, now, &tmUK);
    drawUK(tmUK);
    lastParity = -1;

    int h = tmIN.tm_hour;
    bool night = (h >= NIGHT_START || h < NIGHT_END);
    blTarget = night ? BRIGHT_NIGHT : BRIGHT_DAY;

    if (DAILY_REBOOT && h == REBOOT_HOUR_IST && tmIN.tm_min == 0 && millis() > 3600000UL) ESP.restart();
  }

  // once a second: tiny updates only
  updateSeconds(now % 60);

  if (blCur != blTarget) {                              // gentle fade, ~12 levels/sec
    int d = blTarget - blCur;
    blCur += (d > 12) ? 12 : (d < -12) ? -12 : d;
    backlightSet(blCur);
  }

  if (millis() - lastStatusMs >= 10000 || lastStatusMs == 0) {
    lastStatusMs = millis();
    statusCheck();
  }

  // new notification data from the network task
  if (pollReady) {
    int c = pollCount;
    String m = String(pollMsg);
    pollReady = false;
    if (c != shownCount || m != shownMsg) {
      shownCount = c;
      shownMsg = m;
      drawBell(c > 0, c);
      drawNotifBar(c > 0, m);
    }
  }

  // sleep until the next second boundary
  gettimeofday(&tv, nullptr);
  int wait = 1000 - tv.tv_usec / 1000;
  delay(wait > 0 ? wait : 1);
}


