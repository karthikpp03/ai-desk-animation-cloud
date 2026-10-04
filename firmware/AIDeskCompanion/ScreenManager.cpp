#include "ScreenManager.h"
#include "Config.h"

// ---------------------------------------------------------------------
// Screen registry — add/remove/reorder screens by editing ONLY this
// table. Nothing in update()/draw() hardcodes a transition.
// ---------------------------------------------------------------------
static ScreenSlot SCREEN_TABLE[] = {
  { ScreenType::CHARACTER_HOME, 3000, 5000, true },
  { ScreenType::CLOCK,          4000, 6000, true },
  { ScreenType::WEATHER,        4000, 6000, true },
  { ScreenType::TEMPERATURE,    3000, 5000, true },
  { ScreenType::QUOTE,          5000, 7000, true },
  { ScreenType::REMINDERS,      5000, 8000, true },
  { ScreenType::AI_MESSAGE,     5000, 7000, true },
  { ScreenType::SYSTEM_STATUS,  3000, 5000, true },
};
static const int SCREEN_TABLE_SIZE = sizeof(SCREEN_TABLE) / sizeof(SCREEN_TABLE[0]);

static const char *QUOTES[] = {
  "Keep going!",
  "Small steps win.",
  "Breathe. Focus. Go.",
  "You are capable.",
  "Progress over perfect.",
  "One task at a time.",
};
static const int QUOTE_COUNT = sizeof(QUOTES) / sizeof(QUOTES[0]);

ScreenManager::ScreenManager(Adafruit_SSD1306 &d, Character &c, AnimationManager &a, BuzzerManager &b,
                             WiFiManager &w, ClockManager &clk, WeatherManager &wthr,
                             AIManager &ai, ReminderManager &rem, LEDManager &led, EarManager &ear, MotionManager &motion)
  : display(d), character(c), animation(a), buzzer(b), wifiMgr(w),
    clockMgr(clk), weatherMgr(wthr), aiMgr(ai), reminderMgr(rem), ledMgr(led), earMgr(ear), motionMgr(motion),
    currentIndex(0), currentScreen(ScreenType::CHARACTER_HOME),
    screenEnteredTime(0), currentScreenDuration(4000),
    locked(false), mode(CharacterMode::NORMAL), earModeEnabled(false),
    showingLockIndicator(false), lockIndicatorStart(0),
    buttonReactionActive(false), buttonReactionStart(0),
    reminderOverrideActive(false), quoteIndex(0) {}

void ScreenManager::begin() {
  currentIndex = 0;
  enterScreen(SCREEN_TABLE[0].type);
}

unsigned long ScreenManager::randomDurationFor(ScreenType s) {
  for (int i = 0; i < SCREEN_TABLE_SIZE; i++) {
    if (SCREEN_TABLE[i].type == s) {
      return random(SCREEN_TABLE[i].minDurationMs, SCREEN_TABLE[i].maxDurationMs);
    }
  }
  return 4000;
}

ScreenType ScreenManager::nextEnabledScreen() {
  int tries = 0;
  int idx = currentIndex;
  do {
    idx = (idx + 1) % SCREEN_TABLE_SIZE;
    tries++;
  } while (!SCREEN_TABLE[idx].enabled && tries <= SCREEN_TABLE_SIZE);
  currentIndex = idx;
  return SCREEN_TABLE[idx].type;
}

void ScreenManager::enterScreen(ScreenType s) {
  currentScreen = s;
  screenEnteredTime = millis();
  currentScreenDuration = randomDurationFor(s);
  applyScreenMood(s);

  if (s == ScreenType::QUOTE) {
    quoteIndex = random(0, QUOTE_COUNT);
  }
}

void ScreenManager::advanceToNextScreen() {
  ScreenType next = nextEnabledScreen();
  enterScreen(next);
}

