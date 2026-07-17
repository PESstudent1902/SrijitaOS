/*
  OS_for_Srijita.ino  —  v2
  --------------------------
  UI Engine: Media Player, BT Command Parser, Pagination, Gallery placeholder.
  Clock + Countdown fully synced via Bluetooth (no WiFi needed).
  Works with your existing config.h unchanged.
*/

#include <TFT_eSPI.h>
#include <SPI.h>
#include "config.h"
#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` and enable it
#endif

TFT_eSPI tft = TFT_eSPI();

// ------------------------------------------------------------
// BLUETOOTH
// ------------------------------------------------------------
BluetoothSerial SerialBT;
String btMessage   = "Waiting for a message...";
String currentSong = "No media playing";
String btLineBuf   = "";

volatile bool btConnected   = false;
volatile bool btJustOpened  = false;   // flag set in callback, handled in loop

// ---- BT-synced time -----------------------------------------
bool timeSynced = false;
int  syncH = 0, syncM = 0, syncS = 0;
unsigned long syncMillis = 0;

bool dateSynced = false;
int  curDay = 1, curMon = 1, curYear = 2026;

// ------------------------------------------------------------
// GLOBAL APP STATES
// ------------------------------------------------------------
int  noteIndex     = 0;
int  quoteIndex    = 0;
int  backlightDuty = 255;
bool isPlaying     = true;
#define TFT_BL_PIN 21

// ------------------------------------------------------------
// TOUCH — CYD 2.8" secondary SPI bus
// ------------------------------------------------------------
#include <XPT2046_Touchscreen.h>

#define TOUCH_CLK  25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32
#define TOUCH_CS   33
#define TOUCH_MIN_PRESSURE 200

SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touchCtrl(TOUCH_CS);

bool getTouchRaw(int &x, int &y) {
  if (!touchCtrl.touched()) return false;
  TS_Point p = touchCtrl.getPoint();
  if (p.z < TOUCH_MIN_PRESSURE) return false;   // reject ghost touches

  x = map(p.y, 3719, 485, 0, 239);
  y = map(p.x, 304, 3658, 0, 319);
  x = constrain(x, 0, 239);
  y = constrain(y, 0, 319);
  return true;
}

// ------------------------------------------------------------
// SCREEN MANAGER
// ------------------------------------------------------------
enum Screen {
  SCR_HOME, SCR_CLOCK, SCR_NOTES, SCR_COUNTDOWN,
  SCR_QUOTES, SCR_MEDIA, SCR_SETTINGS, SCR_MESSAGES, SCR_GALLERY
};

Screen currentScreen = SCR_HOME;
bool needsRedraw = true;
int  homePage    = 0;

// forward decls
void renderHome();     void renderClock();    void renderNotes();
void renderCountdown();void renderQuotes();   void renderMedia();
void renderSettings(); void renderMessages(); void renderGallery();
void handleTap(int x, int y);
void handleSwipe(int dir);
void bootAnimation();
void drawHeart(int x, int y, int size, uint16_t color);
void buttonClickFeedback(int x, int y, int w, int h);
int  daysInMonth(int month, int year);
void processBtCommand(String cmd);
void drawClockTime();
void drawBrightnessBar();

void goHome() {
  currentScreen = SCR_HOME;
  homePage = 0;
  needsRedraw = true;
}

void drawBackButton() {
  tft.fillRoundRect(8, 8, 60, 32, 8, COL_BACK_BTN);
  tft.setTextColor(COL_TEXT, COL_BACK_BTN);
  tft.setTextSize(1);
  tft.setCursor(18, 19);
  tft.print("< Back");
}

bool tappedBackButton(int x, int y) {
  if (x >= 8 && x <= 68 && y >= 8 && y <= 40) {
    buttonClickFeedback(8, 8, 60, 32);
    return true;
  }
  return false;
}

