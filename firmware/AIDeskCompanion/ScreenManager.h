#pragma once
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Character.h"
#include "Animation.h"
#include "Buzzer.h"
#include "ClockManager.h"
#include "Weather.h"
#include "AI.h"
#include "Reminders.h"
#include "WiFiManager.h"
#include "LEDManager.h"
#include "EarManager.h"
#include "MotionManager.h"

// V2 — the two character modes from the design brief. NORMAL is exactly
// V1's OLED-only behaviour; LED_CHARACTER additionally syncs the 3 top
// LEDs to the character's expression. Toggled with a double press.
enum class CharacterMode : uint8_t { NORMAL = 0, LED_CHARACTER = 1 };

enum class ScreenType {
  CHARACTER_HOME = 0,
  CLOCK,
  WEATHER,
  TEMPERATURE,
  QUOTE,
  REMINDERS,
  AI_MESSAGE,
  SYSTEM_STATUS,
  SCREEN_COUNT   // sentinel, not a real screen
};

// Everything needed to add/remove a screen lives in one small struct —
// see SCREEN_TABLE in ScreenManager.cpp. Nothing about screen order or
// timing is hardcoded in loop().
struct ScreenSlot {
  ScreenType type;
  unsigned long minDurationMs;
  unsigned long maxDurationMs;
  bool enabled;
};

class ScreenManager {
public:
  ScreenManager(Adafruit_SSD1306 &d, Character &c, AnimationManager &a, BuzzerManager &b,
                WiFiManager &w, ClockManager &clk, WeatherManager &wthr,
                AIManager &ai, ReminderManager &rem, LEDManager &led, EarManager &ear, MotionManager &motion);

  void begin();
  void update();   // advances timers, checks for reminders, handles animation windows
  void draw();     // draws whatever should be on screen right now

  void handleShortPress();
  void handleLongPress();
  void handleDoublePress();  // V2: Mode 1 <-> Mode 2 (LED) toggle
  void handleTriplePress();  // V3: Ear Mode ON/OFF toggle

  // Animation Display Mode hands the OLED to CloudAnimationPlayer. While
  // suspended, update() still keeps LEDs/ears in sync but does no screen
  // cycling/reminders, and draw() leaves the display untouched.
  void setSuspended(bool s);
  bool isSuspended() const { return suspended; }

  bool isLocked() const { return locked; }
  CharacterMode getMode() const { return mode; }
  bool isEarModeOn() const { return earModeEnabled; }

private:
  Adafruit_SSD1306 &display;
  Character &character;
  AnimationManager &animation;
  BuzzerManager &buzzer;
  WiFiManager &wifiMgr;
  ClockManager &clockMgr;
  WeatherManager &weatherMgr;
  AIManager &aiMgr;
  ReminderManager &reminderMgr;
  LEDManager &ledMgr;
  EarManager &earMgr;
  MotionManager &motionMgr;

  int currentIndex;                  // index into enabled-screen order
  ScreenType currentScreen;
  unsigned long screenEnteredTime;
  unsigned long currentScreenDuration;
  bool locked;
  CharacterMode mode;                 // V2: NORMAL or LED_CHARACTER
  bool earModeEnabled;                 // V3: ears ON/OFF, independent of LED mode

  bool showingLockIndicator;
  unsigned long lockIndicatorStart;

  bool buttonReactionActive;
  unsigned long buttonReactionStart;

  bool reminderOverrideActive;
  String activeReminderText;

  int quoteIndex;

  bool suspended = false;            // Animation Display Mode owns the OLED

  void enterScreen(ScreenType s);
  void advanceToNextScreen();
  ScreenType nextEnabledScreen();
  unsigned long randomDurationFor(ScreenType s);

  void applyScreenMood(ScreenType s);   // makes the character react to what's showing
  void drawScreenContent(ScreenType s);
  void drawCharacterBadge(int x, int y, float scale);
  void drawHeader(const char *title);
};
