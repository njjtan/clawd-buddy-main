/*
 * ╔══════════════════════════════════════════════════════════════╗
 *   CLAWD MOCHI — ESP32-C3 Super Mini + ST7789 1.54" 240×240
 *
 *   Wiring:
 *     SDA → GPIO 10  (hardware SPI MOSI)
 *     SCL → GPIO 8   (hardware SPI SCK)
 *     RST → GPIO 2
 *     DC  → GPIO 1
 *     CS  → GPIO 4
 *     BL  → GPIO 3
 *     VCC → 3V3
 *     GND → GND
 *
 *   WiFi AP: "Clawd-Buddy"  pw: clawd1234  → http://192.168.4.1
 *   WiFi STA: 可通过手机配网连接家里 WiFi（获取 NTP 时间）
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <math.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>

// ── Pins ──────────────────────────────────────────────────────
#define TFT_CS  4
#define TFT_DC  1
#define TFT_RST 2
#define TFT_BLK 3

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ── WiFi ──────────────────────────────────────────────────────
const char* AP_SSID = "Clawd-Buddy";
const char* AP_PASS = "clawd1234";
WebServer server(80);
Preferences preferences;

String staSSID = "";
String staPass = "";
bool staConnected = false;
bool ntpSynced = false;

// ── Display ───────────────────────────────────────────────────
#define DISP_W 240
#define DISP_H 240

// ── Eye constants ─────────────────────────────────────────────
#define EYE_W   30
#define EYE_H   60
#define EYE_GAP 120
#define EYE_OX  0
#define EYE_OY  40

// ── Colours ───────────────────────────────────────────────────
uint16_t C_ORANGE, C_DARKBG, C_MUTED, C_GREEN, C_RED, C_BLUE, C_YELLOW;
#define C_WHITE ST77XX_WHITE
#define C_BLACK ST77XX_BLACK

// ── Mode definitions ──────────────────────────────────────────
#define MODE_CLAUDE   0
#define MODE_STANDBY  1
#define MODE_UTILITY  2

// Claude sub-states
#define CLAUDE_IDLE      0
#define CLAUDE_THINKING  1
#define CLAUDE_CODING    2
#define CLAUDE_ERROR     3
#define CLAUDE_DONE      4
#define CLAUDE_SLEEPING  5

// Standby sub-states
#define STANDBY_IDLE     0
#define STANDBY_BLINK    1
#define STANDBY_SLEEPY   2
#define STANDBY_ASLEEP   3
#define STANDBY_QUOTE    4

// Utility sub-states
#define UTIL_CLOCK       0
#define UTIL_POMODORO    1
#define UTIL_STOPWATCH   2

// ── State variables ───────────────────────────────────────────
uint8_t  currentMode   = MODE_STANDBY;
uint8_t  claudeState   = CLAUDE_IDLE;
uint8_t  standbyState  = STANDBY_IDLE;
uint8_t  utilState     = UTIL_CLOCK;
bool     busy          = false;
bool     backlightOn   = true;
uint8_t  animSpeed     = 1;
uint16_t animBgColor   = 0;
uint16_t drawBgColor   = 0;

// Claude mode
unsigned long claudeLastActivity = 0;
uint32_t tokenCount = 0;
uint8_t  claudeThinkFrame = 0;

// Standby mode
unsigned long lastBlinkTime    = 0;
unsigned long lastQuoteTime    = 0;
unsigned long lastActivityTime = 0;
unsigned long lastTimeCheck    = 0;
unsigned long asleepStartTime  = 0;  // when we entered ASLEEP state
unsigned long idleAfterQuote   = 0;  // when we entered IDLE after quote
uint16_t blinkInterval = 3000;
uint8_t  eyeOpenLevel  = 100;  // 0-100, for sleep animation
bool     sleepingAnimDone = false;
#define QUOTE_INTERVAL    900000   // 15 min between quotes while asleep
#define QUOTE_DURATION    60000    // quote shows for 1 min
#define IDLE_AFTER_QUOTE  60000    // blink for 1 min then sleep

// Pomodoro
bool     pomodoroActive = false;
bool     pomodoroBreak  = false;
unsigned long pomodoroStart = 0;
unsigned long pomodoroWorkMs = 1500000;  // 默认 25 分钟
#define POMODORO_BREAK   300000   // 5 min
#define POMODORO_BLINK   5000     // 5s blink warning

// Clock
unsigned long lastClockUpdate = 0;

// Stopwatch
bool     stopwatchRunning = false;
unsigned long stopwatchStart = 0;
unsigned long stopwatchElapsed = 0;  // accumulated when paused

// Animation frame tracking (non-blocking)
unsigned long animNextFrame = 0;
uint8_t       animFrameIdx  = 0;

// ── Terminal (保留兼容) ───────────────────────────────────────
#define TERM_COLS      15
#define TERM_ROWS       8
#define TERM_CHAR_W    12
#define TERM_CHAR_H    20
#define TERM_PAD_X      8
#define TERM_PAD_Y     18

bool    termMode    = false;
String  termLines[TERM_ROWS];
uint8_t termRow     = 0;
uint8_t termCol     = 0;

// ── Logo data (PROGMEM) ──────────────────────────────────────
#define LOGO_CX 120
#define LOGO_CY 105

#define LOGO_TRI_COUNT 162
static const int16_t LOGO_TRIS[][6] PROGMEM = {
  {120,105,65,134,100,114},{120,105,100,114,101,113},{120,105,101,113,100,112},
  {120,105,100,112,99,112},{120,105,99,112,93,111},{120,105,93,111,73,111},
  {120,105,73,111,55,110},{120,105,55,110,38,109},{120,105,38,109,34,108},
  {120,105,34,108,30,103},{120,105,30,103,30,100},{120,105,30,100,34,98},
  {120,105,34,98,39,98},{120,105,39,98,50,99},{120,105,50,99,67,100},
  {120,105,67,100,80,101},{120,105,80,101,98,103},{120,105,98,103,101,103},
  {120,105,101,103,101,102},{120,105,101,102,100,101},{120,105,100,101,100,100},
  {120,105,100,100,82,88},{120,105,82,88,63,76},{120,105,63,76,53,69},
  {120,105,53,69,48,65},{120,105,48,65,45,61},{120,105,45,61,44,54},
  {120,105,44,54,49,49},{120,105,49,49,55,49},{120,105,55,49,57,49},
  {120,105,57,49,64,55},{120,105,64,55,78,66},{120,105,78,66,96,79},
  {120,105,96,79,99,81},{120,105,99,81,100,81},{120,105,100,81,100,80},
  {120,105,100,80,99,78},{120,105,99,78,89,60},{120,105,89,60,78,41},
  {120,105,78,41,73,34},{120,105,73,34,72,29},{120,105,72,29,72,28},
  {120,105,72,28,72,27},{120,105,72,27,71,26},{120,105,71,26,71,25},
  {120,105,71,25,71,24},{120,105,71,24,77,16},{120,105,77,16,80,15},
  {120,105,80,15,87,16},{120,105,87,16,91,19},{120,105,91,19,95,29},
  {120,105,95,29,103,46},{120,105,103,46,114,68},{120,105,114,68,118,75},
  {120,105,118,75,119,81},{120,105,119,81,120,83},{120,105,120,83,121,83},
  {120,105,121,83,121,82},{120,105,121,82,122,69},{120,105,122,69,124,54},
  {120,105,124,54,126,34},{120,105,126,34,126,28},{120,105,126,28,129,21},
  {120,105,129,21,135,18},{120,105,135,18,139,20},{120,105,139,20,143,25},
  {120,105,143,25,142,28},{120,105,142,28,140,42},{120,105,140,42,136,64},
  {120,105,136,64,133,78},{120,105,133,78,135,78},{120,105,135,78,136,76},
  {120,105,136,76,144,67},{120,105,144,67,156,51},{120,105,156,51,162,45},
  {120,105,162,45,168,38},{120,105,168,38,172,35},{120,105,172,35,180,35},
  {120,105,180,35,185,43},{120,105,185,43,183,52},{120,105,183,52,175,62},
  {120,105,175,62,168,71},{120,105,168,71,159,83},{120,105,159,83,153,94},
  {120,105,153,94,154,94},{120,105,154,94,155,94},{120,105,155,94,176,90},
  {120,105,176,90,188,88},{120,105,188,88,201,85},{120,105,201,85,208,88},
  {120,105,208,88,208,91},{120,105,208,91,206,97},{120,105,206,97,191,101},
  {120,105,191,101,174,104},{120,105,174,104,148,110},{120,105,148,110,148,111},
  {120,105,148,111,148,111},{120,105,148,111,160,112},{120,105,160,112,165,112},
  {120,105,165,112,177,112},{120,105,177,112,200,114},{120,105,200,114,205,118},
  {120,105,205,118,209,123},{120,105,209,123,208,126},{120,105,208,126,199,131},
  {120,105,199,131,187,128},{120,105,187,128,159,121},{120,105,159,121,149,119},
  {120,105,149,119,147,119},{120,105,147,119,147,120},{120,105,147,120,156,128},
  {120,105,156,128,170,141},{120,105,170,141,189,158},{120,105,189,158,190,163},
  {120,105,190,163,188,166},{120,105,188,166,185,166},{120,105,185,166,169,153},
  {120,105,169,153,162,148},{120,105,162,148,148,136},{120,105,148,136,147,136},
  {120,105,147,136,147,137},{120,105,147,137,150,142},{120,105,150,142,168,168},
  {120,105,168,168,169,176},{120,105,169,176,168,179},{120,105,168,179,163,180},
  {120,105,163,180,158,179},{120,105,158,179,148,165},{120,105,148,165,137,149},
  {120,105,137,149,129,134},{120,105,129,134,128,135},{120,105,128,135,123,189},
  {120,105,123,189,120,192},{120,105,120,192,115,194},{120,105,115,194,110,191},
  {120,105,110,191,108,185},{120,105,108,185,110,174},{120,105,110,174,113,160},
  {120,105,113,160,116,148},{120,105,116,148,118,134},{120,105,118,134,119,129},
  {120,105,119,129,119,129},{120,105,119,129,118,129},{120,105,118,129,107,144},
  {120,105,107,144,91,166},{120,105,91,166,78,180},{120,105,78,180,75,181},
  {120,105,75,181,70,178},{120,105,70,178,70,173},{120,105,70,173,73,169},
  {120,105,73,169,91,146},{120,105,91,146,102,132},{120,105,102,132,109,124},
  {120,105,109,124,109,123},{120,105,109,123,108,123},{120,105,108,123,61,153},
  {120,105,61,153,52,155},{120,105,52,155,49,151},{120,105,49,151,49,146},
  {120,105,49,146,51,144},{120,105,51,144,65,134},{120,105,65,134,65,134},
};

#define LOGO_SEG_COUNT 162
static const int16_t LOGO_SEGS[][4] PROGMEM = {
  {65,134,100,114},{100,114,101,113},{101,113,100,112},{100,112,99,112},
  {99,112,93,111},{93,111,73,111},{73,111,55,110},{55,110,38,109},
  {38,109,34,108},{34,108,30,103},{30,103,30,100},{30,100,34,98},
  {34,98,39,98},{39,98,50,99},{50,99,67,100},{67,100,80,101},
  {80,101,98,103},{98,103,101,103},{101,103,101,102},{101,102,100,101},
  {100,101,100,100},{100,100,82,88},{82,88,63,76},{63,76,53,69},
  {53,69,48,65},{48,65,45,61},{45,61,44,54},{44,54,49,49},
  {49,49,55,49},{55,49,57,49},{57,49,64,55},{64,55,78,66},
  {78,66,96,79},{96,79,99,81},{99,81,100,81},{100,81,100,80},
  {100,80,99,78},{99,78,89,60},{89,60,78,41},{78,41,73,34},
  {73,34,72,29},{72,29,72,28},{72,28,72,27},{72,27,71,26},
  {71,26,71,25},{71,25,71,24},{71,24,77,16},{77,16,80,15},
  {80,15,87,16},{87,16,91,19},{91,19,95,29},{95,29,103,46},
  {103,46,114,68},{114,68,118,75},{118,75,119,81},{119,81,120,83},
  {120,83,121,83},{121,83,121,82},{121,82,122,69},{122,69,124,54},
  {124,54,126,34},{126,34,126,28},{126,28,129,21},{129,21,135,18},
  {135,18,139,20},{139,20,143,25},{143,25,142,28},{142,28,140,42},
  {140,42,136,64},{136,64,133,78},{133,78,135,78},{135,78,136,76},
  {136,76,144,67},{144,67,156,51},{156,51,162,45},{162,45,168,38},
  {168,38,172,35},{172,35,180,35},{180,35,185,43},{185,43,183,52},
  {183,52,175,62},{175,62,168,71},{168,71,159,83},{159,83,153,94},
  {153,94,154,94},{154,94,155,94},{155,94,176,90},{176,90,188,88},
  {188,88,201,85},{201,85,208,88},{208,88,208,91},{208,91,206,97},
  {206,97,191,101},{191,101,174,104},{174,104,148,110},{148,110,148,111},
  {148,111,148,111},{148,111,160,112},{160,112,165,112},{165,112,177,112},
  {177,112,200,114},{200,114,205,118},{205,118,209,123},{209,123,208,126},
  {208,126,199,131},{199,131,187,128},{187,128,159,121},{159,121,149,119},
  {149,119,147,119},{147,119,147,120},{147,120,156,128},{156,128,170,141},
  {170,141,189,158},{189,158,190,163},{190,163,188,166},{188,166,185,166},
  {185,166,169,153},{169,153,162,148},{162,148,148,136},{148,136,147,136},
  {147,136,147,137},{147,137,150,142},{150,142,168,168},{168,168,169,176},
  {169,176,168,179},{168,179,163,180},{163,180,158,179},{158,179,148,165},
  {148,165,137,149},{137,149,129,134},{129,134,128,135},{128,135,123,189},
  {123,189,120,192},{120,192,115,194},{115,194,110,191},{110,191,108,185},
  {108,185,110,174},{110,174,113,160},{113,160,116,148},{116,148,118,134},
  {118,134,119,129},{119,129,119,129},{119,129,118,129},{118,129,107,144},
  {107,144,91,166},{91,166,78,180},{78,180,75,181},{75,181,70,178},
  {70,178,70,173},{70,173,73,169},{73,169,91,146},{91,146,102,132},
  {102,132,109,124},{109,124,109,123},{109,123,108,123},{108,123,61,153},
  {61,153,52,155},{52,155,49,151},{49,151,49,146},{49,146,51,144},
  {51,144,65,134},{65,134,65,134},
};

// ── Quotes (PROGMEM) ─────────────────────────────────────────
#define QUOTE_COUNT 25
const char* const QUOTES[QUOTE_COUNT] PROGMEM = {
  "Time to drink water!",
  "Rest your eyes~",
  "Take a walk!",
  "You've been focused",
  "Deep breath...",
  "Nice code! :)",
  "Coffee break?",
  "Keep going!",
  "Stretch your legs",
  "Stay hydrated!",
  "You're doing great",
  "Maybe stand up?",
  "Blink a few times",
  "Good progress!",
  "Almost there~",
  "Snack time?",
  "Fresh air helps",
  "Pat on the back!",
  "Well done so far",
  "Small break = ok",
  "Eye yoga time!",
  "Posture check!",
  "Smile :)",
  "You got this!",
  "Hydration check!"
};

// ═════════════════════════════════════════════════════════════
//  HELPERS
// ═════════════════════════════════════════════════════════════

int speedMs(int ms) {
  if (animSpeed == 3) return ms / 2;
  if (animSpeed == 1) return ms * 2;
  return ms;
}

uint16_t hexToRgb565(String hex) {
  hex.replace("#", "");
  if (hex.length() != 6) return C_WHITE;
  long v = strtol(hex.c_str(), nullptr, 16);
  return tft.color565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

void setBacklight(bool on) {
  backlightOn = on;
  digitalWrite(TFT_BLK, on ? HIGH : LOW);
}

void initColours() {
  C_ORANGE = tft.color565(218, 17, 0);
  C_DARKBG = tft.color565(10, 12, 16);
  C_MUTED  = tft.color565(90, 88, 86);
  C_GREEN  = tft.color565(80, 220, 130);
  C_RED    = tft.color565(255, 60, 60);
  C_BLUE   = tft.color565(80, 140, 255);
  C_YELLOW = tft.color565(255, 220, 60);
  animBgColor = C_ORANGE;
  drawBgColor = C_ORANGE;
}

String rgb565ToHex(uint16_t c) {
  uint8_t r = ((c >> 11) & 0x1F) << 3;
  uint8_t g = ((c >> 5)  & 0x3F) << 2;
  uint8_t b = (c & 0x1F) << 3;
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
  return String(buf);
}

void recordActivity() {
  lastActivityTime = millis();
}

// ═════════════════════════════════════════════════════════════
//  LOGO
// ═════════════════════════════════════════════════════════════

void drawLogoFilled(uint16_t bg, uint16_t fg) {
  tft.fillScreen(bg);
  for (uint16_t i = 0; i < LOGO_TRI_COUNT; i++) {
    tft.fillTriangle(
      pgm_read_word(&LOGO_TRIS[i][0]), pgm_read_word(&LOGO_TRIS[i][1]),
      pgm_read_word(&LOGO_TRIS[i][2]), pgm_read_word(&LOGO_TRIS[i][3]),
      pgm_read_word(&LOGO_TRIS[i][4]), pgm_read_word(&LOGO_TRIS[i][5]),
      fg);
  }
  tft.setTextColor(fg); tft.setTextSize(2);
  tft.setCursor(LOGO_CX - 54, 210); tft.print("Anthropic");
  tft.setCursor(LOGO_CX - 53, 210); tft.print("Anthropic");
}

// ═════════════════════════════════════════════════════════════
//  EYE COORDINATE HELPERS
// ═════════════════════════════════════════════════════════════

inline int16_t eyeLX(int16_t ox = 0) {
  return (DISP_W - (EYE_W * 2 + EYE_GAP)) / 2 + EYE_OX + ox;
}
inline int16_t eyeRX(int16_t ox = 0) { return eyeLX(ox) + EYE_W + EYE_GAP; }
inline int16_t eyeY()            { return (DISP_H - EYE_H) / 2 - EYE_OY; }
inline int16_t eyeCY()           { return eyeY() + EYE_H / 2; }

// ═════════════════════════════════════════════════════════════
//  EYE DRAWING — BASIC SHAPES
// ═════════════════════════════════════════════════════════════

// Normal rectangular eyes
void drawNormalEyes(int16_t ox = 0, bool blink = false) {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(ox), rx = eyeRX(ox), ey = eyeY();
  if (!blink) {
    tft.fillRect(lx, ey, EYE_W, EYE_H, C_BLACK);
    tft.fillRect(rx, ey, EYE_W, EYE_H, C_BLACK);
  } else {
    tft.fillRect(lx, ey + EYE_H / 2 - 3, EYE_W, 6, C_BLACK);
    tft.fillRect(rx, ey + EYE_H / 2 - 3, EYE_W, 6, C_BLACK);
  }
}

// Chevron shape (used for squished eyes)
void drawChevron(int16_t cx, int16_t cy, int16_t arm, int16_t reach,
                 uint8_t thk, bool rightFacing, uint16_t col) {
  for (int8_t t = -(int8_t)thk; t <= (int8_t)thk; t++) {
    if (rightFacing) {
      tft.drawLine(cx - reach/2, cy - arm + t, cx + reach/2, cy + t,      col);
      tft.drawLine(cx + reach/2, cy + t,       cx - reach/2, cy + arm + t, col);
    } else {
      tft.drawLine(cx + reach/2, cy - arm + t, cx - reach/2, cy + t,      col);
      tft.drawLine(cx - reach/2, cy + t,       cx + reach/2, cy + arm + t, col);
    }
  }
}

// Squished eyes (> <)
void drawSquishEyes(bool closed = false) {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();
  const int16_t arm   = EYE_H / 2;
  const int16_t reach = EYE_W / 2;
  const int16_t lcx   = lx + EYE_W / 2;
  const int16_t rcx   = rx + EYE_W / 2;
  if (!closed) {
    drawChevron(lcx, cy, arm, reach, 10, true,  C_BLACK);
    drawChevron(rcx, cy, arm, reach, 10, false, C_BLACK);
  } else {
    tft.fillRect(lx, cy - 5, EYE_W, 10, C_BLACK);
    tft.fillRect(rx, cy - 5, EYE_W, 10, C_BLACK);
  }
}

// ═════════════════════════════════════════════════════════════
//  EYE DRAWING — CLAUDE MODE EXPRESSIONS
// ═════════════════════════════════════════════════════════════

// Thinking eyes — normal rectangles that look around
void drawThinkingEyes(int16_t ox = 0) {
  drawNormalEyes(ox, false);
  // Add small "thinking" indicator - slight eyebrow lines
  const int16_t lx = eyeLX(ox), rx = eyeRX(ox), ey = eyeY();
  tft.drawLine(lx, ey - 4, lx + EYE_W, ey - 6, C_BLACK);
  tft.drawLine(rx, ey - 6, rx + EYE_W, ey - 4, C_BLACK);
}

// Coding/focus eyes — squished (> <)
void drawCodingEyes() {
  drawSquishEyes(false);
}

// Error eyes — crying T_T
void drawErrorEyes() {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), ey = eyeY();
  const int16_t cy = eyeCY();

  // Sad eyebrows (slanting down toward center)
  tft.drawLine(lx - 2, ey - 8, lx + EYE_W + 2, ey - 3, C_BLACK);
  tft.drawLine(rx - 2, ey - 3, rx + EYE_W + 2, ey - 8, C_BLACK);

  // Eyes (slightly smaller)
  tft.fillRect(lx + 3, ey + 5, EYE_W - 6, EYE_H - 10, C_BLACK);
  tft.fillRect(rx + 3, ey + 5, EYE_W - 6, EYE_H - 10, C_BLACK);

  // Tears (short vertical lines below eyes)
  tft.fillRect(lx + EYE_W / 2 - 2, ey + EYE_H + 2, 4, 12, C_BLUE);
  tft.fillRect(rx + EYE_W / 2 - 2, ey + EYE_H + 2, 4, 12, C_BLUE);

  // "T_T" text
  tft.setTextColor(C_RED); tft.setTextSize(2);
  tft.setCursor(DISP_W / 2 - 18, DISP_H - 40);
  tft.print("T_T");
}

// Done/happy eyes — curved ^^
void drawDoneEyes() {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), ey = eyeY();
  const int16_t cy = eyeCY();

  // Happy eyebrows (slanting up toward center)
  tft.drawLine(lx - 2, ey - 3, lx + EYE_W + 2, ey - 8, C_BLACK);
  tft.drawLine(rx - 2, ey - 8, rx + EYE_W + 2, ey - 3, C_BLACK);

  // Happy eyes — arc shapes (approximate with lines)
  // Left eye: upward arc
  for (int i = 0; i < EYE_W; i++) {
    int16_t arcY = cy - 10 + (int)(10.0 * cos(PI * i / EYE_W));
    tft.drawPixel(lx + i, arcY, C_BLACK);
    tft.drawPixel(lx + i, arcY + 1, C_BLACK);
    tft.drawPixel(lx + i, arcY + 2, C_BLACK);
  }
  // Right eye: upward arc
  for (int i = 0; i < EYE_W; i++) {
    int16_t arcY = cy - 10 + (int)(10.0 * cos(PI * i / EYE_W));
    tft.drawPixel(rx + i, arcY, C_BLACK);
    tft.drawPixel(rx + i, arcY + 1, C_BLACK);
    tft.drawPixel(rx + i, arcY + 2, C_BLACK);
  }

  // ":)" text
  tft.setTextColor(C_GREEN); tft.setTextSize(2);
  tft.setCursor(DISP_W / 2 - 12, DISP_H - 40);
  tft.print(":)");
}

// Sleeping eyes — closed lines + Zzz
void drawSleepingEyes(bool showZzz = false) {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();

  // Closed eyes (horizontal lines)
  tft.fillRect(lx, cy - 3, EYE_W, 6, C_BLACK);
  tft.fillRect(rx, cy - 3, EYE_W, 6, C_BLACK);

  // Zzz animation
  if (showZzz) {
    tft.setTextColor(C_MUTED); tft.setTextSize(2);
    tft.setCursor(DISP_W - 50, 30);  tft.print("Z");
    tft.setCursor(DISP_W - 40, 15);  tft.print("z");
    tft.setCursor(DISP_W - 30, 5);   tft.print("z");
  }
}

// Gradual close eyes (for drowsy animation)
void drawPartialEyes(uint8_t openLevel) {
  // openLevel: 100 = fully open, 0 = fully closed
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), ey = eyeY();
  const int16_t cy = eyeCY();

  if (openLevel >= 90) {
    // Full open
    tft.fillRect(lx, ey, EYE_W, EYE_H, C_BLACK);
    tft.fillRect(rx, ey, EYE_W, EYE_H, C_BLACK);
  } else if (openLevel <= 10) {
    // Closed
    tft.fillRect(lx, cy - 3, EYE_W, 6, C_BLACK);
    tft.fillRect(rx, cy - 3, EYE_W, 6, C_BLACK);
  } else {
    // Partially open — scale eye height
    int16_t h = (EYE_H * openLevel) / 100;
    if (h < 6) h = 6;
    int16_t topY = cy - h / 2;
    tft.fillRect(lx, topY, EYE_W, h, C_BLACK);
    tft.fillRect(rx, topY, EYE_W, h, C_BLACK);
  }
}

// ═════════════════════════════════════════════════════════════
//  TOKEN COUNTER DISPLAY
// ═════════════════════════════════════════════════════════════

void drawTokenCount() {
  if (tokenCount == 0) return;

  // Position: bottom-right corner
  const int16_t x = DISP_W - 70;
  const int16_t y = DISP_H - 22;

  // Clear area
  tft.fillRect(x - 2, y - 2, 74, 22, animBgColor);

  // Choose color based on token count
  uint16_t col = C_WHITE;
  if (tokenCount > 50000) col = C_RED;
  else if (tokenCount > 10000) col = C_YELLOW;

  // Format: "1.2k" / "5.8k" / "123k"
  tft.setTextColor(col); tft.setTextSize(1);
  tft.setCursor(x, y);
  if (tokenCount < 1000) {
    tft.printf("%u", tokenCount);
  } else if (tokenCount < 10000) {
    tft.printf("%.1fk", tokenCount / 1000.0);
  } else {
    tft.printf("%uk", tokenCount / 1000);
  }
}

// ═════════════════════════════════════════════════════════════
//  CLAUDE CODE VIEW
// ═════════════════════════════════════════════════════════════

void drawCodeView() {
  termMode = false;
  tft.fillScreen(C_DARKBG);
  tft.fillRect(0, 0,          DISP_W, 4, C_ORANGE);
  tft.fillRect(0, DISP_H - 4, DISP_W, 4, C_ORANGE);
  tft.setTextColor(C_ORANGE); tft.setTextSize(4);
  tft.setCursor((DISP_W - 144) / 2, DISP_H / 2 - 52); tft.print("Claude");
  tft.setTextColor(C_WHITE);  tft.setTextSize(4);
  tft.setCursor((DISP_W - 96) / 2,  DISP_H / 2 + 8);  tft.print("Code");
  tft.fillRect((DISP_W - 96) / 2, DISP_H / 2 + 52, 96, 3, C_ORANGE);
}

// ═════════════════════════════════════════════════════════════
//  TERMINAL (保留兼容)
// ═════════════════════════════════════════════════════════════

void termClear() {
  for (uint8_t i = 0; i < TERM_ROWS; i++) termLines[i] = "";
  termRow = 0; termCol = 0;
}

void termDrawHeader() {
  tft.fillRect(0, 0, DISP_W, TERM_PAD_Y + 1, C_DARKBG);
  tft.setTextColor(C_ORANGE); tft.setTextSize(1);
  tft.setCursor(TERM_PAD_X, 4); tft.print("clawd@buddy terminal");
  tft.drawFastHLine(0, TERM_PAD_Y, DISP_W, C_ORANGE);
}

void termDrawPrefix(int16_t yy) {
  tft.setTextColor(C_GREEN); tft.setTextSize(1);
  tft.setCursor(TERM_PAD_X, yy + 6);
  tft.print("clawd:~$ ");
}

#define PREFIX_PX 54

void termDrawLine(uint8_t r) {
  const int16_t yy = TERM_PAD_Y + 4 + r * TERM_CHAR_H;
  tft.fillRect(0, yy, DISP_W, TERM_CHAR_H, C_DARKBG);
  if (r == termRow) termDrawPrefix(yy);
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(TERM_PAD_X + PREFIX_PX, yy + 1);
  tft.print(termLines[r]);
  if (r == termRow) {
    const int16_t cx = TERM_PAD_X + PREFIX_PX + termCol * TERM_CHAR_W;
    tft.fillRect(cx, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
  }
}

void termDrawLastChar() {
  if (termCol == 0) return;
  const int16_t yy    = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
  const int16_t baseX = TERM_PAD_X + PREFIX_PX;
  const uint8_t prev  = termCol - 1;
  tft.fillRect(baseX + prev * TERM_CHAR_W, yy + 1, TERM_CHAR_W, TERM_CHAR_H - 1, C_DARKBG);
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(baseX + prev * TERM_CHAR_W, yy + 1);
  tft.print(termLines[termRow][prev]);
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
}

void termDrawBackspace() {
  const int16_t yy    = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
  const int16_t baseX = TERM_PAD_X + PREFIX_PX;
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W * 2, TERM_CHAR_H - 1, C_DARKBG);
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
  if (termLines[termRow].length() == 0) {
    tft.fillRect(0, yy, TERM_PAD_X + PREFIX_PX, TERM_CHAR_H, C_DARKBG);
  }
}

void termFullRedraw() {
  tft.fillScreen(C_DARKBG);
  termDrawHeader();
  for (uint8_t r = 0; r < TERM_ROWS; r++) termDrawLine(r);
}

void termScroll() {
  for (uint8_t i = 0; i < TERM_ROWS - 1; i++) termLines[i] = termLines[i + 1];
  termLines[TERM_ROWS - 1] = "";
  termRow = TERM_ROWS - 1;
  termFullRedraw();
}

void termAddChar(char c) {
  if (c == '\n' || c == '\r') {
    const int16_t yy = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
    tft.fillRect(TERM_PAD_X + PREFIX_PX + termCol * TERM_CHAR_W,
                 yy + 1, TERM_CHAR_W, TERM_CHAR_H - 1, C_DARKBG);
    termRow++; termCol = 0;
    if (termRow >= TERM_ROWS) { termScroll(); return; }
    termDrawLine(termRow);
  } else if (c == '\b' || c == 127) {
    if (termCol > 0) {
      termCol--;
      termLines[termRow].remove(termLines[termRow].length() - 1);
      termDrawBackspace();
    }
  } else if (c >= 32 && c < 127) {
    if (termCol >= TERM_COLS) {
      termRow++; termCol = 0;
      if (termRow >= TERM_ROWS) { termScroll(); return; }
    }
    if (termCol == 0) termDrawPrefix(TERM_PAD_Y + 4 + termRow * TERM_CHAR_H);
    termLines[termRow] += c;
    termCol++;
    termDrawLastChar();
  }
}

// ═════════════════════════════════════════════════════════════
//  PIXEL CLOCK DISPLAY
// ═════════════════════════════════════════════════════════════

void drawPixelClock() {
  struct tm t;
  if (!getLocalTime(&t)) {
    tft.fillScreen(animBgColor);
    tft.setTextColor(C_MUTED); tft.setTextSize(3);
    tft.setCursor(30, DISP_H / 2 - 12);
    tft.print("--:--");
    tft.setTextSize(1);
    tft.setCursor(30, DISP_H / 2 + 24);
    tft.print("waiting for NTP...");
    return;
  }

  tft.fillScreen(animBgColor);

  uint16_t timeColor = C_WHITE;
  if (t.tm_hour >= 23 || t.tm_hour < 6) timeColor = C_MUTED;
  else if (t.tm_hour >= 18) timeColor = C_ORANGE;

  tft.setTextColor(timeColor); tft.setTextSize(6);
  char buf[4];

  snprintf(buf, sizeof(buf), "%02d", t.tm_hour);
  tft.setCursor((DISP_W - 6 * 6 * 2) / 2, 25);
  tft.print(buf);

  snprintf(buf, sizeof(buf), "%02d", t.tm_min);
  tft.setCursor((DISP_W - 6 * 6 * 2) / 2, 90);
  tft.print(buf);

  tft.setTextColor(C_WHITE); tft.setTextSize(3);
  snprintf(buf, sizeof(buf), "%02d", t.tm_sec);
  tft.setCursor((DISP_W - 6 * 3 * 2) / 2, 155);
  tft.print(buf);

  tft.setTextColor(C_MUTED); tft.setTextSize(1);
  const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  char dateBuf[24];
  snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d %s",
           t.tm_year+1900, t.tm_mon+1, t.tm_mday, days[t.tm_wday]);
  tft.setCursor((DISP_W - strlen(dateBuf)*6)/2, DISP_H-18);
  tft.print(dateBuf);
}

// ═════════════════════════════════════════════════════════════
//  POMODORO TIMER DISPLAY
// ═════════════════════════════════════════════════════════════

void drawPomodoro() {
  unsigned long elapsed = millis() - pomodoroStart;
  unsigned long duration = pomodoroBreak ? POMODORO_BREAK : pomodoroWorkMs;
  unsigned long remaining = 0;
  bool overtime = false;

  if (elapsed >= duration) {
    overtime = true;
    remaining = 0;
  } else {
    remaining = duration - elapsed;
  }

  unsigned long totalSec = remaining / 1000;
  uint8_t mins = totalSec / 60;
  uint8_t secs = totalSec % 60;

  // Blink red when overtime
  bool blinkOn = true;
  if (overtime) {
    blinkOn = (millis() / 500) % 2 == 0;
  }

  tft.fillScreen(animBgColor);

  // Status label
  tft.setTextSize(2);
  if (pomodoroBreak) {
    tft.setTextColor(blinkOn ? C_GREEN : animBgColor);
    tft.setCursor((DISP_W - 108) / 2, 30);
    tft.print("BREAK TIME");
  } else {
    tft.setTextColor(C_ORANGE);
    tft.setCursor((DISP_W - 72) / 2, 30);
    tft.print("POMODORO");
  }

  // Timer: MM / SS (two lines)
  char buf[4];
  uint16_t timerColor = overtime ? (blinkOn ? C_RED : animBgColor) : C_WHITE;
  tft.setTextColor(timerColor); tft.setTextSize(6);
  snprintf(buf, sizeof(buf), "%02u", mins);
  tft.setCursor((DISP_W - 6*6*2)/2, 60);
  tft.print(buf);
  snprintf(buf, sizeof(buf), "%02u", secs);
  tft.setCursor((DISP_W - 6*6*2)/2, 120);
  tft.print(buf);

  // Progress bar
  if (!overtime) {
    uint16_t barW = DISP_W - 40;
    uint16_t fillW = (barW * (duration - remaining)) / duration;
    tft.drawRect(20, DISP_H - 25, barW, 12, C_MUTED);
    uint16_t barColor = pomodoroBreak ? C_GREEN : C_ORANGE;
    tft.fillRect(21, DISP_H - 24, fillW > 0 ? fillW - 1 : 0, 10, barColor);
  }

  // Eye indicator (small pair at top)
  if (overtime) {
    // Red blinking small eyes
    if (blinkOn) {
      tft.fillRect(DISP_W / 2 - 25, 8, 8, 8, C_RED);
      tft.fillRect(DISP_W / 2 + 17, 8, 8, 8, C_RED);
    }
  } else {
    // Normal small eyes
    tft.fillRect(DISP_W / 2 - 25, 8, 8, 8, C_WHITE);
    tft.fillRect(DISP_W / 2 + 17, 8, 8, 8, C_WHITE);
  }
}

// ═════════════════════════════════════════════════════════════
//  STOPWATCH DISPLAY
// ═════════════════════════════════════════════════════════════

void drawStopwatch() {
  unsigned long elapsed = stopwatchElapsed;
  if (stopwatchRunning) {
    elapsed += millis() - stopwatchStart;
  }

  unsigned long totalMs = elapsed;
  unsigned int hours = totalMs / 3600000;
  unsigned int mins = (totalMs % 3600000) / 60000;
  unsigned int secs = (totalMs % 60000) / 1000;
  unsigned int ms = (totalMs % 1000) / 10;

  tft.fillScreen(animBgColor);

  // 标题
  tft.setTextColor(stopwatchRunning ? C_GREEN : C_MUTED);
  tft.setTextSize(2);
  tft.setCursor((DISP_W - 84) / 2, 10);
  tft.print(stopwatchRunning ? "RUNNING" : "STOPPED");

  // 三行时间: HH / MM / SS
  char buf[4];
  tft.setTextColor(C_WHITE); tft.setTextSize(6);

  snprintf(buf, sizeof(buf), "%02u", hours);
  tft.setCursor((DISP_W - 6*6*2)/2, 35);
  tft.print(buf);

  snprintf(buf, sizeof(buf), "%02u", mins);
  tft.setCursor((DISP_W - 6*6*2)/2, 95);
  tft.print(buf);

  snprintf(buf, sizeof(buf), "%02u", secs);
  tft.setCursor((DISP_W - 6*6*2)/2, 155);
  tft.print(buf);

  // 毫秒小字（右下角）
  tft.setTextColor(C_ORANGE); tft.setTextSize(1);
  char msBuf[4];
  snprintf(msBuf, sizeof(msBuf), ".%02u", ms);
  tft.setCursor(DISP_W - 30, DISP_H - 15);
  tft.print(msBuf);
}

// ═════════════════════════════════════════════════════════════
//  QUOTE DISPLAY
// ═════════════════════════════════════════════════════════════

void drawQuote() {
  // Pick a random quote
  uint8_t idx = random(QUOTE_COUNT);
  char quoteBuf[64];
  strncpy_P(quoteBuf, (char*)pgm_read_ptr(&QUOTES[idx]), sizeof(quoteBuf) - 1);
  quoteBuf[sizeof(quoteBuf) - 1] = '\0';

  // Draw open eyes + quote text (waking up to show quote)
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), ey = eyeY();
  tft.fillRect(lx, ey, EYE_W, EYE_H, C_BLACK);
  tft.fillRect(rx, ey, EYE_W, EYE_H, C_BLACK);

  // Quote text below eyes
  tft.setTextColor(C_WHITE); tft.setTextSize(1);
  int16_t tw = strlen(quoteBuf) * 6;
  int16_t tx = (DISP_W - tw) / 2;
  if (tx < 4) tx = 4;
  tft.setCursor(tx, DISP_H - 40);
  tft.print(quoteBuf);
}

// ═════════════════════════════════════════════════════════════
//  ANIMATIONS (原版，保留)
// ═════════════════════════════════════════════════════════════

void animNormalEyes() {
  busy = true;
  const int16_t offs[] = {-16, 16, -16, 16, 0};
  for (uint8_t i = 0; i < 5; i++) { drawNormalEyes(offs[i]); delay(speedMs(80)); }
  drawNormalEyes(0, true);  delay(speedMs(100));
  drawNormalEyes(0, false); delay(speedMs(70));
  drawNormalEyes(0, true);  delay(speedMs(70));
  drawNormalEyes(0, false);
  busy = false;
}

void animSquishEyes() {
  busy = true;
  for (uint8_t i = 0; i < 3; i++) {
    drawSquishEyes(false); delay(speedMs(160));
    drawSquishEyes(true);  delay(speedMs(100));
  }
  drawSquishEyes(false);
  busy = false;
}

void animLogoReveal() {
  busy = true;
  tft.fillScreen(animBgColor);
  for (uint16_t i = 0; i < LOGO_SEG_COUNT; i++) {
    int16_t x1 = pgm_read_word(&LOGO_SEGS[i][0]);
    int16_t y1 = pgm_read_word(&LOGO_SEGS[i][1]);
    int16_t x2 = pgm_read_word(&LOGO_SEGS[i][2]);
    int16_t y2 = pgm_read_word(&LOGO_SEGS[i][3]);
    tft.drawLine(x1, y1, x2, y2, C_WHITE);
    tft.drawLine(x1 + 1, y1, x2 + 1, y2, C_WHITE);
    if (i % 4 == 0) { server.handleClient(); delay(speedMs(8)); }
  }
  drawLogoFilled(animBgColor, C_WHITE);
  delay(1500);
  busy = false;
}

// ═════════════════════════════════════════════════════════════
//  MODE LOOP FUNCTIONS (non-blocking)
// ═════════════════════════════════════════════════════════════

void loopClaude(unsigned long now) {
  // Check for timeout → sleeping
  if (claudeState != CLAUDE_SLEEPING && claudeState != CLAUDE_IDLE) {
    if (now - claudeLastActivity > 60000) {
      claudeState = CLAUDE_SLEEPING;
      drawSleepingEyes(true);
    }
  }

  // Thinking animation: eyes look left-right cyclically
  if (claudeState == CLAUDE_THINKING) {
    if (now > animNextFrame) {
      const int16_t offs[] = {-16, 0, 16, 0};
      drawThinkingEyes(offs[claudeThinkFrame % 4]);
      drawTokenCount();
      claudeThinkFrame++;
      animNextFrame = now + 400;
    }
  }

  // DONE state: auto-return to IDLE after 3 seconds
  if (claudeState == CLAUDE_DONE) {
    if (now - claudeLastActivity > 3000) {
      claudeState = CLAUDE_IDLE;
      drawNormalEyes(0);
    }
  }

  // Sleeping: occasional Zzz
  if (claudeState == CLAUDE_SLEEPING) {
    if (now > animNextFrame) {
      drawSleepingEyes((now / 2000) % 2 == 0);
      animNextFrame = now + 2000;
    }
  }
}

void loopStandby(unsigned long now) {

  // ── IDLE / BLINK ─────────────────────────────────────────────
  // Random blinking while awake
  if (standbyState == STANDBY_IDLE || standbyState == STANDBY_BLINK) {
    // Random blink trigger
    if (standbyState == STANDBY_IDLE && now - lastBlinkTime > blinkInterval) {
      drawNormalEyes(0, true);
      standbyState = STANDBY_BLINK;
      animNextFrame = now + 150;
      lastBlinkTime = now;
      blinkInterval = random(3000, 8000);
    }
    // End blink → back to IDLE
    if (standbyState == STANDBY_BLINK && now > animNextFrame) {
      drawNormalEyes(0, false);
      standbyState = STANDBY_IDLE;
    }

    // After quote: blink for ~1 min then go sleepy
    if (standbyState == STANDBY_IDLE && idleAfterQuote > 0) {
      if (now - idleAfterQuote > IDLE_AFTER_QUOTE) {
        idleAfterQuote = 0;
        standbyState = STANDBY_SLEEPY;
        // Don't reset eyeOpenLevel — start closing from 100
      }
    }

    // Normal idle: 30s no external activity → go sleepy (not after quote)
    if (standbyState == STANDBY_IDLE && idleAfterQuote == 0 && eyeOpenLevel == 100) {
      if (now - lastActivityTime > 30000) {
        standbyState = STANDBY_SLEEPY;
      }
    }
  }

  // ── SLEEPY: gradual eye closing ──────────────────────────────
  if (standbyState == STANDBY_SLEEPY) {
    if (now > animNextFrame) {
      if (eyeOpenLevel > 0) {
        eyeOpenLevel -= 5;
        if (eyeOpenLevel < 0) eyeOpenLevel = 0;
        drawPartialEyes(eyeOpenLevel);
        animNextFrame = now + 500;
      } else {
        // Fully closed → enter ASLEEP
        standbyState = STANDBY_ASLEEP;
        asleepStartTime = now;
        drawSleepingEyes(true);
        animNextFrame = now + 2000;
      }
    }
  }

  // ── ASLEEP: Zzz animation + 15-min quote wake ────────────────
  if (standbyState == STANDBY_ASLEEP) {
    // Zzz animation
    if (now > animNextFrame) {
      drawSleepingEyes((now / 2000) % 2 == 0);
      animNextFrame = now + 2000;
    }
    // Every 15 min: wake up with a quote
    if (now - asleepStartTime > QUOTE_INTERVAL) {
      standbyState = STANDBY_QUOTE;
      drawQuote();
      animNextFrame = now + QUOTE_DURATION;
      lastQuoteTime = now;
      eyeOpenLevel = 100;  // reset for next sleep cycle
    }
  }

  // ── QUOTE: show for 1 minute ─────────────────────────────────
  if (standbyState == STANDBY_QUOTE) {
    if (now > animNextFrame) {
      // Quote done → blink for a while before sleeping again
      standbyState = STANDBY_IDLE;
      idleAfterQuote = now;
      lastBlinkTime = now;
      blinkInterval = random(3000, 8000);
      drawNormalEyes(0);
    }
  }

  // ── Time-based expression (late night → drowsy) ──────────────
  if (now - lastTimeCheck > 60000) {
    lastTimeCheck = now;
    struct tm t;
    if (getLocalTime(&t)) {
      if ((t.tm_hour >= 23 || t.tm_hour < 6)
          && standbyState == STANDBY_IDLE
          && idleAfterQuote == 0
          && eyeOpenLevel == 100) {
        standbyState = STANDBY_SLEEPY;
      }
    }
  }
}

void loopUtility(unsigned long now) {
  if (utilState == UTIL_CLOCK) {
    if (now - lastClockUpdate >= 1000) {
      drawPixelClock();
      lastClockUpdate = now;
    }
  }

  if (utilState == UTIL_POMODORO && pomodoroActive) {
    static unsigned long lastPomUpdate = 0;
    if (now - lastPomUpdate >= 1000) {
      drawPomodoro();
      lastPomUpdate = now;
    }
  }

  if (utilState == UTIL_STOPWATCH) {
    static unsigned long lastSwUpdate = 0;
    if (now - lastSwUpdate >= 1000) {
      drawStopwatch();
      lastSwUpdate = now;
    }
  }
}

// ═════════════════════════════════════════════════════════════
//  WAKE UP (from any sleep state)
// ═════════════════════════════════════════════════════════════

void wakeUp() {
  recordActivity();
  eyeOpenLevel = 100;
  sleepingAnimDone = false;
  idleAfterQuote = 0;
  asleepStartTime = 0;

  if (currentMode == MODE_CLAUDE) {
    claudeState = CLAUDE_IDLE;
    drawNormalEyes(0);
  } else if (currentMode == MODE_STANDBY) {
    standbyState = STANDBY_IDLE;
    drawNormalEyes(0);
  }
}

// ═════════════════════════════════════════════════════════════
//  WEB PAGE HTML
// ═════════════════════════════════════════════════════════════
const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Clawd Buddy</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
body{background:#1c1c20;font-family:'Courier New',monospace;color:#e8e4dc;
  display:flex;flex-direction:column;align-items:center;
  padding:20px 14px 52px;gap:14px;min-height:100vh}
.hdr{text-align:center;padding:2px 0 4px}
.mascot{font-size:15px;color:#c96a3e;line-height:1.3;font-weight:bold;
  font-family:'Courier New',monospace;display:block;letter-spacing:1px}
.sitename{font-size:10px;color:#5a5048;margin-top:8px;letter-spacing:3px}
.sec{width:100%;max-width:390px;font-size:10px;color:#8a8278;
  letter-spacing:2px;font-weight:bold;padding:0 2px}
.busy{width:100%;max-width:390px;height:2px;background:#2e2a28;
  border-radius:1px;overflow:hidden;opacity:0;transition:opacity .2s}
.busy.show{opacity:1}
.busy-i{height:100%;width:30%;background:#c96a3e;border-radius:1px;
  animation:sl 1s linear infinite}
@keyframes sl{0%{margin-left:-30%}100%{margin-left:100%}}
.ctrl{display:flex;gap:8px;width:100%;max-width:390px}
.cbtn{flex:1;background:#252428;border:1.5px solid #38343a;border-radius:10px;
  color:#b8b4ac;font-family:'Courier New',monospace;font-size:11px;font-weight:bold;
  padding:12px 4px;cursor:pointer;text-align:center;transition:all .12s}
.cbtn:active:not(:disabled){transform:scale(.94)}
.cbtn:disabled{opacity:.3;cursor:default}
.cbtn.on{border-color:#c96a3e;color:#c96a3e;background:#201408}
.cbtn.dim{border-color:#2e2a28;color:#4a4540}
.mgrid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;width:100%;max-width:390px}
.mbtn{background:#252428;border:1.5px solid #38343a;border-radius:12px;
  color:#d8d4cc;font-family:'Courier New',monospace;
  padding:14px 6px 10px;cursor:pointer;text-align:center;
  transition:all .12s;user-select:none}
.mbtn:active:not(:disabled){transform:scale(.94)}
.mbtn .ic{font-size:20px;display:block;margin-bottom:4px;line-height:1;color:#c96a3e}
.mbtn .nm{font-size:12px;font-weight:bold;color:#e8e4dc}
.mbtn .ht{font-size:9px;color:#8a8278;margin-top:3px}
.mbtn.active{border-color:#c96a3e;background:#201408}
.mbtn[data-m="1"].active{border-color:#4a8acd;background:#0c1628}
.mbtn[data-m="2"].active{border-color:#38843a;background:#0c2018}
.panel{width:100%;max-width:390px;display:none;flex-direction:column;gap:10px}
.panel.open{display:flex}
.stat-row{display:flex;gap:8px;flex-wrap:wrap}
.stat{flex:1;min-width:80px;background:#252428;border:1.5px solid #38343a;
  border-radius:9px;padding:10px;text-align:center}
.stat .vl{font-size:18px;color:#c96a3e;font-weight:bold}
.stat .lb{font-size:9px;color:#6a6058;margin-top:2px}
.speed-row{width:100%;max-width:390px;display:flex;align-items:center;gap:10px}
.sl{font-size:10px;color:#6a6058;white-space:nowrap;min-width:36px}
input[type=range]{flex:1;accent-color:#c96a3e;cursor:pointer;height:20px}
.sv{font-size:11px;color:#c96a3e;min-width:44px;text-align:right;font-weight:bold}
input[type=color]{width:100%;height:38px;border-radius:7px;border:1.5px solid #38343a;cursor:pointer;padding:0;background:none}
.db{flex:1;background:#1c1820;border:1.5px solid #38343a;border-radius:9px;
  color:#c0bab8;font-family:'Courier New',monospace;font-size:11px;
  font-weight:bold;padding:11px 4px;cursor:pointer;transition:all .12s}
.db:active{transform:scale(.95);background:#281838}
.db.hi{border-color:#c96a3e;color:#c96a3e}
.twrap{width:100%;max-width:390px;display:none;flex-direction:column;gap:8px}
.twrap.open{display:flex}
.thdr{display:flex;justify-content:space-between;align-items:center}
.tttl{font-size:11px;color:#28b878;letter-spacing:1px;font-weight:bold}
.tx{background:#0c1e12;border:2px solid #1a4828;border-radius:9px;
  color:#28b878;font-family:'Courier New',monospace;font-size:13px;
  font-weight:bold;padding:10px 18px;cursor:pointer}
.tx:active{background:#081410}
.trow{display:flex;gap:6px}
.tin{flex:1;background:#0c1018;border:1.5px solid #1a2820;border-radius:9px;
  color:#40d880;font-family:'Courier New',monospace;font-size:15px;
  padding:11px;outline:none}
.tin::placeholder{color:#2a3828}
.tgo{background:#1a9060;border:none;border-radius:9px;color:#fff;
  font-family:'Courier New',monospace;font-size:22px;font-weight:bold;
  padding:11px 16px;cursor:pointer;min-width:52px}
.tgo:active{background:#0f6040}
.toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);
  background:#252428;border:1.5px solid #38343a;border-radius:9px;
  font-size:12px;color:#d8d4cc;padding:7px 16px;opacity:0;
  transition:opacity .18s;pointer-events:none;white-space:nowrap;z-index:99}
.toast.show{opacity:1}
.wifi-section{width:100%;max-width:390px;background:#222028;border:1.5px solid #38343a;
  border-radius:12px;padding:14px;display:flex;flex-direction:column;gap:10px}
.wifi-section input{background:#0c1018;border:1.5px solid #38343a;border-radius:8px;
  color:#e8e4dc;font-family:'Courier New',monospace;font-size:13px;padding:10px;outline:none;width:100%}
.wifi-section select{background:#0c1018;border:1.5px solid #38343a;border-radius:8px;
  color:#e8e4dc;font-family:'Courier New',monospace;font-size:13px;padding:10px;width:100%}
</style>
</head>
<body>
<div class="hdr">
  <span class="mascot">&#x2590;&#x259B;&#x2588;&#x2588;&#x2588;&#x259C;&#x258C;<br>&#x259C;&#x2588;&#x2588;&#x2588;&#x2588;&#x2588;&#x259B;<br>&#x2598;&#x2598;&nbsp;&#x259D;&#x259D;</span>
  <div class="sitename">CLAWD &middot; BUDDY &middot; CONTROLLER</div>
</div>
<div class="busy" id="busy"><div class="busy-i"></div></div>

<!-- Mode Selector -->
<div class="sec">// mode</div>
<div class="mgrid">
  <button class="mbtn" data-m="0" onclick="setMode(0)">
    <span class="ic">{ }</span>
    <span class="nm">Claude</span>
    <span class="ht">hooks</span>
  </button>
  <button class="mbtn active" data-m="1" onclick="setMode(1)">
    <span class="ic">&#9632; &#9632;</span>
    <span class="nm">Standby</span>
    <span class="ht">auto</span>
  </button>
  <button class="mbtn" data-m="2" onclick="setMode(2)">
    <span class="ic">&#9201;</span>
    <span class="nm">Utility</span>
    <span class="ht">clock/timer</span>
  </button>
</div>

<!-- Claude Mode Panel -->
<div class="panel" id="pClaude">
  <div class="sec">// claude status</div>
  <div class="stat-row">
    <div class="stat"><div class="vl" id="csState">idle</div><div class="lb">state</div></div>
    <div class="stat"><div class="vl" id="csTokens">0</div><div class="lb">tokens</div></div>
    <div class="stat"><div class="vl" id="csSession">-</div><div class="lb">session</div></div>
  </div>
  <div class="sec">// claude controls</div>
  <div class="ctrl">
    <button class="db hi" onclick="testClaude('thinking')">thinking</button>
    <button class="db" onclick="testClaude('coding')">coding</button>
    <button class="db" onclick="testClaude('error')">error</button>
    <button class="db" onclick="testClaude('done')">done</button>
  </div>
</div>

<!-- Standby Mode Panel -->
<div class="panel" id="pStandby">
  <div class="sec">// standby</div>
  <div class="ctrl">
    <button class="db hi" onclick="req('/quote')">show quote</button>
    <button class="db" onclick="req('/wakeup')">wake up</button>
  </div>
</div>

<!-- Utility Mode Panel -->
<div class="panel" id="pUtil">
  <div class="sec">// utility</div>
  <div class="ctrl">
    <button class="db hi" id="clkBtn" onclick="setUtil(0)">Clock</button>
    <button class="db" id="pomBtn" onclick="setUtil(1)">Pomodoro</button>
    <button class="db" id="swBtn" onclick="setUtil(2)">Stopwatch</button>
  </div>
  <div class="sec">// pomodoro time (min)</div>
  <div class="ctrl">
    <button class="db" onclick="setPomTime(5)">5</button>
    <button class="db" onclick="setPomTime(15)">15</button>
    <button class="db hi" onclick="setPomTime(25)">25</button>
    <button class="db" onclick="setPomTime(45)">45</button>
  </div>
  <div class="sec">// pomodoro control</div>
  <div class="ctrl">
    <button class="db hi" onclick="startPom()">Start</button>
    <button class="db" onclick="req('/pomodoro?action=stop')">Stop</button>
    <button class="db" onclick="resetPom()">Reset</button>
  </div>
  <div class="sec">// stopwatch</div>
  <div class="ctrl">
    <button class="db hi" onclick="req('/stopwatch?action=start')">Start</button>
    <button class="db" onclick="req('/stopwatch?action=stop')">Stop</button>
    <button class="db" onclick="req('/stopwatch?action=reset')">Reset</button>
  </div>
</div>

<!-- Common Controls -->
<div class="sec">// controls</div>
<div class="ctrl">
  <button class="cbtn on" id="blBtn" onclick="toggleBL()">&#9728; display on</button>
</div>
<div class="speed-row">
  <span class="sl">slow</span>
  <input type="range" id="spd" min="1" max="3" value="1" step="1" oninput="setSpeed(this.value)">
  <span class="sv" id="spdV">slow</span>
</div>
<div class="sec">// background color</div>
<input type="color" id="bgCol" value="#aa4818" oninput="onBgChange(this.value)">

<!-- Terminal (for Claude mode) -->
<div class="twrap" id="twrap">
  <div class="thdr">
    <span class="tttl">&#9658; clawd:~$</span>
    <button class="tx" onclick="closeTerm()">&#x2715; exit</button>
  </div>
  <div class="trow">
    <input class="tin" id="tin" type="text" placeholder="type here..."
           autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false">
    <button class="tgo" onclick="termEnter()">&#8629;</button>
  </div>
</div>

<div class="toast" id="toast"></div>
<script>
let currentMode=1,blOn=true,isBusy=false;
const spdLabels=['','slow','normal','fast'];
const modePanels=['pClaude','pStandby','pUtil'];
function toast(m,ok=true){const e=document.getElementById('toast');e.textContent=m;
e.style.borderColor=ok?'#28b878':'#c96a3e';e.classList.add('show');
clearTimeout(window._tt);window._tt=setTimeout(()=>e.classList.remove('show'),1300);}
function setBusy(b){isBusy=b;document.getElementById('busy').classList.toggle('show',b);}
async function req(p){try{const r=await fetch(p);return r.ok;}catch(e){toast('no conn',false);return false;}}
async function setMode(m){
  if(isBusy)return;
  await req('/mode?m='+m);
  currentMode=m;
  document.querySelectorAll('.mbtn').forEach(b=>b.classList.toggle('active',parseInt(b.dataset.m)===m));
  modePanels.forEach((id,i)=>{
    document.getElementById(id).classList.toggle('open',i===m);
  });
  // Show terminal section only in Claude mode
  document.getElementById('twrap').classList.toggle('open',m===0);
}
async function setUtil(s){
  await req('/util?s='+s);
  document.getElementById('clkBtn').classList.toggle('hi',s===0);
  document.getElementById('pomBtn').classList.toggle('hi',s===1);
  document.getElementById('swBtn').classList.toggle('hi',s===2);
}
async function setSpeed(v){
  document.getElementById('spdV').textContent=spdLabels[v];
  await req('/speed?v='+v);
}
async function toggleBL(){
  blOn=!blOn;
  await req('/backlight?on='+(blOn?1:0));
  const b=document.getElementById('blBtn');
  b.textContent=blOn?'☀ display on':'○ display off';
  b.classList.toggle('on',blOn);b.classList.toggle('dim',!blOn);
}
async function onBgChange(hex){
  await req('/redraw?bg='+encodeURIComponent(hex));
}
async function testClaude(state){
  await req('/status?state='+state+'&tokens='+(Math.floor(Math.random()*15000)+500));
  toast('state: '+state);
}
// Pomodoro
let pomMins=25;
function setPomTime(m){
  pomMins=m;
  document.querySelectorAll('#pUtil .ctrl:first-of-type .db').forEach(b=>{
    b.classList.toggle('hi',parseInt(b.textContent)===m);
  });
  toast('Timer: '+m+' min');
}
async function startPom(){
  await req('/pomodoro?action=start&minutes='+pomMins);
  toast('Started '+pomMins+'min');
}
async function resetPom(){
  await req('/pomodoro?action=reset');
  toast('Reset');
}
// Terminal
const tin=document.getElementById('tin');
let lastVal='';
tin.addEventListener('input',async()=>{
  const cur=tin.value,prev=lastVal;
  if(cur.length>prev.length)await req('/char?c='+encodeURIComponent(cur[cur.length-1]));
  else if(cur.length<prev.length)await req('/char?c=%08');
  lastVal=cur;
});
async function termEnter(){await req('/char?c=%0A');tin.value='';lastVal='';tin.focus();}
tin.addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();termEnter();}});
async function closeTerm(){await req('/cmd?k=q');toast('terminal closed');}
// Init
(async()=>{
  try{
    const r=await fetch('/state');const j=await r.json();
    document.getElementById('spd').value=j.speed||1;
    document.getElementById('spdV').textContent=spdLabels[j.speed||1];
    if(j.bl===false){blOn=false;const b=document.getElementById('blBtn');
      b.textContent='○ display off';b.classList.remove('on');b.classList.add('dim');}
    if(j.mode!==undefined){await setMode(j.mode);}
  }catch(e){}
})();
</script>
</body>
</html>
)rawhtml";

// ═════════════════════════════════════════════════════════════
//  WEB ROUTES
// ═════════════════════════════════════════════════════════════

void routeRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.send_P(200, "text/html", INDEX_HTML);
}

// /status?state=<s>&tokens=<n> — Claude Code hooks endpoint
void routeStatus() {
  if (!server.hasArg("state")) {
    server.send(400, "application/json", "{\"e\":1}"); return;
  }

  String state = server.arg("state");
  recordActivity();

  // Wake up if sleeping
  if (claudeState == CLAUDE_SLEEPING || currentMode == MODE_STANDBY) {
    // Don't auto-switch mode, just note activity
  }

  // Update token count
  if (server.hasArg("tokens")) {
    tokenCount = server.arg("tokens").toInt();
  }

  // Map state string to internal state
  if (state == "thinking") {
    claudeState = CLAUDE_THINKING;
    claudeThinkFrame = 0;
    drawThinkingEyes(0);
    drawTokenCount();
  } else if (state == "coding") {
    claudeState = CLAUDE_CODING;
    drawCodingEyes();
    drawTokenCount();
  } else if (state == "error") {
    claudeState = CLAUDE_ERROR;
    drawErrorEyes();
    drawTokenCount();
  } else if (state == "done") {
    claudeState = CLAUDE_DONE;
    drawDoneEyes();
    drawTokenCount();
  } else if (state == "sleeping") {
    claudeState = CLAUDE_SLEEPING;
    drawSleepingEyes(true);
  } else if (state == "idle") {
    claudeState = CLAUDE_IDLE;
    drawNormalEyes(0);
    drawTokenCount();
  }

  claudeLastActivity = millis();
  animNextFrame = millis();

  server.send(200, "application/json", "{\"ok\":1}");
}

// /mode?m=0/1/2
void routeMode() {
  if (!server.hasArg("m")) {
    server.send(400, "application/json", "{\"e\":1}"); return;
  }

  uint8_t newMode = server.arg("m").toInt();
  if (newMode > 2) newMode = 2;

  currentMode = newMode;
  recordActivity();

  // Redraw appropriate view
  switch (currentMode) {
    case MODE_CLAUDE:
      drawNormalEyes(0);
      drawTokenCount();
      break;
    case MODE_STANDBY:
      eyeOpenLevel = 100;
      standbyState = STANDBY_IDLE;
      drawNormalEyes(0);
      break;
    case MODE_UTILITY:
      if (utilState == UTIL_CLOCK) drawPixelClock();
      else if (utilState == UTIL_POMODORO) drawPomodoro();
      else if (utilState == UTIL_STOPWATCH) drawStopwatch();
      break;
  }

  server.send(200, "application/json", "{\"ok\":1}");
}

// /util?s=0/1/2 (0=clock, 1=pomodoro, 2=stopwatch)
void routeUtil() {
  if (server.hasArg("s")) {
    utilState = server.arg("s").toInt();
    if (utilState > 2) utilState = 2;
  }
  if (currentMode == MODE_UTILITY) {
    if (utilState == UTIL_CLOCK) drawPixelClock();
    else if (utilState == UTIL_POMODORO && pomodoroActive) drawPomodoro();
    else if (utilState == UTIL_STOPWATCH) drawStopwatch();
    else {
      tft.fillScreen(animBgColor);
      tft.setTextColor(C_MUTED); tft.setTextSize(2);
      tft.setCursor(30, DISP_H / 2 - 12);
      tft.print("Press Start");
    }
  }
  server.send(200, "application/json", "{\"ok\":1}");
}

// /pomodoro?action=start/stop/reset&minutes=25
void routePomodoro() {
  if (!server.hasArg("action")) {
    server.send(400, "application/json", "{\"e\":1}"); return;
  }
  String action = server.arg("action");

  // 自定义时间
  if (server.hasArg("minutes")) {
    int mins = server.arg("minutes").toInt();
    if (mins > 0 && mins <= 120) {
      pomodoroWorkMs = mins * 60000UL;
    }
  }

  if (action == "start") {
    pomodoroActive = true;
    pomodoroBreak = false;
    pomodoroStart = millis();
    utilState = UTIL_POMODORO;
    currentMode = MODE_UTILITY;
  } else if (action == "stop") {
    pomodoroActive = false;
    if (currentMode == MODE_UTILITY && utilState == UTIL_POMODORO) {
      drawPomodoro();  // draw final state
      tft.setTextColor(C_YELLOW); tft.setTextSize(2);
      tft.setCursor((DISP_W - 84) / 2, DISP_H / 2 + 40);
      tft.print("PAUSED");
    }
  } else if (action == "reset") {
    pomodoroActive = false;
    pomodoroBreak = false;
    pomodoroStart = 0;
    // 重置后刷新屏幕
    if (currentMode == MODE_UTILITY && utilState == UTIL_POMODORO) {
      tft.fillScreen(animBgColor);
      tft.setTextColor(C_MUTED); tft.setTextSize(2);
      tft.setCursor(30, DISP_H / 2 - 12);
      tft.print("Ready");
      // Force full redraw next time
      extern void drawPomodoro();
    }
  }

  server.send(200, "application/json", "{\"ok\":1}");
}

// /quote — show a random quote
void routeQuote() {
  if (currentMode == MODE_STANDBY) {
    standbyState = STANDBY_QUOTE;
    drawQuote();
    animNextFrame = millis() + 5000;
    lastQuoteTime = millis();
  }
  server.send(200, "application/json", "{\"ok\":1}");
}

// /stopwatch?action=start/stop/reset
void routeStopwatch() {
  if (!server.hasArg("action")) {
    server.send(400, "application/json", "{\"e\":1}"); return;
  }
  String action = server.arg("action");

  if (action == "start") {
    if (!stopwatchRunning) {
      stopwatchStart = millis();
      stopwatchRunning = true;
    }
    utilState = UTIL_STOPWATCH;
    currentMode = MODE_UTILITY;
  } else if (action == "stop") {
    if (stopwatchRunning) {
      stopwatchElapsed += millis() - stopwatchStart;
      stopwatchRunning = false;
    }
  } else if (action == "reset") {
    stopwatchRunning = false;
    stopwatchStart = 0;
    stopwatchElapsed = 0;
    if (currentMode == MODE_UTILITY && utilState == UTIL_STOPWATCH) {
      drawStopwatch();
    }
  }

  server.send(200, "application/json", "{\"ok\":1}");
}

// /wakeup — wake from sleep
void routeWakeup() {
  wakeUp();
  server.send(200, "application/json", "{\"ok\":1}");
}

// Legacy route: /cmd?k=<char>
void routeCmd() {
  if (!server.hasArg("k") || server.arg("k").isEmpty()) {
    server.send(400, "application/json", "{\"e\":1}"); return;
  }
  const char c = server.arg("k")[0];

  if (termMode) {
    if (c == 'q') { termMode = false; drawCodeView(); }
    server.send(200, "application/json", "{\"ok\":1}"); return;
  }

  server.send(200, "application/json", "{\"ok\":1}");
  switch (c) {
    case 'w': currentMode = MODE_STANDBY; standbyState = STANDBY_IDLE; eyeOpenLevel = 100; drawNormalEyes(0); break;
    case 's': drawSquishEyes(); break;
    case 'd':
      drawCodeView();
      termMode = true; termClear(); termFullRedraw(); break;
    case 'a': animLogoReveal(); break;
  }
  recordActivity();
}

// /char?c=<char> — terminal input
void routeChar() {
  if (!termMode) { server.send(200, "application/json", "{\"ok\":1}"); return; }
  const String val = server.arg("c");
  if (val.length() > 0) termAddChar(val[0]);
  server.send(200, "application/json", "{\"ok\":1}");
}

// /speed?v=1-3
void routeSpeed() {
  if (server.hasArg("v")) animSpeed = constrain(server.arg("v").toInt(), 1, 3);
  server.send(200, "application/json", "{\"ok\":1}");
}

// /redraw?bg=hex
void routeRedraw() {
  if (server.hasArg("bg")) {
    animBgColor = hexToRgb565(server.arg("bg"));
    drawBgColor = animBgColor;
  }
  // Redraw current view with new color
  switch (currentMode) {
    case MODE_CLAUDE:
      drawNormalEyes(0);
      drawTokenCount();
      break;
    case MODE_STANDBY:
      drawNormalEyes(0);
      break;
    case MODE_UTILITY:
      if (utilState == UTIL_CLOCK) drawPixelClock();
      break;
  }
  server.send(200, "application/json", "{\"ok\":1}");
}

// /backlight?on=0/1
void routeBacklight() {
  setBacklight(server.hasArg("on") && server.arg("on") == "1");
  server.send(200, "application/json", "{\"ok\":1}");
}

// /state — return current state as JSON
void routeState() {
  String j = "{";
  j += "\"mode\":";    j += currentMode;
  j += ",\"busy\":";   j += busy        ? "true" : "false";
  j += ",\"term\":";   j += termMode    ? "true" : "false";
  j += ",\"bl\":";     j += backlightOn ? "true" : "false";
  j += ",\"speed\":";  j += animSpeed;
  j += ",\"staConnected\":"; j += staConnected ? "true" : "false";
  j += ",\"ntpSynced\":";    j += ntpSynced    ? "true" : "false";
  j += ",\"claudeState\":";  j += claudeState;
  j += ",\"tokens\":";       j += tokenCount;
  j += ",\"pomodoroActive\":"; j += pomodoroActive ? "true" : "false";
  if (ntpSynced) {
    struct tm t;
    if (getLocalTime(&t)) {
      char buf[24];
      snprintf(buf, sizeof(buf), ",\"time\":\"%02d:%02d:%02d\"", t.tm_hour, t.tm_min, t.tm_sec);
      j += buf;
    }
  }
  j += "}";
  server.send(200, "application/json", j);
}

// /wifi — serve WiFi config page
const char WIFI_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>WiFi Config</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#1c1c20;font-family:'Courier New',monospace;color:#e8e4dc;
  display:flex;flex-direction:column;align-items:center;padding:20px;gap:14px}
h2{color:#c96a3e;font-size:18px}
.box{width:100%;max-width:360px;background:#222028;border:1.5px solid #38343a;
  border-radius:12px;padding:16px;display:flex;flex-direction:column;gap:10px}
label{font-size:11px;color:#8a8278;letter-spacing:1px;font-weight:bold}
input,select{background:#0c1018;border:1.5px solid #38343a;border-radius:8px;
  color:#e8e4dc;font-family:'Courier New',monospace;font-size:14px;padding:11px;outline:none;width:100%}
button{background:#252428;border:1.5px solid #c96a3e;border-radius:9px;
  color:#c96a3e;font-family:'Courier New',monospace;font-size:13px;font-weight:bold;
  padding:12px;cursor:pointer;width:100%}
button:active{transform:scale(.96)}
.ok{border-color:#28b878;color:#28b878}
.status{font-size:12px;color:#8a8278;padding:8px;text-align:center}
a{color:#c96a3e;text-decoration:none}
</style></head><body>
<h2>WiFi Config</h2>
<div class="status" id="st">Checking...</div>
<div class="box">
  <label>Available Networks</label>
  <button onclick="doScan()">Scan WiFi</button>
  <select id="scanList" onchange="document.getElementById('ssid').value=this.value"></select>
  <label>SSID</label>
  <input id="ssid" placeholder="WiFi name">
  <label>Password</label>
  <input id="pass" type="password" placeholder="WiFi password">
  <button class="ok" onclick="doSave()">Save & Connect</button>
</div>
<div class="box">
  <button onclick="doDisconnect()" style="border-color:#c96a3e;color:#c96a3e">Disconnect WiFi</button>
  <button onclick="doReset()" style="border-color:#ff4444;color:#ff4444">Reset All WiFi</button>
</div>
<p><a href="/">Back to Controller</a></p>
<script>
async function getStatus(){
  try{const r=await fetch('/wifi/status');const j=await r.json();
    document.getElementById('st').textContent=
      j.connected?'Connected: '+j.ssid+' ('+j.ip+')':'Not connected';
  }catch(e){document.getElementById('st').textContent='Error';}
}
async function doScan(){
  document.getElementById('st').textContent='Scanning...';
  try{const r=await fetch('/wifi/scan');const j=await r.json();
    const sel=document.getElementById('scanList');sel.innerHTML='';
    j.networks.forEach(n=>{const o=document.createElement('option');o.value=n;o.textContent=n;sel.appendChild(o);});
    document.getElementById('st').textContent='Found '+j.networks.length+' networks';
  }catch(e){document.getElementById('st').textContent='Scan failed';}
}
async function doSave(){
  const ssid=document.getElementById('ssid').value;
  const pass=document.getElementById('pass').value;
  if(!ssid){alert('Enter SSID');return;}
  document.getElementById('st').textContent='Saving...';
  try{const r=await fetch('/wifi/save?ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass));
    const j=await r.json();
    document.getElementById('st').textContent=j.ok?'Saved! Reconnecting...':'Save failed';
  }catch(e){document.getElementById('st').textContent='Save failed';}
}
async function doDisconnect(){
  try{await fetch('/wifi/disconnect');getStatus();}catch(e){}
}
async function doReset(){
  if(!confirm('Reset all WiFi settings?'))return;
  try{await fetch('/wifi/reset');getStatus();}catch(e){}
}
getStatus();setInterval(getStatus,5000);
</script></body></html>
)rawhtml";

void routeWifiPage() {
  server.send_P(200, "text/html", WIFI_HTML);
}

// /wifi/status
void routeWifiStatus() {
  String j = "{";
  j += "\"connected\":"; j += staConnected ? "true" : "false";
  if (staConnected) {
    j += ",\"ssid\":\""; j += WiFi.SSID(); j += "\"";
    j += ",\"ip\":\""; j += WiFi.localIP().toString(); j += "\"";
  }
  j += "}";
  server.send(200, "application/json", j);
}

// /wifi/scan
void routeWifiScan() {
  int n = WiFi.scanNetworks();
  String j = "{\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i > 0) j += ",";
    j += "\"";
    j += WiFi.SSID(i);
    j += "\"";
  }
  j += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json", j);
}

// /wifi/save?ssid=...&pass=...
void routeWifiSave() {
  if (!server.hasArg("ssid")) {
    server.send(400, "application/json", "{\"e\":1}"); return;
  }
  String newSSID = server.arg("ssid");
  String newPass = server.hasArg("pass") ? server.arg("pass") : "";

  // Save to flash
  preferences.begin("wifi", false);
  preferences.putString("ssid", newSSID);
  preferences.putString("pass", newPass);
  preferences.end();

  // Try to connect
  WiFi.disconnect();
  WiFi.begin(newSSID.c_str(), newPass.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(100);
  }

  staConnected = (WiFi.status() == WL_CONNECTED);

  if (staConnected) {
    configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
    ntpSynced = true;
  }

  server.send(200, "application/json", staConnected ? "{\"ok\":1}" : "{\"ok\":0,\"msg\":\"connect failed\"}");
}

// /wifi/disconnect
void routeWifiDisconnect() {
  WiFi.disconnect();
  staConnected = false;
  ntpSynced = false;
  server.send(200, "application/json", "{\"ok\":1}");
}

// /wifi/reset
void routeWifiReset() {
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();
  WiFi.disconnect();
  staConnected = false;
  ntpSynced = false;
  server.send(200, "application/json", "{\"ok\":1}");
}

// Legacy routes (keep compatibility)
void routeCanvas() {
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeDrawClear() {
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeDrawStroke() {
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeNotFound() { server.send(404, "text/plain", "not found"); }

// ═════════════════════════════════════════════════════════════
//  WIFI INITIALIZATION
// ═════════════════════════════════════════════════════════════

void initWiFi() {
  // Read saved WiFi config
  preferences.begin("wifi", true);
  staSSID = preferences.getString("ssid", "");
  staPass = preferences.getString("pass", "");
  preferences.end();

  // Start AP mode (hotspot)
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP started: ");
  Serial.println(AP_SSID);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // If saved credentials, try STA connection
  if (staSSID.length() > 0) {
    Serial.print("Connecting to ");
    Serial.println(staSSID);
    WiFi.begin(staSSID.c_str(), staPass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();

    staConnected = (WiFi.status() == WL_CONNECTED);
    if (staConnected) {
      Serial.print("STA connected! IP: ");
      Serial.println(WiFi.localIP());
      configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
      ntpSynced = true;
    } else {
      Serial.println("STA connection failed");
    }
  }
}

// ═════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(10);

  // ── WiFi first (before SPI) ───────────────────────────────
  initWiFi();

  // ── Display ────────────────────────────────────────────────
  pinMode(TFT_BLK, OUTPUT);
  setBacklight(true);

  SPI.begin(8, -1, 10, TFT_CS);
  tft.init(240, 240);
  tft.setSPISpeed(40000000);
  tft.setRotation(1);
  initColours();

  // Random seed for quotes
  randomSeed(analogRead(0));

  // ── Boot splash ────────────────────────────────────────────
  tft.fillScreen(animBgColor);
  tft.setTextColor(C_WHITE); tft.setTextSize(3);
  tft.setCursor(DISP_W / 2 - 54, DISP_H / 2 - 22); tft.print("Clawd");
  tft.setCursor(DISP_W / 2 - 54, DISP_H / 2 + 14); tft.print("Buddy");
  delay(1200);

  // ── Logo at startup ────────────────────────────────────────
  animLogoReveal();

  // ── WiFi info screen ───────────────────────────────────────
  tft.fillScreen(animBgColor);
  tft.fillRect(0, 0, DISP_W, 4, C_ORANGE);

  tft.setTextColor(C_GREEN);  tft.setTextSize(2);
  tft.setCursor(12, 16);  tft.print("Hotspot Ready!");

  tft.setTextColor(C_WHITE);  tft.setTextSize(1);
  tft.setCursor(12, 50);  tft.print("WiFi:");
  tft.setTextColor(C_ORANGE);  tft.setTextSize(2);
  tft.setCursor(12, 65);  tft.print(AP_SSID);

  tft.setTextColor(C_WHITE);  tft.setTextSize(1);
  tft.setCursor(12, 95);  tft.print("Password:");
  tft.setTextColor(C_ORANGE);  tft.setTextSize(2);
  tft.setCursor(12, 110);  tft.print(AP_PASS);

  tft.setTextColor(C_WHITE);  tft.setTextSize(1);
  tft.setCursor(12, 145);  tft.print("Open in browser:");
  tft.setTextColor(C_ORANGE);  tft.setTextSize(2);
  tft.setCursor(12, 160);  tft.print("192.168.4.1");

  if (staConnected) {
    tft.setTextColor(C_GREEN);  tft.setTextSize(1);
    tft.setCursor(12, 195);  tft.print("Also on home WiFi:");
    tft.print(WiFi.localIP().toString());
  }

  delay(5000);

  // ── Register routes ────────────────────────────────────────
  server.on("/",              HTTP_GET, routeRoot);
  server.on("/status",        HTTP_GET, routeStatus);
  server.on("/mode",          HTTP_GET, routeMode);
  server.on("/util",          HTTP_GET, routeUtil);
  server.on("/pomodoro",      HTTP_GET, routePomodoro);
  server.on("/stopwatch",     HTTP_GET, routeStopwatch);
  server.on("/quote",         HTTP_GET, routeQuote);
  server.on("/wakeup",        HTTP_GET, routeWakeup);
  server.on("/cmd",           HTTP_GET, routeCmd);
  server.on("/char",          HTTP_GET, routeChar);
  server.on("/speed",         HTTP_GET, routeSpeed);
  server.on("/redraw",        HTTP_GET, routeRedraw);
  server.on("/backlight",     HTTP_GET, routeBacklight);
  server.on("/state",         HTTP_GET, routeState);
  server.on("/canvas",        HTTP_GET, routeCanvas);
  server.on("/draw/clear",    HTTP_GET, routeDrawClear);
  server.on("/draw/stroke",   HTTP_GET, routeDrawStroke);
  // WiFi config routes
  server.on("/wifi",            HTTP_GET, routeWifiPage);
  server.on("/wifi/status",     HTTP_GET, routeWifiStatus);
  server.on("/wifi/scan",       HTTP_GET, routeWifiScan);
  server.on("/wifi/save",       HTTP_GET, routeWifiSave);
  server.on("/wifi/disconnect", HTTP_GET, routeWifiDisconnect);
  server.on("/wifi/reset",      HTTP_GET, routeWifiReset);
  server.onNotFound(routeNotFound);
  server.begin();

  // Initialize timers
  lastBlinkTime = millis();
  lastQuoteTime = millis();
  lastActivityTime = millis();
  lastTimeCheck = millis();
  lastClockUpdate = 0;
  animNextFrame = millis();

  // Default to standby mode
  currentMode = MODE_STANDBY;
  drawNormalEyes(0);
}

// ═════════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════════

void loop() {
  server.handleClient();

  unsigned long now = millis();

  // Check WiFi connection status periodically
  static unsigned long lastWifiCheck = 0;
  if (now - lastWifiCheck > 10000) {
    lastWifiCheck = now;
    bool wasConnected = staConnected;
    staConnected = (WiFi.status() == WL_CONNECTED);
    if (!wasConnected && staConnected) {
      configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
      ntpSynced = true;
    }
  }

  // Mode-specific loop
  switch (currentMode) {
    case MODE_CLAUDE:
      loopClaude(now);
      break;
    case MODE_STANDBY:
      loopStandby(now);
      break;
    case MODE_UTILITY:
      loopUtility(now);
      break;
  }

  // Pomodoro overtime check (convert to break)
  if (pomodoroActive && !pomodoroBreak) {
    if (millis() - pomodoroStart >= pomodoroWorkMs + POMODORO_BLINK) {
      pomodoroBreak = true;
      pomodoroStart = millis();
    }
  }
  if (pomodoroActive && pomodoroBreak) {
    if (millis() - pomodoroStart >= POMODORO_BREAK + POMODORO_BLINK) {
      pomodoroActive = false;
      pomodoroBreak = false;
    }
  }
}