void buttonClickFeedback(int x, int y, int w, int h) {
  tft.drawRoundRect(x, y, w, h, 8, 0xFFFF);
  tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 7, 0xFFFF);
  delay(70);
  needsRedraw = true;   // full redraw clears the highlight
}

// ------------------------------------------------------------
// BT CONNECTION CALLBACK  (instant status + auto sync request)
// ------------------------------------------------------------
void btCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (event == ESP_SPP_SRV_OPEN_EVT) {
    btConnected  = true;
    btJustOpened = true;      // handled in loop (don't send from BT task)
    needsRedraw  = true;
  } else if (event == ESP_SPP_CLOSE_EVT) {
    btConnected = false;
    needsRedraw = true;
  }
}

// ------------------------------------------------------------
// SETUP
// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Start BT FIRST so the phone can already see/connect to it
  // during the boot animation. SSP (no PIN) pairs fastest on Android —
  // do NOT force a legacy PIN, it slows pairing down.
  SerialBT.register_callback(btCallback);
  SerialBT.begin("Srijita_OS");

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(COL_BG);

  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touchCtrl.begin(touchSpi);
  touchCtrl.setRotation(0);

  pinMode(TFT_BL_PIN, OUTPUT);
  analogWrite(TFT_BL_PIN, backlightDuty);

  bootAnimation();
  needsRedraw = true;
}

// ------------------------------------------------------------
// BT COMMAND PARSER (non-blocking, char-buffered)
// ------------------------------------------------------------
void processBtCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd.startsWith("TIME:")) {
    // Accepts TIME:HH:MM or TIME:HH:MM:SS
    String t = cmd.substring(5);
    syncH = t.substring(0, 2).toInt();
    syncM = t.substring(3, 5).toInt();
    syncS = (t.length() >= 8) ? t.substring(6, 8).toInt() : 0;
    syncMillis = millis();
    timeSynced = true;
    if (currentScreen == SCR_CLOCK) needsRedraw = true;
  }
  else if (cmd.startsWith("DATE:")) {
    // Accepts DATE:DD/MM/YYYY
    String d = cmd.substring(5);
    curDay  = d.substring(0, 2).toInt();
    curMon  = d.substring(3, 5).toInt();
    curYear = d.substring(6).toInt();
    dateSynced = true;
    if (currentScreen == SCR_COUNTDOWN || currentScreen == SCR_CLOCK)
      needsRedraw = true;
  }
  else if (cmd.startsWith("MEDIA:")) {
    currentSong = cmd.substring(6);
    if (currentSong.length() > 34) currentSong = currentSong.substring(0, 31) + "...";
    isPlaying = true;
    currentScreen = SCR_MEDIA;
    needsRedraw = true;
  }
  else if (cmd.startsWith("MSG:")) {
    btMessage = cmd.substring(4);
    if (currentScreen == SCR_MESSAGES) needsRedraw = true;
  }
  else {
    btMessage = cmd;
    if (currentScreen == SCR_MESSAGES) needsRedraw = true;
  }
}

// ---- current time from sync + millis(), with day rollover ----
void getCurrentTime(int &h, int &m, int &s) {
  unsigned long elapsed = (millis() - syncMillis) / 1000UL;
  unsigned long total   = (unsigned long)syncH * 3600UL
                        + (unsigned long)syncM * 60UL
                        + (unsigned long)syncS + elapsed;

  unsigned long daysPassed = total / 86400UL;
  total %= 86400UL;
  h = total / 3600; m = (total / 60) % 60; s = total % 60;

  // roll the synced date forward if we've crossed midnight(s)
  while (daysPassed > 0 && dateSynced) {
    curDay++;
    if (curDay > daysInMonth(curMon, curYear)) {
      curDay = 1; curMon++;
      if (curMon > 12) { curMon = 1; curYear++; }
    }
    daysPassed--;
    syncMillis += 86400000UL;  // keep math anchored so we only roll once
  }
}