void ScreenManager::applyScreenMood(ScreenType s) {
  // Rule: the character reacts to whatever information is being shown.
  // Weather / AI use the named multi-step reactions (they carry more
  // emotional weight); simpler screens just get a brief suggested mood.
  switch (s) {
    case ScreenType::CLOCK:
      animation.suggestMood(Expression::LOOK_LEFT, 500);
      break;
    case ScreenType::WEATHER:
      if (weatherMgr.hasError()) {
        animation.suggestMood(Expression::CONFUSED, 900);
      } else if (weatherMgr.isGoodWeather()) {
        animation.reactWeatherGood();
      } else if (weatherMgr.getCondition() == WeatherCondition::RAIN ||
                 weatherMgr.getCondition() == WeatherCondition::STORM) {
        animation.reactWeatherRain();
      } else {
        animation.suggestMood(Expression::SAD, 1000);
      }
      break;
    case ScreenType::TEMPERATURE:
      if (weatherMgr.getTemperatureC() >= 35.0f) {
        animation.reactWeatherHot();
      } else {
        animation.suggestMood(Expression::NORMAL, 400);
      }
      break;
    case ScreenType::QUOTE:
      animation.suggestMood(Expression::HAPPY, 1500);
      break;
    case ScreenType::REMINDERS:
      animation.suggestMood(Expression::EXCITED, 1000);
      break;
    case ScreenType::AI_MESSAGE:
      if (aiMgr.getState() == AIState::ERROR) {
        animation.reactAIError();
      } else if (aiMgr.getState() == AIState::REQUESTING) {
        animation.reactAIThinking();
      } else {
        animation.reactAIResponse();
      }
      break;
    case ScreenType::SYSTEM_STATUS:
      animation.suggestMood(wifiMgr.isConnected() ? Expression::NORMAL : Expression::CONFUSED, 600);
      break;
    default:
      break;
  }
}

void ScreenManager::handleShortPress() {
  animation.notifyInteraction();

  if (locked) {
    // Short press while locked: acknowledge with a tiny blip, but the
    // screen intentionally does not change (only long press toggles lock).
    buzzer.beepButton();
    animation.suggestMood(Expression::ATTENTION, 200);
    return;
  }

  buzzer.beepButton();
  animation.reactButtonShort(); // notice -> curious/happy, then AnimationManager settles back to idle
  buttonReactionActive = true;
  buttonReactionStart = millis();
}

void ScreenManager::handleLongPress() {
  animation.notifyInteraction();

  if (!locked) {
    locked = true;
    buzzer.beepLock();
    animation.reactButtonLong(); // notice -> curious -> satisfied/smug
    showingLockIndicator = true;
    lockIndicatorStart = millis();
  } else {
    locked = false;
    buzzer.beepUnlock();
    animation.reactUnlock(); // confused -> happy
    showingLockIndicator = false;
    enterScreen(currentScreen); // resume from the same screen, fresh timer
  }
}

void ScreenManager::handleDoublePress() {
  if (!ENABLE_LED_MODE) return; // feature compiled out / hardware not present
  animation.notifyInteraction();

  if (locked) {
    // Keep locked semantics simple and predictable: while locked, only
    // long-press unlocks. A double press while locked just acknowledges.
    buzzer.beepButton();
    return;
  }

  if (mode == CharacterMode::NORMAL) {
    mode = CharacterMode::LED_CHARACTER;
    ledMgr.setEnabled(true);
    buzzer.beepFun();
    animation.reactModeEnter();      // OLED: surprised -> happy
    ledMgr.playModeEnterAnimation(); // LEDs: L->C->R sweep, then all flash once
  } else {
    mode = CharacterMode::NORMAL;
    animation.reactModeExit();       // OLED: small wink/blink
    ledMgr.playModeExitAnimation();  // LEDs: fade to off
    ledMgr.setEnabled(false);
  }
}

