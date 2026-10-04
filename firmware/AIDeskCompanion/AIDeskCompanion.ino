// =====================================================================
// AI Desk Companion — a tiny living character on a 128x64 OLED,
// controlled with exactly one button.
//
// Required libraries (install via Arduino Library Manager):
//   - Adafruit GFX Library
//   - Adafruit SSD1306
//   - ArduinoJson (only needed once DEMO_MODE is set to false)
//   - ESP32Servo (V3 — drives the 2 SG90 ear servos, see EarManager.h)
//
// Board: "ESP32 Dev Module" (generic ESP32-WROOM / ESP32-32D)
//
// See Config.h for every pin, credential and timing constant.
// =====================================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "Config.h"
#include "Character.h"
#include "Animation.h"
#include "Buttons.h"
#include "Buzzer.h"
#include "WiFiManager.h"
#include "ClockManager.h"
#include "Weather.h"
#include "AI.h"
#include "Reminders.h"
#include "LEDManager.h"
#include "EarManager.h"
#include "MotionManager.h"
#include "ScreenManager.h"
#include "CloudAnimation.h"
#include "DrawPadManager.h"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN);

Character        character(display);
AnimationManager animation(character);
ButtonManager    button(BUTTON_PIN);
BuzzerManager    buzzer(BUZZER_PIN);
WiFiManager      wifiMgr;
ClockManager     clockMgr(wifiMgr);
WeatherManager   weatherMgr(wifiMgr);
AIManager        aiMgr(wifiMgr);
ReminderManager  reminderMgr(clockMgr);
LEDManager       ledMgr; // V2: the 3 top LEDs — see LEDManager.h
EarManager       earMgr; // V3: the 2 ear servos — see EarManager.h
MotionManager    motionMgr(animation, buzzer, ledMgr, earMgr); // V5
ScreenManager    screenMgr(display, character, animation, buzzer,
                            wifiMgr, clockMgr, weatherMgr, aiMgr, reminderMgr, ledMgr, earMgr, motionMgr);
CloudAnimationPlayer cloudAnim; // Animation Display Mode — see CloudAnimation.h
DrawPadManager drawPad(display, wifiMgr);
static bool restoreAnimationAfterDrawPad = false;

// ---------------------------------------------------------------------
// Boot animation — a short, non-blocking-per-frame "waking up" sequence
// shown once at power-on:
//   dot -> closed eyes -> tiny opening -> blink -> look around ->
//   surprised -> happy "Hi!" -> settle into normal idle
//
// Each phase is its own tight while-loop that keeps redrawing at ~100fps;
// this is intentionally the ONE place in the whole project a short delay()
// is acceptable, since nothing else needs to run before the character has
// finished introducing itself.
// ---------------------------------------------------------------------
static void bootPhase(unsigned long durationMs, Expression e, int originX, int originY, float scale) {
  character.setExpression(e, 0);
  unsigned long phaseStart = millis();
  while (millis() - phaseStart < durationMs) {
    display.clearDisplay();
    character.update();
    character.draw(originX, originY, scale);
    display.display();
    buzzer.update();
    delay(10);
  }
}

void playBootAnimation() {
  unsigned long start = millis();
  buzzer.beepBoot();

  // Frame 1: a small dot growing (fake "power on" pulse), ~500ms
  while (millis() - start < 500) {
    display.clearDisplay();
    int r = 1 + (int)((millis() - start) / 100);
    display.fillCircle(64, 32, r, SSD1306_WHITE);
    display.display();
    buzzer.update();
    delay(10);
  }

  bootPhase(450, Expression::SLEEPY,    64, 32, 1.0f); // closed eyes / tiny opening
  bootPhase(400, Expression::WAKE,      64, 32, 1.0f); // eyes opening
  bootPhase(160, Expression::BLINK,     64, 32, 1.0f); // blink
  bootPhase(260, Expression::LOOK_LEFT, 64, 32, 1.0f); // look around...
  bootPhase(260, Expression::LOOK_RIGHT,64, 32, 1.0f); // ...left then right
  bootPhase(400, Expression::SURPRISED, 64, 32, 1.0f); // notices the world

  // Happy + "Hi!"
  character.setExpression(Expression::HAPPY, 0);
  unsigned long hiStart = millis();
  while (millis() - hiStart < 900) {
    display.clearDisplay();
    character.update();
    character.draw(64, 22, 0.85f);
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(48, 48);
    display.print("Hi!");
    display.display();
    buzzer.update();
    delay(10);
  }

  character.setExpression(Expression::NORMAL, 0);
}

// Animation Display Mode on/off (4 quick presses). While on, ScreenManager is
// suspended (no screen cycling, does not draw) and CloudAnimationPlayer owns
// the OLED. Everything else — WiFi, LEDs, ears, buzzer, motion — keeps running.
static void toggleAnimationMode() {
  if (cloudAnim.isActive()) {
    cloudAnim.stop();                 // closes the file, network task goes idle
    screenMgr.setSuspended(false);    // normal screens resume (next draw() repaints)
    animation.notifyInteraction();    // restart the idle/sleep timer
    buzzer.beepFun();
  } else {
    if (screenMgr.isLocked()) {       // same rule as the other toggles: locked = acknowledge only
      buzzer.beepButton();
      return;
    }
    animation.notifyInteraction();
    buzzer.beepFun();
    screenMgr.setSuspended(true);
    cloudAnim.start();
  }
}