// ------------------------------------------------------------
// LOOP
// ------------------------------------------------------------
void loop() {
  // --- BT: non-blocking read ---
  while (SerialBT.available()) {
    char c = SerialBT.read();
    if (c == '\n')       { processBtCommand(btLineBuf); btLineBuf = ""; }
    else if (c != '\r')  { btLineBuf += c; if (btLineBuf.length() > 200) btLineBuf = ""; }
  }

  // --- On fresh connection, ask companion for a sync ---
  if (btJustOpened) {
    btJustOpened = false;
    SerialBT.println("REQ:SYNC");   // companion replies TIME: + DATE:
  }

  // --- Redraw ---
  if (needsRedraw) {
    switch (currentScreen) {
      case SCR_HOME:      renderHome();      break;
      case SCR_CLOCK:     renderClock();     break;
      case SCR_NOTES:     renderNotes();     break;
      case SCR_COUNTDOWN: renderCountdown(); break;
      case SCR_QUOTES:    renderQuotes();    break;
      case SCR_MEDIA:     renderMedia();     break;
      case SCR_SETTINGS:  renderSettings();  break;
      case SCR_MESSAGES:  renderMessages();  break;
      case SCR_GALLERY:   renderGallery();   break;
    }
    needsRedraw = false;
  }

  // --- Touch handling ---
  static int  lastX = 0, lastY = 0, startX = 0, startY = 0;
  static bool wasTouched = false;
  static unsigned long touchStartMs = 0;

  int x, y;
  bool isTouched = getTouchRaw(x, y);

  if (isTouched) {
    lastX = x; lastY = y;
    if (!wasTouched) {
      wasTouched = true;
      startX = x; startY = y;
      touchStartMs = millis();
    }
    // Live brightness drag on Settings
    if (currentScreen == SCR_SETTINGS && y >= 150 && y <= 200) {
      int duty = constrain(map(x, 30, 210, 0, 255), 0, 255);
      if (abs(duty - backlightDuty) > 3) {
        backlightDuty = duty;
        analogWrite(TFT_BL_PIN, backlightDuty);
        drawBrightnessBar();   // partial redraw, no flicker
      }
    }
  } else if (wasTouched) {
    wasTouched = false;
    int dx = lastX - startX;
    int dy = lastY - startY;

    if (abs(dx) > 25 || abs(dy) > 25) {
      if (abs(dx) > abs(dy)) handleSwipe(dx > 0 ? 1 : -1);
      else                   handleSwipe(dy > 0 ? 2 : -2);
    }
    else if (millis() - touchStartMs > 20 && millis() - touchStartMs < 800) {
      handleTap(lastX, lastY);
    }
  }

  // --- 1-second tick: live clock update ---
  static unsigned long lastTick = 0;
  if (millis() - lastTick >= 1000) {
    lastTick = millis();
    if (currentScreen == SCR_CLOCK && timeSynced) drawClockTime();
  }
}

// ==============================================================
//  GESTURES
// ==============================================================
void handleSwipe(int dir) {
  if (currentScreen == SCR_HOME) {
    // up/down OR left/right flips the page
    homePage = (homePage == 0) ? 1 : 0;
    needsRedraw = true;
  }
  else if (currentScreen == SCR_NOTES) {
    if (dir == -1)     noteIndex = (noteIndex + 1) % LOVE_NOTES_COUNT;
    else if (dir == 1) noteIndex = (noteIndex - 1 + LOVE_NOTES_COUNT) % LOVE_NOTES_COUNT;
    needsRedraw = true;
  }
  else if (currentScreen == SCR_QUOTES) {
    if (dir == -1 || dir == 1) {
      int next;
      do { next = random(QUOTES_COUNT); } while (next == quoteIndex && QUOTES_COUNT > 1);
      quoteIndex = next;
      needsRedraw = true;
    }
  }
}

// ==============================================================
//  HOME SCREEN APP GRID
// ==============================================================
struct AppIcon { const char* label; uint16_t color; };

