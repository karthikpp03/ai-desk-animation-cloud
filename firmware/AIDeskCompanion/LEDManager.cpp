#include "LEDManager.h"
#include "Config.h"
#include <math.h>

LEDManager::LEDManager()
  : enabled(false),
    lastReactedExpr(Expression::NORMAL),
    hasLastExpr(false),
    stepLen(0),
    stepIndex(0),
    overrideActive(false),
    stepStart(0) {
  for (int i = 0; i < 3; i++) {
    leds[i].current = 0.0f;
    leds[i].ambientMode = LedAmbient::OFF;
    leds[i].ambientLevel = 0;
    leds[i].ambientPeriodMs = 2000;
    leds[i].ambientPhaseStart = 0;
  }
}

uint8_t LEDManager::pinFor(int idx) const {
  switch (idx) {
    case 0: return LED_LEFT_PIN;
    case 1: return LED_CENTER_PIN;
    default: return LED_RIGHT_PIN;
  }
}

void LEDManager::begin() {
  for (int i = 0; i < 3; i++) {
    pinMode(pinFor(i), OUTPUT);
    digitalWrite(pinFor(i), LOW);
    leds[i].ambientPhaseStart = millis();
  }
}

void LEDManager::setEnabled(bool en) {
  enabled = en;
  if (!enabled) {
    allAmbientOff(); // fades to 0 via the normal easing in update()
  }
}

// =======================================================================
// Ambient layer
// =======================================================================
void LEDManager::setAmbient(LedId id, LedAmbient mode, uint8_t level, uint16_t periodMs) {
  int i = (int)id;
  leds[i].ambientMode = mode;
  leds[i].ambientLevel = level;
  if (periodMs > 0) leds[i].ambientPeriodMs = periodMs;
  leds[i].ambientPhaseStart = millis();
}

void LEDManager::allAmbientOff() {
  for (int i = 0; i < 3; i++) {
    leds[i].ambientMode = LedAmbient::OFF;
    leds[i].ambientLevel = 0;
  }
}

uint8_t LEDManager::ambientTarget(int idx, unsigned long now) const {
  const LedState &s = leds[idx];
  switch (s.ambientMode) {
    case LedAmbient::OFF:
      return 0;
    case LedAmbient::HOLD:
      return s.ambientLevel;
    case LedAmbient::BREATHE: {
      float phase = (float)((now - s.ambientPhaseStart) % s.ambientPeriodMs) / (float)s.ambientPeriodMs;
      float wave = 0.5f - 0.5f * cosf(phase * 2.0f * PI); // 0..1, starts at 0
      return (uint8_t)(s.ambientLevel * wave);
    }
  }
  return 0;
}

// =======================================================================
// Override step engine — same "fill a small buffer, play it, advance by
// millis()" shape as AnimationManager's sequence engine, just applied to
// 3 LED brightness targets per step instead of 1 face expression.
// =======================================================================
void LEDManager::setStep(uint8_t idx, uint8_t l, uint8_t c, uint8_t r, uint16_t ms) {
  if (idx >= MAX_STEPS) return;
  stepBuf[idx] = { l, c, r, ms };
}

void LEDManager::playSteps(uint8_t len) {
  stepLen = min(len, MAX_STEPS);
  stepIndex = 0;
  overrideActive = true;
  stepStart = millis();
}

void LEDManager::advanceOverride(unsigned long now) {
  if (!overrideActive) return;
  if (now - stepStart < stepBuf[stepIndex].ms) return;

  stepIndex++;
  if (stepIndex >= stepLen) {
    overrideActive = false;
    return;
  }
  stepStart = now;
}

// =======================================================================
// Named choreographies
// =======================================================================
void LEDManager::flash(LedId id, uint8_t peak, uint16_t onMs, uint16_t tailMs) {
  uint8_t l = 0, c = 0, r = 0;
  switch (id) {
    case LedId::LEFT:   l = peak; break;
    case LedId::CENTER: c = peak; break;
    case LedId::RIGHT:  r = peak; break;
  }
  setStep(0, l, c, r, onMs);
  setStep(1, 0, 0, 0, tailMs);
  playSteps(2);
}

void LEDManager::randomWink() {
  flash(random(0, 2) == 0 ? LedId::LEFT : LedId::RIGHT, 190, 140, 100);
}

void LEDManager::playModeEnterAnimation() {
  setStep(0, 180, 0,   0,   80);
  setStep(1, 0,   180, 0,   80);
  setStep(2, 0,   0,   180, 80);
  setStep(3, 230, 230, 230, 160);
  playSteps(4);
}

void LEDManager::playModeExitAnimation() {
  setStep(0, 90, 90, 90, 120);
  setStep(1, 0,  0,  0,  160);
  playSteps(2);
}

void LEDManager::playNotification() {
  setStep(0, 220, 220, 220, 110);
  setStep(1, 0,   0,   0,   90);
  setStep(2, 220, 220, 220, 110);
  setStep(3, 0,   0,   0,   0);
  playSteps(4);
}