void ScreenManager::handleTriplePress() {
  if (!ENABLE_EAR_MODE) return; // feature compiled out / hardware not present
  animation.notifyInteraction();

  if (locked) {
    // Same locked semantics as the double-press LED toggle: while locked,
    // only long-press unlocks. A triple press while locked just acknowledges.
    buzzer.beepButton();
    return;
  }

  earModeEnabled = !earModeEnabled;
  buzzer.beepFun();
  earMgr.setEnabled(earModeEnabled); // EarManager plays its own wake/sleep animation
}

void ScreenManager::update() {
  unsigned long now = millis();

  // V2: keep the LEDs in sync with the character's current expression
  // every loop() while in Mode 2. syncWithExpression() is a no-op unless
  // the expression actually changed, so this is cheap to call constantly.
  if (mode == CharacterMode::LED_CHARACTER) {
    ledMgr.syncWithExpression(character.getExpression());
  }

  // V3: keep the ears in sync with the character's current expression
  // every loop() while Ear Mode is on — independent of LED Mode, exactly
  // like the brief asks (two separate toggles). syncWithExpression() is a
  // no-op unless the expression actually changed AND it's one of the
  // "meaningful reaction" moods EarManager reacts to.
  if (earModeEnabled) {
    earMgr.syncWithExpression(character.getExpression());
  }

  // Animation Display Mode owns the OLED: no screen cycling, no reminder
  // screen jump (a due reminder stays pending and shows after exit).
  if (suspended) return;

  // Finish the "attention" flash from a short press, then actually advance.
  if (buttonReactionActive) {
    if (now - buttonReactionStart >= BUTTON_REACTION_MS) {
      buttonReactionActive = false;
      advanceToNextScreen();
    }
    return; // hold everything else while the reaction plays
  }

  if (showingLockIndicator && (now - lockIndicatorStart >= LOCK_INDICATOR_MS)) {
    showingLockIndicator = false;
  }

  // A due reminder always beeps and reacts, even while locked — but it
  // only jumps the display to the Reminders screen when unlocked, so a
  // locked screen's promise ("information remains visible") still holds.
  String dueText;
  if (reminderMgr.consumeDueReminder(dueText)) {
    activeReminderText = dueText;
    buzzer.beepNotification();
    animation.reactReminder(); // attention -> surprised
    if (!locked) {
      reminderOverrideActive = true;
      enterScreen(ScreenType::REMINDERS);
      currentScreenDuration = 6000; // give it a moment to be read
      return;
    }
  }

  if (locked) return;            // auto-advance is paused while locked
  if (animation.isAsleep()) return; // ...and while the character is asleep

  if (now - screenEnteredTime >= currentScreenDuration) {
    reminderOverrideActive = false;
    advanceToNextScreen();
  }
}

// ---------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------

void ScreenManager::drawHeader(const char *title) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 2);
  display.print(title);
}

void ScreenManager::drawCharacterBadge(int x, int y, float scale) {
  character.draw(x, y, scale);
}

static void drawSunIcon(Adafruit_SSD1306 &d, int cx, int cy) {
  d.fillCircle(cx, cy, 5, SSD1306_WHITE);
  for (int a = 0; a < 360; a += 45) {
    float rad = a * PI / 180.0f;
    int x1 = cx + cos(rad) * 8, y1 = cy + sin(rad) * 8;
    int x2 = cx + cos(rad) * 11, y2 = cy + sin(rad) * 11;
    d.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
  }
}

static void drawCloudIcon(Adafruit_SSD1306 &d, int cx, int cy) {
  d.fillCircle(cx - 5, cy + 2, 5, SSD1306_WHITE);
  d.fillCircle(cx + 3, cy, 6, SSD1306_WHITE);
  d.fillCircle(cx + 9, cy + 3, 4, SSD1306_WHITE);
  d.fillRect(cx - 8, cy + 2, 20, 5, SSD1306_WHITE);
}