AppIcon apps[] = {
  { "Clock",      0x3A7C }, // 0  Pg1
  { "Love Notes", 0xFB37 }, // 1
  { "Countdown",  0x07FF }, // 2
  { "Quotes",     0xFD20 }, // 3
  { "",           0x0000 }, // 4  hidden
  { "",           0x0000 }, // 5  hidden
  { "Gallery",    0xC212 }, // 6  Pg2
  { "Messages",   0x39C7 }, // 7
  { "Media",      0xD8E4 }, // 8
  { "Settings",   0x8410 }  // 9
};
const int TOTAL_APPS = 10;

void handleTap(int x, int y) {
  if (currentScreen == SCR_HOME) {
    int startIdx = homePage * 6;
    for (int i = startIdx; i < startIdx + 6 && i < TOTAL_APPS; i++) {
      if (strlen(apps[i].label) == 0) continue;

      int localPos = i - startIdx;
      int col = localPos % 2;
      int row = localPos / 2;
      int cx = 16 + col * (104 + 8);
      int cy = 70 + row * (90 + 12);

      if (x >= cx && x <= cx + 104 && y >= cy && y <= cy + 90) {
        buttonClickFeedback(cx, cy, 104, 90);
        switch (i) {
          case 0: currentScreen = SCR_CLOCK;     break;
          case 1: currentScreen = SCR_NOTES;     break;
          case 2: currentScreen = SCR_COUNTDOWN; break;
          case 3: currentScreen = SCR_QUOTES;    break;
          case 6: currentScreen = SCR_GALLERY;   break;
          case 7: currentScreen = SCR_MESSAGES;  break;
          case 8: currentScreen = SCR_MEDIA;     break;
          case 9: currentScreen = SCR_SETTINGS;  break;
        }
        needsRedraw = true;
        return;
      }
    }
    return;
  }

  // --- Sub-screens ---
  if (tappedBackButton(x, y)) { goHome(); return; }

  if (currentScreen == SCR_NOTES) {
    if (x >= 60 && x <= 180 && y >= 260 && y <= 300) {
      buttonClickFeedback(60, 260, 120, 40);
      noteIndex = (noteIndex + 1) % LOVE_NOTES_COUNT;
      needsRedraw = true;
    }
  }
  else if (currentScreen == SCR_QUOTES) {
    if (x >= 60 && x <= 180 && y >= 260 && y <= 300) {
      buttonClickFeedback(60, 260, 120, 40);
      handleSwipe(1);
    }
  }
  else if (currentScreen == SCR_MEDIA) {
    if (x >= 130 && x <= 220) {
      if (y >= 80 && y <= 120) {
        buttonClickFeedback(130, 80, 90, 40);
        SerialBT.println("CMD:PREV");
      }
      else if (y >= 140 && y <= 180) {
        buttonClickFeedback(130, 140, 90, 40);
        isPlaying = !isPlaying;
        SerialBT.println("CMD:PAUSE");
        needsRedraw = true;
      }
      else if (y >= 200 && y <= 240) {
        buttonClickFeedback(130, 200, 90, 40);
        SerialBT.println("CMD:NEXT");
      }
    }
  }
  else if (currentScreen == SCR_CLOCK) {
    // Tap "Request Sync" button
    if (x >= 60 && x <= 180 && y >= 250 && y <= 290) {
      buttonClickFeedback(60, 250, 120, 40);
      SerialBT.println("REQ:SYNC");
      needsRedraw = true;
    }
  }
}

