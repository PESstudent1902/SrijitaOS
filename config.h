#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
//  PERSONALIZATION — edit this section freely
// ============================================================
#define HER_NAME        "Jaan"
#define HOME_TITLE      "Srijita's OS"

// Set this to your anniversary / "start date" (used by Countdown app)
#define ANNIV_YEAR   2025     // <-- CHANGE THIS to the actual year. Month/day are set below.
#define ANNIV_MONTH  12       // December
#define ANNIV_DAY    13       // 13th

// Optional: fill these in to enable live clock + accurate countdown via NTP.
// Leave blank ("") to skip WiFi — Clock/Countdown will show a manual fallback.
#define WIFI_SSID     ""
#define WIFI_PASS     ""
#define NTP_SERVER    "pool.ntp.org"
#define GMT_OFFSET_SEC   (5*3600 + 1800)  // IST = UTC+5:30
#define DAYLIGHT_OFFSET_SEC 0

// Love notes — cycles through these, tap "Next" to advance
static const char* LOVE_NOTES[] = {
  "You make ordinary days feel like plot twists in a good movie.",
  "CCD, PES campus, you. Best recurring scene in my life.",
  "Still my favorite person to overthink startup ideas with at 2am.",
  "You're the best co-founder of us.",
  "Every build I ship, I low-key want to show you first.",
  "Proud of you. Always. Even on the chaotic days.",
};
static const int LOVE_NOTES_COUNT = sizeof(LOVE_NOTES) / sizeof(LOVE_NOTES[0]);

// Quotes app — pick your vibe, swap these out anytime
static const char* QUOTES[] = {
  "Small steps daily beat big leaps rarely.",
  "Build the thing. Fix it later. Ship anyway.",
  "Discipline is choosing between what you want now and what you want most.",
  "Good things take time. So do good relationships and good code.",
  "Bad days are just plot setup for better ones.",
};
static const int QUOTES_COUNT = sizeof(QUOTES) / sizeof(QUOTES[0]);

// ============================================================
//  DISPLAY / TOUCH — screen is 240 x 320 (portrait), ST7789
// ============================================================
#define SCREEN_W 240
#define SCREEN_H 320

// NOTE: pin defs & touch calibration are assumed already handled in your
// existing TFT_eSPI User_Setup.h and touch init code from your prior setup.
// Nothing here overrides that.

// ============================================================
//  THEME COLORS (RGB565)
// ============================================================
#define COL_BG        0x10A2   // deep navy
#define COL_CARD      0x2A69   // card background
#define COL_ACCENT    0xFB37   // warm pink/coral
#define COL_TEXT      0xFFFF
#define COL_SUBTEXT   0xAD75
#define COL_BACK_BTN  0x4A29

#endif