static void drawRainIcon(Adafruit_SSD1306 &d, int cx, int cy) {
  drawCloudIcon(d, cx, cy - 4);
  for (int i = -6; i <= 6; i += 6) {
    d.drawLine(cx + i, cy + 6, cx + i - 2, cy + 11, SSD1306_WHITE);
  }
}

static void drawBellIcon(Adafruit_SSD1306 &d, int cx, int cy) {
  d.fillRoundRect(cx - 6, cy - 6, 12, 12, 4, SSD1306_WHITE);
  d.fillRect(cx - 2, cy + 6, 4, 3, SSD1306_WHITE);
  d.drawFastHLine(cx - 8, cy - 8, 16, SSD1306_WHITE);
}

// Simple manual word-wrap for text size 1 (~6px per character).
static void drawWrappedText(Adafruit_SSD1306 &d, const String &text, int x, int y, int maxCharsPerLine) {
  int start = 0;
  int lineY = y;
  while (start < (int)text.length()) {
    int end = min((int)text.length(), start + maxCharsPerLine);
    if (end < (int)text.length()) {
      int lastSpace = text.lastIndexOf(' ', end);
      if (lastSpace > start) end = lastSpace;
    }
    String line = text.substring(start, end);
    line.trim();
    int textX = x - (line.length() * 6) / 2;
    d.setCursor(textX, lineY);
    d.print(line);
    lineY += 10;
    start = end;
    while (start < (int)text.length() && text[start] == ' ') start++;
  }
}

void ScreenManager::drawScreenContent(ScreenType s) {
  display.setTextColor(SSD1306_WHITE);

  switch (s) {
    case ScreenType::CHARACTER_HOME:
      drawCharacterBadge(64, 32, 1.0f);
      break;

    case ScreenType::CLOCK: {
      drawCharacterBadge(64, 14, 0.35f);
      display.setTextSize(2);
      String t = clockMgr.getTimeString();
      display.setCursor(64 - (t.length() * 12) / 2, 32);
      display.print(t);
      display.setTextSize(1);
      String day = clockMgr.getDayString();
      display.setCursor(64 - (day.length() * 6) / 2, 54);
      display.print(day);
      break;
    }

    case ScreenType::WEATHER: {
      drawCharacterBadge(24, 16, 0.32f);
      switch (weatherMgr.getCondition()) {
        case WeatherCondition::RAIN:
        case WeatherCondition::STORM:
          drawRainIcon(display, 100, 16);
          break;
        case WeatherCondition::CLOUDS:
        case WeatherCondition::MIST:
          drawCloudIcon(display, 100, 16);
          break;
        default:
          drawSunIcon(display, 100, 14);
          break;
      }
      display.setTextSize(2);
      char buf[8];
      snprintf(buf, sizeof(buf), "%.0fC", weatherMgr.getTemperatureC());
      String tstr(buf);
      display.setCursor(64 - (tstr.length() * 12) / 2, 32);
      display.print(tstr);
      display.setTextSize(1);
      String loc = weatherMgr.getLocationName();
      display.setCursor(64 - (loc.length() * 6) / 2, 54);
      display.print(weatherMgr.hasError() ? "(cached) " + loc : loc);
      break;
    }

    case ScreenType::TEMPERATURE: {
      drawCharacterBadge(64, 12, 0.3f);
      display.setTextSize(2);
      char buf[8];
      snprintf(buf, sizeof(buf), "%.0fC", weatherMgr.getTemperatureC());
      String tstr(buf);
      display.setCursor(64 - (tstr.length() * 12) / 2, 30);
      display.print(tstr);
      display.setTextSize(1);
      display.setCursor(64 - (5 * 6) / 2, 52);
      display.print("Today");
      break;
    }

    case ScreenType::QUOTE: {
      drawCharacterBadge(20, 12, 0.28f);
      display.setTextSize(1);
      String q = String("\"") + QUOTES[quoteIndex] + "\"";
      drawWrappedText(display, q, 64, 30, 20);
      break;
    }

    case ScreenType::REMINDERS: {
      drawBellIcon(display, 64, 14);
      display.setTextSize(1);
      String text = reminderOverrideActive ? activeReminderText : reminderMgr.getNextReminderPreview();
      drawWrappedText(display, text, 64, 34, 18);
      break;
    }

    case ScreenType::AI_MESSAGE: {
      drawCharacterBadge(64, 14, 0.35f);
      display.setTextSize(1);
      if (aiMgr.getState() == AIState::REQUESTING) {
        display.setCursor(64 - (10 * 6) / 2, 40);
        display.print("Thinking...");
      } else if (aiMgr.getState() == AIState::ERROR) {
        display.setCursor(64 - (12 * 6) / 2, 40);
        display.print("No message");
      } else {
        drawWrappedText(display, aiMgr.getMessage(), 64, 34, 18);
      }
      break;
    }

    case ScreenType::SYSTEM_STATUS: {
      drawHeader("SYSTEM  " FIRMWARE_VERSION);
      display.setTextSize(1);
      // Compact 7-row layout (7px spacing) — still fits everything V1/V2
      // showed, plus the new V3 field (Ears).
      display.setCursor(4, 12);
      display.print("Mode: ");
      display.print(mode == CharacterMode::LED_CHARACTER ? "LED" : "NORMAL");
      display.setCursor(4, 19);
      display.print("Data: ");
      display.print(DEMO_MODE ? "DEMO" : "LIVE");
      display.print("  WiFi:");
      display.print(wifiMgr.isConnected() ? "OK" : "---");
      display.setCursor(4, 26);
      display.print("LED:");
      display.print(!ENABLE_LED_MODE ? "OFF" : (ledMgr.isEnabled() ? "ON" : "OK"));
      display.print("  Ears:");
      display.print(!ENABLE_EAR_MODE ? "OFF" : (earMgr.isEnabled() ? "ON" : "OK"));
      display.setCursor(4, 33);
      display.print("MPU: ");
      display.print(motionMgr.isAvailable() ? "OK" : "ERROR");
      display.setCursor(4, 40);
      display.print("Motion: ");
      display.print(motionMgr.isEnabled() ? "ON" : "OFF");
      display.setCursor(4, 47);
      display.print("Heap: ");
      display.print(ESP.getFreeHeap());
      display.setCursor(4, 54);
      display.print("Up: ");
      display.print(millis() / 1000);
      display.print("s");
      break;
    }

    default:
      break;
  }
}

