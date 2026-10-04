#pragma once
#include <Arduino.h>
#include <ESP32Servo.h>
#include "Character.h" // for Expression

// V3 — the 2 SG90 ear servos. Mirrors the shape of LEDManager (ambient
// layer + short override choreographies, both eased toward their target
// via millis(), never delay()) so the ears read as one more expressive
// layer driven by the same Character::getExpression() the LEDs already
// react to — not a separate/random motor animation.
//
// Two independent layers, exactly like LEDManager:
//   1. Ambient — a persistent per-emotion pose (upright / tilted / drooped
//      / lowered / tilted-back) that the ears ease toward and then just
//      hold, driven by syncWithExpression().
//   2. Override — a short multi-step choreography (wiggle / perk / loose
//      "dizzy" flutter) that temporarily takes over both servos with its
//      own anticipation -> movement -> hold -> recovery beats, then hands
//      control back to the ambient layer automatically when it finishes.
//
// Only a deliberately small set of "meaningful" expressions changes the
// ears at all (see syncWithExpression()) — everything else (blinks,
// glances, most idle chatter) is intentionally a no-op so the ears don't
// twitch for every tiny eye movement.
//
// IMPORTANT: EarManager never talks to Character/AnimationManager/
// ScreenManager directly except by reading an Expression value passed in
// — so if the servos are never wired up, or ENABLE_EAR_MODE is off, the
// OLED character and everything else keeps working exactly as before.
// The ears are strictly an optional extra layer, never a dependency.
class EarManager {
public:
  EarManager();

  void begin();
  void update(); // call every loop() — always, regardless of enabled state

  // Ear Mode ON/OFF (V3 triple-press toggle). Disabling smoothly returns
  // both servos to their configured EAR_*_DOWN_ANGLE and then holds there
  // (no further writes once settled). Enabling plays a small wake/perk
  // animation and settles into the normal upright ambient pose.
  void setEnabled(bool en);
  bool isEnabled() const { return enabled; }

  // Drives the ambient layer from the character's current expression.
  // Only acts on expressions in the "meaningful reaction" set below, and
  // only when the expression actually changed since the last call, so it
  // is safe (and expected) to call this every loop() while Ear Mode is on.
  // No-op while disabled.
  void syncWithExpression(Expression e);

  // ---- Named override choreographies ------------------------------------
  void playWakeAnimation();  // Ear Mode turned ON: small perk-up, settle to upright
  void playSleepAnimation(); // Ear Mode turned OFF: smooth return to down, then stop

  // V5 physical-motion reactions. These are independent of the OLED
  // animation and remain non-blocking; they no-op safely when Ear Mode is off.
  void reactMotionFront();
  void reactMotionBack();
  void reactMotionLeft();
  void reactMotionRight();
  void reactMotionTap();
  void reactMotionShake(bool strong);
  void reactMotionDizzy();
  void reactMotionPickup();
  void reactMotionLanding();
  void reactMotionShock();
  void reactMotionWorried();

private:
  bool enabled;

  Expression lastReactedExpr;
  bool hasLastExpr;

  // One servo "channel" — current eased angle plus its ambient target.
  // lastWrittenAngle dedupes servo.write() calls: a new value is only
  // pushed to the servo when the rounded angle actually changes, which is
  // what "do not continuously move the servos" / "then stop" boil down to
  // in code — once a channel settles on its target, updates naturally
  // stop writing anything at all.
  struct EarChannel {
    Servo servo;
    float current;      // what's actually being written (degrees, eased)
    float ambientTarget;
    float ambientEase;  // EAR_EASE_FACTOR_NORMAL or _SLOW
    int lastWrittenAngle;
  };
  EarChannel left, right;

  // Override step engine — same "fill a small buffer, play it, advance by
  // millis()" shape LEDManager uses for its choreographies, just with a
  // per-step ease factor added so anticipation/hold/recovery beats can
  // each move at their own speed.
  struct EarStep {
    uint8_t leftAngle, rightAngle;
    uint16_t ms;
    float ease;
  };
  static const uint8_t MAX_STEPS = 5;
  EarStep stepBuf[MAX_STEPS];
  uint8_t stepLen;
  uint8_t stepIndex;
  bool overrideActive;
  unsigned long stepStart;
  unsigned long lastUpdateMs;
  unsigned long normalMicroStart;
  int8_t normalMicroSign;

  // What the ambient layer should settle back to once an override
  // sequence finishes (set right before playSteps() is called).
  uint8_t postOverrideLeft, postOverrideRight;
  float postOverrideEase;

  void setStep(uint8_t idx, uint8_t l, uint8_t r, uint16_t ms, float ease);
  void playSteps(uint8_t len, uint8_t afterLeft, uint8_t afterRight, float afterEase);
  void advanceOverride(unsigned long now);

  void setAmbient(uint8_t leftAngle, uint8_t rightAngle, float ease);

  void playHappyWiggle();
  void playExcitedWiggle();
  void playPerkUp();
  void playDizzyFlutter();
  void playYawnDroop();
};