void LEDManager::playWake() {
  setStep(0, 170, 0,   0,   90);
  setStep(1, 0,   170, 0,   90);
  setStep(2, 0,   0,   170, 90);
  setStep(3, 200, 200, 200, 200);
  playSteps(4);
}

void LEDManager::playMotionShock() {
  setStep(0, 255, 255, 255, 80);
  setStep(1, 0,   0,   0,   55);
  setStep(2, 245, 245, 245, 90);
  setStep(3, 0,   0,   0,   0);
  playSteps(4);
}
void LEDManager::playMotionShake(bool strong) {
  uint8_t p = strong ? 250 : 210;
  setStep(0, p, 0, 0, 55);
  setStep(1, 0, 0, p, 55);
  setStep(2, 0, p, 0, 55);
  setStep(3, strong ? p : 0, strong ? p : 0, strong ? p : 0, strong ? 100 : 0);
  playSteps(4);
}

// =======================================================================
// Expression -> LED reaction table. Only acts on an actual expression
// change, so it's safe to call every loop() from ScreenManager while in
// Mode 2. This is the "LEDs behave like an extension of the character's
// eyes/emotions" + "LED directional reaction system" + "LED emotional
// reaction library" sections of the brief, all in one place as requested
// ("do not scatter LED logic throughout the whole project").
// =======================================================================
void LEDManager::syncWithExpression(Expression e) {
  if (!enabled) return;
  if (hasLastExpr && e == lastReactedExpr) return;
  hasLastExpr = true;
  lastReactedExpr = e;

  switch (e) {
    // ---- Directional looks: LED on the matching side turns softly on
    // and STAYS on for as long as the look is held (it only turns off
    // once the expression changes again) — a quick glance therefore
    // reads as a brief flash, a longer idle look reads as a soft hold,
    // simply because the underlying expression durations already vary.
    case Expression::LOOK_LEFT:
    case Expression::MISCHIEVOUS: // (mostly) looks toward the left side
      allAmbientOff();
      setAmbient(LedId::LEFT, LedAmbient::HOLD, 150);
      break;

    case Expression::LOOK_RIGHT:
      allAmbientOff();
      setAmbient(LedId::RIGHT, LedAmbient::HOLD, 150);
      break;

    case Expression::LOOK_UP:
    case Expression::CURIOUS:
      allAmbientOff();
      setAmbient(LedId::CENTER, LedAmbient::HOLD, 140);
      break;

    case Expression::SIDE_EYE:
      allAmbientOff();
      setAmbient(LedId::RIGHT, LedAmbient::BREATHE, 110, 1300); // "side-eye pulses"
      break;

    case Expression::JUDGING:
    case Expression::ANNOYED:
      allAmbientOff();
      setAmbient(LedId::LEFT, LedAmbient::BREATHE, 90, 1500);
      break;

    // ---- Breathing / slow-pulse moods --------------------------------
    case Expression::THINKING:
      allAmbientOff();
      setAmbient(LedId::CENTER, LedAmbient::BREATHE, 130, 1400);
      break;

    case Expression::BORED:
      allAmbientOff();
      setAmbient(LedId::CENTER, LedAmbient::BREATHE, 40, 2600);
      break;

    case Expression::SLEEPY:
      allAmbientOff();
      setAmbient(LedId::CENTER, LedAmbient::BREATHE, 60, 2200);
      break;

    case Expression::SLEEP:
    case Expression::FAKE_SLEEP:
      allAmbientOff();
      setAmbient(LedId::CENTER, LedAmbient::BREATHE, 26, 3000); // "extremely subtle"
      break;

    case Expression::YAWN:
      allAmbientOff();
      setAmbient(LedId::CENTER, LedAmbient::BREATHE, 70, 1800);
      break;

    case Expression::HAPPY:
    case Expression::HAPPY_SQUINT:
      allAmbientOff();
      // V2.1: a quick, gentle symmetrical sparkle before settling into the
      // soft breathing pulse — reads as a warm "happy glow", not an alert
      // (peak is deliberately low + short, unlike the sharper ALL-flashes
      // used for SURPRISED/SHOCK/notifications below).
      setStep(0, 90, 90, 90, 110);
      setStep(1, 0,  0,  0,  90);
      playSteps(2);
      setAmbient(LedId::CENTER, LedAmbient::BREATHE, 120, 1000); // soft happy pulse
      break;

    case Expression::SMUG:
      allAmbientOff();
      setAmbient(LedId::CENTER, LedAmbient::HOLD, 90);
      break;

    // ---- One-shot flashes / sweeps -----------------------------------
    case Expression::SURPRISED:
      allAmbientOff();
      playNotification(); // ALL 3, short flash, off, flash again
      break;

    case Expression::SHOCK:
      // V2.1: SHOCK gets its own snappier single flash (vs. SURPRISED's
      // double-flash) so the two feel distinct — face timing already
      // matches (SHOCK's eyes are the largest/widest in computeFace()).
      allAmbientOff();
      setStep(0, 240, 240, 240, 90); // very quick, bright, all 3 at once
      setStep(1, 0,   0,   0,   0);
      playSteps(2);
      break;

    case Expression::ATTENTION:
    case Expression::STRETCH:
      allAmbientOff();
      setStep(0, 200, 200, 200, 130);
      setStep(1, 0,   0,   0,   0);
      playSteps(2);
      break;

    case Expression::CONFUSED:
      allAmbientOff();
      setStep(0, 190, 0, 0,   90);
      setStep(1, 0,   0, 190, 90);
      setStep(2, 190, 0, 0,   90);
      setStep(3, 0,   0, 0,   0);
      playSteps(4);
      break;

    case Expression::DIZZY:
      // V2.1: the "what just happened?!" reaction — rapid, irregular
      // LEFT -> RIGHT -> CENTER -> brief ALL-flash, distinct from
      // CONFUSED's steadier left/right sweep above.
      allAmbientOff();
      setStep(0, 200, 0,   0,   55);
      setStep(1, 0,   0,   200, 55);
      setStep(2, 0,   200, 0,   55);
      setStep(3, 230, 230, 230, 110);
      playSteps(4);
      break;

    case Expression::EXCITED:
      allAmbientOff();
      setStep(0, 190, 0,   0,   80);
      setStep(1, 0,   190, 0,   80);
      setStep(2, 0,   0,   190, 80);
      setStep(3, 220, 220, 220, 160);
      playSteps(4);
      break;

    case Expression::TINY_EXCITED:
      allAmbientOff();
      setStep(0, 170, 0,   0,   55);
      setStep(1, 0,   170, 0,   55);
      setStep(2, 0,   0,   170, 55);
      setStep(3, 0,   0,   0,   0);
      playSteps(4);
      break;

    case Expression::FIREWORK:
      allAmbientOff();
      setStep(0, 160, 0,   0,   70);
      setStep(1, 0,   160, 0,   70);
      setStep(2, 0,   0,   160, 70);
      setStep(3, 230, 230, 230, 220);
      playSteps(4);
      break;

    case Expression::WAKE:
      allAmbientOff();
      playWake();
      break;

    case Expression::WINK:
      allAmbientOff();
      flash(LedId::LEFT, 190, 150, 100);
      break;

    case Expression::PLAYFUL:
      allAmbientOff();
      randomWink();
      break;

    case Expression::LAUGHING:
      allAmbientOff();
      setStep(0, 180, 180, 180, 100);
      setStep(1, 0,   0,   0,   0);
      playSteps(2);
      break;

    case Expression::ROLLING_EYES:
      allAmbientOff();
      setStep(0, 150, 0, 0,   90);
      setStep(1, 0,   0, 150, 90);
      playSteps(2);
      break;

    case Expression::ANGRY:
      // V2.1: true fast LEFT/RIGHT alternating flashes (was a same-side
      // double-flash before) — short and punchy, paired with the
      // existing angry face (small focused pupils, tight mouth).
      allAmbientOff();
      setStep(0, 200, 0, 0,   60);
      setStep(1, 0,   0, 200, 60);
      setStep(2, 200, 0, 0,   60);
      setStep(3, 0,   0, 200, 60);
      playSteps(4);
      break;

    // ---- Everything else: no LED reaction (subdued/neutral moods) ---
    case Expression::NORMAL:
    case Expression::BLINK:
    case Expression::LOOK_DOWN:
    case Expression::SAD:
    case Expression::SHY:
    default:
      allAmbientOff();
      break;
  }
}