void ScreenManager::setSuspended(bool s) {
  if (suspended == s) return;
  suspended = s;
  if (!suspended) {
    // Resume the normal screen sequence from where it was, fresh timer.
    buttonReactionActive = false;
    showingLockIndicator = false;
    enterScreen(currentScreen);
  }
}

void ScreenManager::draw() {
  if (suspended) return; // CloudAnimationPlayer is drawing
  display.clearDisplay();

  if (animation.isAsleep()) {
    // Sleep takes over the whole display, regardless of which info
    // screen was showing when the timeout hit. Waking resumes exactly
    // where the slideshow left off (see AnimationManager::notifyInteraction).
    character.draw(64, 32, 1.0f);
  } else if (buttonReactionActive) {
    // Big, clear attention reaction fills the screen briefly.
    character.draw(64, 32, 0.9f);
  } else {
    drawScreenContent(currentScreen);
  }

  if (showingLockIndicator) {
    display.fillRect(34, 24, 60, 16, SSD1306_BLACK);
    display.drawRect(34, 24, 60, 16, SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(44, 28);
    display.print("LOCKED");
  } else if (locked) {
    // Small persistent lock glyph, top-right corner, so it never eats
    // meaningful screen space.
    display.drawRect(120, 2, 6, 5, SSD1306_WHITE);
    display.drawFastHLine(121, 1, 4, SSD1306_WHITE);
  }

  display.display();
}
