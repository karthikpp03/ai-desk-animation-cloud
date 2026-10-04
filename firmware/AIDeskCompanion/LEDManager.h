#pragma once
#include <Arduino.h>
#include "Character.h" // for Expression

// The 3 top LEDs, addressed by position.
enum class LedId : uint8_t { LEFT = 0, CENTER = 1, RIGHT = 2 };

// One step of a short LED "choreography" (flash / sweep / all-flash /
// mode-switch confirmation / ...): a target brightness (0-255) for each
// of the 3 LEDs, held for `ms` before the next step plays. Chaining a
// few of these is exactly how AnimationManager chains face expressions
// (see SeqStep in Animation.h) — same idea, applied to LEDs.
struct LedStep {
  uint8_t left, center, right;
  uint16_t ms;
};

// How a given LED behaves when no override choreography is playing.
enum class LedAmbient : uint8_t {
  OFF,      // stays at 0
  HOLD,     // steady non-zero level (e.g. "looking left" — LEFT LED softly on)
  BREATHE   // slow sine pulse between 0 and a max level (sleep/thinking/bored/...)
};

// Non-blocking LED animation engine for the 3 top LEDs — the "expressive
// LED character" layer from the V2 brief. Two independent layers:
//
//   1. Ambient — a slow, continuous per-LED mood (off / steady hold /
//      breathing pulse), driven by syncWithExpression() so the LEDs read
//      as an extension of the character's eyes ("looking left" -> LEFT
//      glows; sleepy -> CENTER breathes very slowly; ...).
//   2. Override — a short multi-step choreography (flash, double-flash,
//      sweep, all-flash, mode-switch confirmation, ...) that temporarily
//      takes over all 3 LEDs, then hands control back to the ambient
//      layer automatically when it finishes.
//
// Brightness is never digitalWrite()'d directly to a target — every LED
// eases toward its target a little each update() call, which gives free
// smooth fades for both layers without any extra bookkeeping. Output
// uses simple time-sliced digitalWrite() ("software PWM", ~50Hz) instead
// of ledc/analogWrite, so it has no dependency on which ESP32 Arduino
// core version is installed.
//
// IMPORTANT: LEDManager never talks to Character/AnimationManager/
// ScreenManager directly except by reading an Expression value passed
// in — so if the LEDs are never wired up, or ENABLE_LED_MODE is off, the
// OLED character keeps working exactly as before. LEDs are strictly an
// optional extra layer, never a dependency.
class LEDManager {
public:
  LEDManager();

  void begin();
  void update(); // call every loop() — always, regardless of enabled state

  // Mode 1 (false) vs Mode 2 (true). Disabling immediately clears the
  // ambient layer (fades LEDs to off) but does not cut off an
  // already-running override choreography (e.g. the exit-mode fade).
  void setEnabled(bool en);
  bool isEnabled() const { return enabled; }

  // Drives the ambient layer from the character's current expression.
  // Only acts when the expression actually changed since the last call,
  // so it is safe (and expected) to call this every loop() while in
  // Mode 2. No-op while disabled.
  void syncWithExpression(Expression e);

  // ---- Named override choreographies -----------------------------------
  void playModeEnterAnimation(); // LEFT->CENTER->RIGHT sweep, then ALL flash once
  void playModeExitAnimation();  // fade everything to off
  void playNotification();       // ALL 3 flash — paired with an important notification
  void playWake();                // LEFT->CENTER->RIGHT then ALL briefly on
  void playMotionShock();         // V5: bright all-LED shock
  void playMotionShake(bool strong); // V5: rapid shake flash pattern

private:
  bool enabled;

  Expression lastReactedExpr;
  bool hasLastExpr;

  struct LedState {
    float current;          // what's actually being written (0-255, eased)
    LedAmbient ambientMode;
    uint8_t ambientLevel;    // HOLD level, or BREATHE max level
    uint16_t ambientPeriodMs; // BREATHE period
    unsigned long ambientPhaseStart;
  };
  LedState leds[3];

  // Override step engine — up to 4 steps, filled fresh by each play*()
  // call right before it starts (see setStep()/playSteps()).
  static const uint8_t MAX_STEPS = 4;
  LedStep stepBuf[MAX_STEPS];
  uint8_t stepLen;
  uint8_t stepIndex;
  bool overrideActive;
  unsigned long stepStart;

  void setStep(uint8_t idx, uint8_t l, uint8_t c, uint8_t r, uint16_t ms);
  void playSteps(uint8_t len);
  void advanceOverride(unsigned long now);

  void setAmbient(LedId id, LedAmbient mode, uint8_t level, uint16_t periodMs = 0);
  void allAmbientOff();
  uint8_t ambientTarget(int idx, unsigned long now) const;

  void flash(LedId id, uint8_t peak = 210, uint16_t onMs = 120, uint16_t tailMs = 90);
  void randomWink();

  uint8_t pinFor(int idx) const;
};