// =======================================================================
// Main update — advance whichever layer is authoritative for each LED,
// ease every LED's actual output toward that target, and write it out.
// =======================================================================
void LEDManager::update() {
  unsigned long now = millis();
  advanceOverride(now);

  for (int i = 0; i < 3; i++) {
    uint8_t target;
    if (overrideActive) {
      const LedStep &st = stepBuf[stepIndex];
      target = (i == 0) ? st.left : (i == 1) ? st.center : st.right;
    } else {
      target = ambientTarget(i, now);
    }

    float diff = (float)target - leds[i].current;
    if (fabsf(diff) > 0.5f) {
      leds[i].current += diff * 0.28f; // ease toward target -> free smooth fades
    } else {
      leds[i].current = target;
    }

    uint8_t out = (uint8_t)constrain((int)roundf(leds[i].current), 0, 255);

    // Software PWM: brief on/off time-slicing within a short fixed
    // period gives smooth-looking brightness without any ledc/
    // analogWrite dependency (portable across ESP32 Arduino core
    // versions).
    unsigned long cyclePos = now % LED_PWM_PERIOD_MS;
    bool on = cyclePos < ((unsigned long)LED_PWM_PERIOD_MS * out) / 255UL;
    digitalWrite(pinFor(i), (out > 0 && on) ? HIGH : LOW);
  }
}