// ==============================================================
//  HOME
// ==============================================================
void renderHome() {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(2);
  tft.setCursor(16, 20);
  tft.print(HOME_TITLE);

  // BT status dot + label
  tft.fillCircle(210, 26, 5, btConnected ? 0x07E0 : 0xF800);
  tft.setTextSize(1);
  tft.setTextColor(COL_SUBTEXT, COL_BG);
  tft.setCursor(186, 22);
  tft.print("BT");

  tft.setCursor(16, 46);
  tft.print("(Swipe for pages)");

  int startIdx = homePage * 6;
  int endIdx   = min(startIdx + 6, TOTAL_APPS);

  for (int i = startIdx; i < endIdx; i++) {
    if (strlen(apps[i].label) == 0) continue;

    int localPos = i - startIdx;
    int col = localPos % 2;
    int row = localPos / 2;
    int x = 16 + col * (104 + 8);
    int y = 70 + row * (90 + 12);

    tft.fillRoundRect(x, y, 104, 90, 12, apps[i].color);
    tft.setTextColor(COL_TEXT, apps[i].color);
    tft.setTextSize(1);
    int textW = strlen(apps[i].label) * 6;
    tft.setCursor(x + (104 - textW) / 2, y + 41);
    tft.print(apps[i].label);
  }

  int dotY = 310;
  if (homePage == 0) {
    tft.fillCircle(110, dotY, 4, COL_TEXT);
    tft.drawCircle(130, dotY, 4, COL_SUBTEXT);
  } else {
    tft.drawCircle(110, dotY, 4, COL_SUBTEXT);
    tft.fillCircle(130, dotY, 4, COL_TEXT);
  }
}

// ==============================================================
//  CLOCK  (BT-synced, self-ticking)
// ==============================================================
void drawClockTime() {
  int h, m, s;
  getCurrentTime(h, m, s);

  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);

  tft.fillRect(10, 130, 220, 40, COL_BG);
  tft.setTextSize(4);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setCursor(24, 135);
  tft.print(buf);
}

void renderClock() {
  tft.fillScreen(COL_BG);
  drawBackButton();

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(16, 60);
  tft.print("Clock (BT Synced)");

  if (timeSynced) {
    drawClockTime();
    if (dateSynced) {
      char dbuf[16];
      snprintf(dbuf, sizeof(dbuf), "%02d/%02d/%04d", curDay, curMon, curYear);
      tft.setTextSize(2);
      tft.setTextColor(COL_TEXT, COL_BG);
      tft.setCursor(62, 185);
      tft.print(dbuf);
    }
  } else {
    tft.setTextSize(3);
    tft.setTextColor(COL_SUBTEXT, COL_BG);
    tft.setCursor(50, 135);
    tft.print("--:--:--");
    tft.setTextSize(1);
    tft.setCursor(30, 185);
    tft.print("Not synced yet");
  }

  // Request Sync button
  tft.fillRoundRect(60, 250, 120, 40, 10, COL_CARD);
  tft.setTextColor(COL_TEXT, COL_CARD);
  tft.setTextSize(1);
  tft.setCursor(82, 265);
  tft.print("Request Sync");

  tft.setTextColor(COL_SUBTEXT, COL_BG);
  tft.setCursor(14, 305);
  tft.print("Send: TIME:HH:MM:SS  DATE:DD/MM/YYYY");
}

// ==============================================================
//  MEDIA
// ==============================================================
void renderMedia() {
  tft.fillScreen(COL_BG);
  drawBackButton();

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(80, 19);
  tft.print("Now Playing");

  tft.setTextSize(1);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextWrap(true);
  tft.setCursor(16, 50);
  tft.print(currentSong);

  tft.drawRect(16, 100, 100, 100, COL_SUBTEXT);
  tft.setTextColor(COL_SUBTEXT, COL_BG);
  tft.setCursor(38, 145);
  tft.print("Art Area");

  tft.fillRoundRect(130, 80, 90, 40, 8, COL_CARD);
  tft.setTextColor(COL_TEXT, COL_CARD);
  tft.setCursor(153, 95);
  tft.print("|< Prev");

  tft.fillRoundRect(130, 140, 90, 40, 8, COL_CARD);
  tft.setTextColor(COL_TEXT, COL_CARD);
  tft.setCursor(isPlaying ? 157 : 160, 155);
  tft.print(isPlaying ? "Pause" : "Play");

  tft.fillRoundRect(130, 200, 90, 40, 8, COL_CARD);
  tft.setTextColor(COL_TEXT, COL_CARD);
  tft.setCursor(153, 215);
  tft.print("Next >|");
}