static void processDrawPadModeRequest() {
  const int8_t request = drawPad.takeModeRequest();
  if (request == 1 && !drawPad.isActive()) {
    // Draw Pad owns the OLED. Pause cloud animation playback first, then suspend
    // normal screen rendering so only DrawPadManager can update the panel.
    restoreAnimationAfterDrawPad = cloudAnim.isActive();
    if (restoreAnimationAfterDrawPad) cloudAnim.stop();
    screenMgr.setSuspended(true);
    drawPad.activate();
    animation.notifyInteraction();
  } else if (request == 0 && drawPad.isActive()) {
    drawPad.deactivate();
    if (restoreAnimationAfterDrawPad) {
      screenMgr.setSuspended(true);
      cloudAnim.start();
    } else {
      screenMgr.setSuspended(false);
    }
    restoreAnimationAfterDrawPad = false;
    animation.notifyInteraction();
  }
}

void setup() {
  Serial.begin(115200);

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed — check wiring/address."));
    for (;;) { delay(1000); } // nothing else to do without a display
  }
  display.clearDisplay();
  display.display();

  randomSeed(analogRead(0));

  button.begin();
  buzzer.begin();
  character.begin();
  animation.begin();
  wifiMgr.begin();
  clockMgr.begin();
  weatherMgr.begin();
  aiMgr.begin();
  reminderMgr.begin();
  ledMgr.begin(); // V2 — safe to call even if the LEDs aren't physically wired up
  earMgr.begin(); // V3 — safe to call even if the ear servos aren't physically wired up
  motionMgr.begin(); // V5 — safe failure path if MPU6050 is absent
  cloudAnim.begin(display); // Animation Display Mode — cheap; network task starts on first use
  drawPad.begin(); // local 128x64 web drawing pad; server uses whichever WiFi is active

  playBootAnimation();

  screenMgr.begin();
}

void loop() {
  // ---- Input ----
  ButtonEvent event = button.update();

  drawPad.update();
  processDrawPadModeRequest();
  cloudAnim.setDrawPadStatus(drawPad.isActive(), drawPad.clientCount(), drawPad.eventId(), drawPad.lastEvent());

#if ENABLE_ANIMATION_MODE
  if (event == ButtonEvent::QUADRUPLE_PRESS && !drawPad.isActive()) {
    toggleAnimationMode();
    event = ButtonEvent::NONE;
  } else if (cloudAnim.isActive() &&
             (event == ButtonEvent::SHORT_PRESS || event == ButtonEvent::LONG_PRESS)) {
    // Inside Animation Display Mode there is no screen to advance or lock.
    // Double (LED mode) and triple (ear mode) presses keep working.
    event = ButtonEvent::NONE;
  }
#endif
#if ENABLE_ANIMATION_MODE
  // Website "Mode Change" button (arrives via the cloud network task; same switch as 4 quick presses).
  const int8_t webModeRequest = cloudAnim.takeModeRequest();
  if (!drawPad.isActive() &&
      ((webModeRequest == 1 && !cloudAnim.isActive()) || (webModeRequest == 0 && cloudAnim.isActive()))) {
    toggleAnimationMode();
  }
#endif
  if (!drawPad.isActive() && event == ButtonEvent::SHORT_PRESS) {
    screenMgr.handleShortPress();
  } else if (!drawPad.isActive() && event == ButtonEvent::LONG_PRESS) {
    screenMgr.handleLongPress();
  } else if (event == ButtonEvent::DOUBLE_PRESS) {
    screenMgr.handleDoublePress(); // V2: LED Mode ON/OFF
  } else if (event == ButtonEvent::TRIPLE_PRESS) {
    screenMgr.handleTriplePress(); // V3: Ear Mode ON/OFF
  }

  // ---- Background systems (all non-blocking) ----
  wifiMgr.update();
  clockMgr.update();
  weatherMgr.update();
  aiMgr.update();
  reminderMgr.update();
  buzzer.update();
  ledMgr.update(); // V2 — always runs, so fades/mode-switch animations finish
                    // cleanly even the instant Mode 2 is turned off.
  earMgr.update();  // V3 — always runs, so the wake/sleep servo animations
                     // finish cleanly even the instant Ear Mode is toggled.
  motionMgr.update(); // V5 — non-blocking MPU poll/reaction layer

  // ---- Character + screen state ----
  // animation.update() runs first: if it needs to set a fresh transient
  // expression exactly this frame (e.g. the next step of the wake
  // sequence), character.update() below should see that fresh timer
  // rather than expiring the previous one first.
  animation.update(!screenMgr.isLocked() && !cloudAnim.isActive()); // no sleeping while an animation is on screen
  character.update();
  screenMgr.update();
  cloudAnim.update(); // no-op unless Animation Display Mode is on; draws the cloud animation

  // V2: a rare "special event" idle behaviour (firework, surprise, ...)
  // just started this frame — give it a fun little buzzer flourish. LEDs
  // already react automatically via ScreenManager::update() -> LEDManager.
  if (animation.consumeSpecialBeep()) {
    buzzer.beepFun();
  }

  // ---- Draw ----
  screenMgr.draw();
}