// ==============================================================
//  LOVE NOTES
// ==============================================================
void renderNotes() {
  tft.fillScreen(COL_BG);
  drawBackButton();

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(16, 60);
  tft.print("For ");
  tft.print(HER_NAME);
  tft.setCursor(170, 60);
  tft.print("(Swipe <->)");

  tft.setTextSize(2);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextWrap(true);
  tft.setCursor(16, 110);
  tft.print(LOVE_NOTES[noteIndex]);

  tft.fillRoundRect(60, 260, 120, 40, 10, COL_ACCENT);
  tft.setTextColor(COL_BG, COL_ACCENT);
  tft.setTextSize(1);
  tft.setCursor(101, 275);
  tft.print("Next >");
}

// ==============================================================
//  COUNTDOWN  (BT-synced date, no WiFi)
// ==============================================================
int daysInMonth(int month, int year) {
  static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month == 2) {
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return leap ? 29 : 28;
  }
  return dim[month - 1];
}

long toJDN(int y, int m, int d) {   // day-count for date diffs
  int a = (14 - m) / 12;
  long yy = y + 4800 - a;
  int  mm = m + 12 * a - 3;
  return d + (153L * mm + 2) / 5 + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045L;
}

void renderCountdown() {
  tft.fillScreen(COL_BG);
  drawBackButton();

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(16, 60);
  tft.print("Us, so far");

  if (dateSynced) {
    long diff = toJDN(curYear, curMon, curDay) - toJDN(ANNIV_YEAR, ANNIV_MONTH, ANNIV_DAY);

    if (diff >= 0) {
      // elapsed years/months/days
      int yrs = curYear - ANNIV_YEAR;
      int mos = curMon  - ANNIV_MONTH;
      int dys = curDay  - ANNIV_DAY;
      if (dys < 0) {
        mos -= 1;
        int pm = curMon - 1, py = curYear;
        if (pm < 1) { pm = 12; py -= 1; }
        dys += daysInMonth(pm, py);
      }
      if (mos < 0) { yrs -= 1; mos += 12; }

      tft.setTextSize(3);
      tft.setTextColor(COL_ACCENT, COL_BG);
      tft.setCursor(20, 120);
      tft.print(yrs); tft.print("y ");
      tft.print(mos); tft.print("m");

      tft.setTextSize(2);
      tft.setCursor(20, 160);
      tft.print(dys); tft.print(" days");

      tft.setTextSize(1);
      tft.setTextColor(COL_SUBTEXT, COL_BG);
      tft.setCursor(20, 190);
      tft.print("("); tft.print(diff); tft.print(" days total)");
    } else {
      // anniversary is still in the future
      tft.setTextSize(3);
      tft.setTextColor(COL_ACCENT, COL_BG);
      tft.setCursor(20, 120);
      tft.print(-diff);
      tft.setTextSize(2);
      tft.setCursor(20, 160);
      tft.print("days to go!");
    }
  } else {
    tft.setTextSize(1);
    tft.setTextColor(COL_ACCENT, COL_BG);
    tft.setCursor(20, 140);
    tft.print("Waiting for BT date sync...");
    tft.setCursor(20, 160);
    tft.print("Send DATE:DD/MM/YYYY");
  }

  tft.setTextSize(1);
  tft.setTextColor(COL_SUBTEXT, COL_BG);
  tft.setCursor(16, 230);
  tft.print("Since: ");
  tft.print(ANNIV_DAY);   tft.print("/");
  tft.print(ANNIV_MONTH); tft.print("/");
  tft.print(ANNIV_YEAR);
}

// ==============================================================
//  QUOTES
// ==============================================================
void renderQuotes() {
  tft.fillScreen(COL_BG);
  drawBackButton();

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(16, 60);
  tft.print("Quote");
  tft.setCursor(170, 60);
  tft.print("(Swipe <->)");

  tft.setTextSize(2);
  tft.setTextWrap(true);
  tft.setCursor(16, 110);
  tft.print(QUOTES[quoteIndex]);

  tft.fillRoundRect(60, 260, 120, 40, 10, 0xFD20);
  tft.setTextColor(COL_BG, 0xFD20);
  tft.setTextSize(1);
  tft.setCursor(99, 275);
  tft.print("Shuffle");
}

// ==============================================================
//  SETTINGS
// ==============================================================
void drawBrightnessBar() {
  tft.fillRoundRect(30, 160, 180, 30, 8, COL_CARD);
  int fillW = map(backlightDuty, 0, 255, 6, 180);
  tft.fillRoundRect(30, 160, fillW, 30, 8, COL_ACCENT);
}

void renderSettings() {
  tft.fillScreen(COL_BG);
  drawBackButton();

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(16, 60);
  tft.print("Settings");

  tft.setCursor(16, 130);
  tft.print("Backlight (tap or drag)");
  drawBrightnessBar();

  tft.setTextColor(COL_SUBTEXT, COL_BG);
  tft.setCursor(16, 220);
  tft.print("BT: ");
  tft.setTextColor(btConnected ? 0x07E0 : 0xF800, COL_BG);
  tft.print(btConnected ? "Connected" : "Waiting for phone...");

  tft.setTextColor(COL_SUBTEXT, COL_BG);
  tft.setCursor(16, 240);
  tft.print("Device name: Srijita_OS");
}

// ==============================================================
//  MESSAGES
// ==============================================================
void renderMessages() {
  tft.fillScreen(COL_BG);
  drawBackButton();

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(16, 60);
  tft.print("Live Bluetooth Msg");

  tft.setTextSize(2);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextWrap(true);
  tft.setCursor(16, 110);
  tft.print(btMessage);
}

// ==============================================================
//  GALLERY (placeholder)
// ==============================================================
void renderGallery() {
  tft.fillScreen(COL_BG);
  drawBackButton();

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(16, 60);
  tft.print("Gallery");

  drawHeart(120, 150, 40, COL_ACCENT);

  tft.setTextSize(1);
  tft.setTextColor(COL_SUBTEXT, COL_BG);
  tft.setCursor(52, 220);
  tft.print("Photo gallery coming soon <3");
}

// ==============================================================
//  BOOT ANIMATION
// ==============================================================
void drawHeart(int x, int y, int size, uint16_t color) {
  int r = size / 2;
  tft.fillCircle(x - r / 2, y, r / 2, color);
  tft.fillCircle(x + r / 2, y, r / 2, color);
  tft.fillTriangle(x - r, y, x + r, y, x, y + r, color);
}

void bootAnimation() {
  const int FRAMES = 30;
  int heartX[6], heartY[6], heartSize[6];
  uint16_t heartColors[6] = {0xFB37, 0xF81F, 0xFD20, 0xFB37, 0xF81F, 0xFD20};

  randomSeed(analogRead(0));
  for (int i = 0; i < 6; i++) {
    heartX[i]    = random(20, 220);
    heartY[i]    = 320 + random(0, 200);
    heartSize[i] = random(10, 20);
  }

  for (int f = 0; f < FRAMES; f++) {
    tft.fillScreen(COL_BG);
    for (int i = 0; i < 6; i++) {
      heartY[i] -= 8;
      drawHeart(heartX[i], heartY[i], heartSize[i], heartColors[i]);
    }
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(28, 130);
    tft.print("Happy Birthday");
    tft.setTextSize(3);
    tft.setTextColor(COL_ACCENT, COL_BG);
    tft.setCursor(50, 165);
    tft.print(HER_NAME);
    tft.print("!");
    delay(40);
  }
  delay(500);
}

