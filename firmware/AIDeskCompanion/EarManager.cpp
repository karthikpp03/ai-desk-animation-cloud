#include "EarManager.h"
#include "Config.h"
#include <math.h>

EarManager::EarManager()
  : enabled(false),
    lastReactedExpr(Expression::NORMAL),
    hasLastExpr(false),
    stepLen(0),
    stepIndex(0),
    overrideActive(false),
    stepStart(0),
    lastUpdateMs(0),
    normalMicroStart(0),
    normalMicroSign(1),
    postOverrideLeft(EAR_LEFT_DOWN_ANGLE),
    postOverrideRight(EAR_RIGHT_DOWN_ANGLE),
    postOverrideEase(EAR_EASE_FACTOR_SLOW) {
  left.current = EAR_LEFT_DOWN_ANGLE;
  left.ambientTarget = EAR_LEFT_DOWN_ANGLE;
  left.ambientEase = EAR_EASE_FACTOR_SLOW;
  left.lastWrittenAngle = -1;

  right.current = EAR_RIGHT_DOWN_ANGLE;
  right.ambientTarget = EAR_RIGHT_DOWN_ANGLE;
  right.ambientEase = EAR_EASE_FACTOR_SLOW;
  right.lastWrittenAngle = -1;
}

void EarManager::begin() {
  left.servo.setPeriodHertz(50);
  right.servo.setPeriodHertz(50);
  left.servo.attach(EAR_LEFT_SERVO_PIN, EAR_SERVO_MIN_US, EAR_SERVO_MAX_US);
  right.servo.attach(EAR_RIGHT_SERVO_PIN, EAR_SERVO_MIN_US, EAR_SERVO_MAX_US);

  // Boot state: Ear Mode starts OFF (matches LEDManager's Mode 1 default),
  // so the ears simply park at rest — no wake animation at power-on.
  left.current = EAR_LEFT_DOWN_ANGLE;
  right.current = EAR_RIGHT_DOWN_ANGLE;
  left.servo.write(EAR_LEFT_DOWN_ANGLE);
  right.servo.write(EAR_RIGHT_DOWN_ANGLE);
  left.lastWrittenAngle = EAR_LEFT_DOWN_ANGLE;
  right.lastWrittenAngle = EAR_RIGHT_DOWN_ANGLE;

  lastUpdateMs = millis();
  normalMicroStart = lastUpdateMs;
}

void EarManager::setEnabled(bool en) {
  if (en == enabled) return;
  enabled = en;
  hasLastExpr = false; // re-evaluate ambient fresh once mode is back on

  if (enabled) {
    playWakeAnimation();
  } else {
    playSleepAnimation();
  }
}

// =======================================================================
// Ambient layer
// =======================================================================
void EarManager::setAmbient(uint8_t leftAngle, uint8_t rightAngle, float ease) {
  left.ambientTarget = leftAngle;
  left.ambientEase = ease;
  right.ambientTarget = rightAngle;
  right.ambientEase = ease;
}

// =======================================================================
// Override step engine — same "fill a small buffer, play it, advance by
// millis()" shape LEDManager uses, with a per-step ease factor added so
// anticipation/movement/hold/recovery beats can each move at their own
// speed instead of one fixed rate for the whole sequence.
// =======================================================================
void EarManager::setStep(uint8_t idx, uint8_t l, uint8_t r, uint16_t ms, float ease) {
  if (idx >= MAX_STEPS) return;
  stepBuf[idx] = { l, r, ms, ease };
}

void EarManager::playSteps(uint8_t len, uint8_t afterLeft, uint8_t afterRight, float afterEase) {
  stepLen = min(len, MAX_STEPS);
  stepIndex = 0;
  overrideActive = true;
  stepStart = millis();
  postOverrideLeft = afterLeft;
  postOverrideRight = afterRight;
  postOverrideEase = afterEase;
}

void EarManager::advanceOverride(unsigned long now) {
  if (!overrideActive) return;
  if (now - stepStart < stepBuf[stepIndex].ms) return;

  stepIndex++;
  if (stepIndex >= stepLen) {
    overrideActive = false;
    // Hand back to the ambient layer at whatever pose this reaction
    // should settle into (e.g. back to upright after a happy wiggle).
    setAmbient(postOverrideLeft, postOverrideRight, postOverrideEase);
    return;
  }
  stepStart = now;
}

// =======================================================================
// Named override choreographies
// =======================================================================
void EarManager::playHappyWiggle() {
  int lu = EAR_LEFT_UPRIGHT_ANGLE, ru = EAR_RIGHT_UPRIGHT_ANGLE;
  setStep(0, constrain(lu - 8, 0, 180), constrain(ru - 8, 0, 180), 90,  EAR_EASE_FACTOR_NORMAL); // tiny anticipation dip
  setStep(1, constrain(lu + 12, 0, 180), constrain(ru + 12, 0, 180), 90,  EAR_EASE_FACTOR_NORMAL); // wiggle out
  setStep(2, constrain(lu - 4, 0, 180), constrain(ru - 4, 0, 180), 90,  EAR_EASE_FACTOR_NORMAL); // wiggle back
  setStep(3, (uint8_t)lu, (uint8_t)ru, 140, EAR_EASE_FACTOR_NORMAL); // settle
  playSteps(4, (uint8_t)lu, (uint8_t)ru, EAR_EASE_FACTOR_NORMAL);
}

void EarManager::playExcitedWiggle() {
  int lu = EAR_LEFT_UPRIGHT_ANGLE, ru = EAR_RIGHT_UPRIGHT_ANGLE;
  setStep(0, constrain(lu + 18, 0, 180), constrain(ru - 18, 0, 180), 70, EAR_EASE_FACTOR_NORMAL);
  setStep(1, constrain(lu - 18, 0, 180), constrain(ru + 18, 0, 180), 70, EAR_EASE_FACTOR_NORMAL);
  setStep(2, constrain(lu + 18, 0, 180), constrain(ru - 18, 0, 180), 70, EAR_EASE_FACTOR_NORMAL);
  setStep(3, constrain(lu - 18, 0, 180), constrain(ru + 18, 0, 180), 70, EAR_EASE_FACTOR_NORMAL);
  setStep(4, (uint8_t)lu, (uint8_t)ru, 150, EAR_EASE_FACTOR_NORMAL); // settle
  playSteps(5, (uint8_t)lu, (uint8_t)ru, EAR_EASE_FACTOR_NORMAL);
}

void EarManager::playPerkUp() {
  int lu = EAR_LEFT_UPRIGHT_ANGLE, ru = EAR_RIGHT_UPRIGHT_ANGLE;
  setStep(0, constrain(lu - 6, 0, 180), constrain(ru - 6, 0, 180), 50,  EAR_EASE_FACTOR_NORMAL); // tiny anticipation
  setStep(1, EAR_LEFT_PERK_ANGLE, EAR_RIGHT_PERK_ANGLE, 90,  EAR_EASE_FACTOR_NORMAL);             // quick perk — the one place fast/sudden is wanted
  setStep(2, EAR_LEFT_PERK_ANGLE, EAR_RIGHT_PERK_ANGLE, 220, EAR_EASE_FACTOR_NORMAL);             // hold
  setStep(3, (uint8_t)lu, (uint8_t)ru, 160, EAR_EASE_FACTOR_NORMAL);                              // recover
  playSteps(4, (uint8_t)lu, (uint8_t)ru, EAR_EASE_FACTOR_NORMAL);
}

void EarManager::playDizzyFlutter() {
  int lu = EAR_LEFT_UPRIGHT_ANGLE, ru = EAR_RIGHT_UPRIGHT_ANGLE;
  uint8_t l0 = (uint8_t)constrain(lu + random(-20, 21), 0, 180);
  uint8_t r0 = (uint8_t)constrain(ru + random(-20, 21), 0, 180);
  uint8_t l1 = (uint8_t)constrain(lu + random(-20, 21), 0, 180);
  uint8_t r1 = (uint8_t)constrain(ru + random(-20, 21), 0, 180);
  uint8_t l2 = (uint8_t)constrain(lu + random(-20, 21), 0, 180);
  uint8_t r2 = (uint8_t)constrain(ru + random(-20, 21), 0, 180);
  setStep(0, l0, r0, 60, EAR_EASE_FACTOR_NORMAL);
  setStep(1, l1, r1, 60, EAR_EASE_FACTOR_NORMAL);
  setStep(2, l2, r2, 60, EAR_EASE_FACTOR_NORMAL);
  setStep(3, (uint8_t)lu, (uint8_t)ru, 140, EAR_EASE_FACTOR_NORMAL); // recover
  playSteps(4, (uint8_t)lu, (uint8_t)ru, EAR_EASE_FACTOR_NORMAL);
}

void EarManager::playYawnDroop() {
  int lu = EAR_LEFT_UPRIGHT_ANGLE, ru = EAR_RIGHT_UPRIGHT_ANGLE;
  setStep(0, constrain(lu + 5, 0, 180), constrain(ru - 5, 0, 180), 150, EAR_EASE_FACTOR_NORMAL); // tiny lift (anticipation)
  setStep(1, EAR_LEFT_DROOP_ANGLE, EAR_RIGHT_DROOP_ANGLE, 550, EAR_EASE_FACTOR_SLOW);             // slow droop
  setStep(2, EAR_LEFT_DROOP_ANGLE, EAR_RIGHT_DROOP_ANGLE, 300, EAR_EASE_FACTOR_SLOW);             // hold drooped
  playSteps(3, EAR_LEFT_DROOP_ANGLE, EAR_RIGHT_DROOP_ANGLE, EAR_EASE_FACTOR_SLOW);
}

void EarManager::playWakeAnimation() {
  setStep(0, EAR_LEFT_PERK_ANGLE, EAR_RIGHT_PERK_ANGLE, 160, EAR_EASE_FACTOR_NORMAL);
  setStep(1, EAR_LEFT_UPRIGHT_ANGLE, EAR_RIGHT_UPRIGHT_ANGLE, 220, EAR_EASE_FACTOR_NORMAL);
  playSteps(2, EAR_LEFT_UPRIGHT_ANGLE, EAR_RIGHT_UPRIGHT_ANGLE, EAR_EASE_FACTOR_NORMAL);
}

void EarManager::playSleepAnimation() {
  setStep(0, EAR_LEFT_DOWN_ANGLE, EAR_RIGHT_DOWN_ANGLE, 260, EAR_EASE_FACTOR_SLOW);
  playSteps(1, EAR_LEFT_DOWN_ANGLE, EAR_RIGHT_DOWN_ANGLE, EAR_EASE_FACTOR_SLOW);
}

// =======================================================================
// V5 physical-motion ear reactions
// =======================================================================
void EarManager::reactMotionFront() {
  int l = EAR_LEFT_UPRIGHT_ANGLE + EAR_FRONT_BACK_OFFSET;
  int r = EAR_RIGHT_UPRIGHT_ANGLE - EAR_FRONT_BACK_OFFSET;
  setStep(0, constrain(EAR_LEFT_UPRIGHT_ANGLE + 6,0,180), constrain(EAR_RIGHT_UPRIGHT_ANGLE - 6,0,180), 90, EAR_EASE_FACTOR_NORMAL);
  setStep(1, constrain(l,0,180), constrain(r,0,180), 360, EAR_EASE_FACTOR_NORMAL);
  setStep(2, constrain(l,0,180), constrain(r,0,180), 650, EAR_EASE_FACTOR_SLOW);
  setStep(3, EAR_LEFT_UPRIGHT_ANGLE, EAR_RIGHT_UPRIGHT_ANGLE, 520, EAR_EASE_FACTOR_SLOW);
  playSteps(4, EAR_LEFT_UPRIGHT_ANGLE, EAR_RIGHT_UPRIGHT_ANGLE, EAR_EASE_FACTOR_NORMAL);
}
void EarManager::reactMotionBack() {
  int l = EAR_LEFT_UPRIGHT_ANGLE - EAR_FRONT_BACK_OFFSET;
  int r = EAR_RIGHT_UPRIGHT_ANGLE + EAR_FRONT_BACK_OFFSET;
  setStep(0, constrain(EAR_LEFT_UPRIGHT_ANGLE - 6,0,180), constrain(EAR_RIGHT_UPRIGHT_ANGLE + 6,0,180), 90, EAR_EASE_FACTOR_NORMAL);
  setStep(1, constrain(l,0,180), constrain(r,0,180), 360, EAR_EASE_FACTOR_NORMAL);
  setStep(2, constrain(l,0,180), constrain(r,0,180), 650, EAR_EASE_FACTOR_SLOW);
  setStep(3, EAR_LEFT_UPRIGHT_ANGLE, EAR_RIGHT_UPRIGHT_ANGLE, 520, EAR_EASE_FACTOR_SLOW);
  playSteps(4, EAR_LEFT_UPRIGHT_ANGLE, EAR_RIGHT_UPRIGHT_ANGLE, EAR_EASE_FACTOR_NORMAL);
}
void EarManager::reactMotionLeft() {
  int l = EAR_LEFT_UPRIGHT_ANGLE + EAR_SIDE_OFFSET;
  int r = EAR_RIGHT_UPRIGHT_ANGLE - EAR_SUBTLE_OFFSET;
  setStep(0, EAR_LEFT_UPRIGHT_ANGLE + 5, EAR_RIGHT_UPRIGHT_ANGLE - 4, 80, EAR_EASE_FACTOR_NORMAL);
  setStep(1, constrain(l,0,180), constrain(r,0,180), 220, EAR_EASE_FACTOR_NORMAL);
  setStep(2, constrain(l,0,180), constrain(r,0,180), 360, EAR_EASE_FACTOR_SLOW);
  setStep(3, EAR_LEFT_UPRIGHT_ANGLE, EAR_RIGHT_UPRIGHT_ANGLE, 400, EAR_EASE_FACTOR_SLOW);
  playSteps(4, EAR_LEFT_UPRIGHT_ANGLE, EAR_RIGHT_UPRIGHT_ANGLE, EAR_EASE_FACTOR_NORMAL);
}
void EarManager::reactMotionRight() {
  int l = EAR_LEFT_UPRIGHT_ANGLE - EAR_SUBTLE_OFFSET;
  int r = EAR_RIGHT_UPRIGHT_ANGLE + EAR_SIDE_OFFSET;
  setStep(0, EAR_LEFT_UPRIGHT_ANGLE - 4, EAR_RIGHT_UPRIGHT_ANGLE + 5, 80, EAR_EASE_FACTOR_NORMAL);
  setStep(1, constrain(l,0,180), constrain(r,0,180), 220, EAR_EASE_FACTOR_NORMAL);
  setStep(2, constrain(l,0,180), constrain(r,0,180), 360, EAR_EASE_FACTOR_SLOW);
  setStep(3, EAR_LEFT_UPRIGHT_ANGLE, EAR_RIGHT_UPRIGHT_ANGLE, 400, EAR_EASE_FACTOR_SLOW);
  playSteps(4, EAR_LEFT_UPRIGHT_ANGLE, EAR_RIGHT_UPRIGHT_ANGLE, EAR_EASE_FACTOR_NORMAL);
}
void EarManager::reactMotionTap() {
  int lu=EAR_LEFT_UPRIGHT_ANGLE, ru=EAR_RIGHT_UPRIGHT_ANGLE;
  setStep(0, lu-5, ru+5, 70, EAR_EASE_FACTOR_NORMAL);
  setStep(1, lu+18, ru, 110, EAR_EASE_FACTOR_NORMAL);
  setStep(2, lu, ru+18, 110, EAR_EASE_FACTOR_NORMAL);
  setStep(3, lu, ru, 220, EAR_EASE_FACTOR_SLOW);
  playSteps(4, lu, ru, EAR_EASE_FACTOR_NORMAL);
}
void EarManager::reactMotionShake(bool strong) {
  int a = strong ? EAR_SHAKE_OFFSET : EAR_SHAKE_OFFSET - 8;
  int lu=EAR_LEFT_UPRIGHT_ANGLE, ru=EAR_RIGHT_UPRIGHT_ANGLE;
  setStep(0, lu-8, ru+7, 90, EAR_EASE_FACTOR_FAST);
  setStep(1, constrain(lu+a,0,180), constrain(ru-a/2,0,180), 120, EAR_EASE_FACTOR_NORMAL);
  setStep(2, constrain(lu-a/2,0,180), constrain(ru+a,0,180), 120, EAR_EASE_FACTOR_NORMAL);
  setStep(3, constrain(lu+a,0,180), constrain(ru-a,0,180), 120, EAR_EASE_FACTOR_NORMAL);
  setStep(4, constrain(lu-a/2,0,180), constrain(ru+a/2,0,180), 160, EAR_EASE_FACTOR_NORMAL);
  playSteps(5, lu, ru, EAR_EASE_FACTOR_SLOW);
}
void EarManager::reactMotionDizzy() {
  int lu=EAR_LEFT_UPRIGHT_ANGLE, ru=EAR_RIGHT_UPRIGHT_ANGLE;
  setStep(0, lu+12, ru-10, 110, EAR_EASE_FACTOR_FAST);
  setStep(1, lu-EAR_DIZZY_OFFSET, ru+26, 300, EAR_EASE_FACTOR_NORMAL);
  setStep(2, lu+26, ru-EAR_DIZZY_OFFSET, 320, EAR_EASE_FACTOR_NORMAL);
  setStep(3, lu-18, ru+EAR_DIZZY_OFFSET, 340, EAR_EASE_FACTOR_NORMAL);
  setStep(4, lu+EAR_DIZZY_OFFSET/2, ru-22, 850, EAR_EASE_FACTOR_SLOW);
  playSteps(5, lu, ru, EAR_EASE_FACTOR_SLOW);
}
void EarManager::reactMotionPickup() {
  int lu=EAR_LEFT_UPRIGHT_ANGLE, ru=EAR_RIGHT_UPRIGHT_ANGLE;
  setStep(0, lu-7, ru-7, 100, EAR_EASE_FACTOR_NORMAL);
  setStep(1, EAR_LEFT_PERK_ANGLE, ru, 180, EAR_EASE_FACTOR_NORMAL);
  setStep(2, EAR_LEFT_PERK_ANGLE, EAR_RIGHT_PERK_ANGLE, 180, EAR_EASE_FACTOR_NORMAL);
  setStep(3, lu, ru, 350, EAR_EASE_FACTOR_SLOW);
  playSteps(4, lu, ru, EAR_EASE_FACTOR_NORMAL);
}
void EarManager::reactMotionLanding() {
  int lu=EAR_LEFT_UPRIGHT_ANGLE, ru=EAR_RIGHT_UPRIGHT_ANGLE;
  setStep(0, lu-8, ru+8, 70, EAR_EASE_FACTOR_NORMAL);
  setStep(1, EAR_LEFT_DROOP_ANGLE + 12, EAR_RIGHT_DROOP_ANGLE - 12, 130, EAR_EASE_FACTOR_NORMAL);
  setStep(2, lu, ru, 380, EAR_EASE_FACTOR_NORMAL);
  playSteps(3, lu, ru, EAR_EASE_FACTOR_NORMAL);
}
void EarManager::reactMotionShock() {
  int lu=EAR_LEFT_UPRIGHT_ANGLE, ru=EAR_RIGHT_UPRIGHT_ANGLE;
  setStep(0, lu-10, ru+10, 45, EAR_EASE_FACTOR_NORMAL);
  setStep(1, EAR_LEFT_PERK_ANGLE+8, EAR_RIGHT_PERK_ANGLE-8, 130, EAR_EASE_FACTOR_NORMAL);
  setStep(2, EAR_LEFT_PERK_ANGLE+8, EAR_RIGHT_PERK_ANGLE-8, 220, EAR_EASE_FACTOR_SLOW);
  setStep(3, lu, ru, 300, EAR_EASE_FACTOR_SLOW);
  playSteps(4, lu, ru, EAR_EASE_FACTOR_NORMAL);
}


void EarManager::reactMotionWorried() {
  int lu = EAR_LEFT_UPRIGHT_ANGLE, ru = EAR_RIGHT_UPRIGHT_ANGLE;
  setStep(0, lu - 7, ru + 7, 100, EAR_EASE_FACTOR_FAST);
  setStep(1, EAR_LEFT_LOWERED_ANGLE, EAR_RIGHT_LOWERED_ANGLE, 260, EAR_EASE_FACTOR_NORMAL);
  setStep(2, EAR_LEFT_LOWERED_ANGLE, EAR_RIGHT_LOWERED_ANGLE, 700, EAR_EASE_FACTOR_SLOW);
  setStep(3, lu + 5, ru - 5, 180, EAR_EASE_FACTOR_NORMAL);
  setStep(4, lu, ru, 450, EAR_EASE_FACTOR_SLOW);
  playSteps(5, lu, ru, EAR_EASE_FACTOR_NORMAL);
}
// =======================================================================
// Expression -> ear reaction table. Deliberately sparse: only the
// "meaningful reaction" emotions from the V3 brief move the ears at all.
// Everything else (blinks, glances, most idle chatter, mode-switch faces,
// ...) falls through and leaves the ears exactly where they were — see
// the class comment for why. Only acts on an actual expression change, so
// it's safe (and expected) to call this every loop() while Ear Mode is on.
// =======================================================================
void EarManager::syncWithExpression(Expression e) {
  if (!enabled) return;
  if (overrideActive) return; // V5 physical reaction owns the ears until recovery completes
  if (hasLastExpr && e == lastReactedExpr) return;
  hasLastExpr = true;
  lastReactedExpr = e;

  switch (e) {
    case Expression::NORMAL:
      setAmbient(EAR_LEFT_UPRIGHT_ANGLE, EAR_RIGHT_UPRIGHT_ANGLE, EAR_EASE_FACTOR_NORMAL);
      break;

    case Expression::CURIOUS:
      // Asymmetric tilt.
      setAmbient(EAR_LEFT_TILT_FWD_ANGLE, EAR_RIGHT_UPRIGHT_ANGLE, EAR_EASE_FACTOR_NORMAL);
      break;

    case Expression::THINKING:
      // One ear tilted — the other of the two, so Curious and Thinking
      // read as distinct poses on the OLED and the ears alike.
      setAmbient(EAR_LEFT_UPRIGHT_ANGLE, EAR_RIGHT_TILT_FWD_ANGLE, EAR_EASE_FACTOR_NORMAL);
      break;

    case Expression::HAPPY:
    case Expression::HAPPY_SQUINT:
      playHappyWiggle();
      break;

    case Expression::EXCITED:
    case Expression::TINY_EXCITED:
      playExcitedWiggle();
      break;

    case Expression::SURPRISED:
    case Expression::SHOCK:
      playPerkUp();
      break;

    case Expression::SAD:
      setAmbient(EAR_LEFT_DROOP_ANGLE, EAR_RIGHT_DROOP_ANGLE, EAR_EASE_FACTOR_SLOW);
      break;

    case Expression::SLEEPY:
    case Expression::SLEEP:
    case Expression::FAKE_SLEEP:
      setAmbient(EAR_LEFT_DROOP_ANGLE, EAR_RIGHT_DROOP_ANGLE, EAR_EASE_FACTOR_SLOW);
      break;

    case Expression::ANGRY:
      setAmbient(EAR_LEFT_TILT_BACK_ANGLE, EAR_RIGHT_TILT_BACK_ANGLE, EAR_EASE_FACTOR_NORMAL);
      break;

    case Expression::SIDE_EYE:
    case Expression::SUSPICIOUS:
    case Expression::JUDGING:
      // Closest existing moods to the brief's "Suspicious" (there's no
      // dedicated SUSPICIOUS expression) — one ear slightly lowered.
      setAmbient(EAR_LEFT_LOWERED_ANGLE, EAR_RIGHT_UPRIGHT_ANGLE, EAR_EASE_FACTOR_NORMAL);
      break;

    case Expression::MISCHIEVOUS:
      // One up, one down.
      setAmbient(EAR_LEFT_UPRIGHT_ANGLE, EAR_RIGHT_DROOP_ANGLE, EAR_EASE_FACTOR_NORMAL);
      break;

    case Expression::DIZZY:
      playDizzyFlutter();
      break;

    case Expression::YAWN:
      playYawnDroop();
      break;

    case Expression::WORRIED:
    case Expression::SCARED:
      setAmbient(EAR_LEFT_LOWERED_ANGLE, EAR_RIGHT_LOWERED_ANGLE, EAR_EASE_FACTOR_NORMAL);
      break;

    case Expression::RELIEVED:
      playHappyWiggle();
      break;

    default:
      // Blinks, glances, bored/playful/wink/confused/laughing/rolling-eyes/
      // smug/annoyed/shy/attention/wake/stretch/firework, etc. — no ear
      // reaction. Deliberately not "allAmbientOff()"-style reset here:
      // resetting on every unmapped expression would itself be the kind
      // of constant twitch the brief asks to avoid.
      break;
  }
}

// =======================================================================
// Main update — advance the override engine if one is playing, ease each
// servo's actual angle toward its current target, and only write to the
// servo when the rounded angle actually changes.
// =======================================================================
void EarManager::update() {
  unsigned long now = millis();
  if (now - lastUpdateMs < EAR_UPDATE_INTERVAL_MS) return;
  lastUpdateMs = now;

  advanceOverride(now);

  // V5: subtle, deterministic "alive" micro-movement only while the ears
  // are enabled, idle in their normal upright pose, and no reaction owns them.
  if (enabled && !overrideActive && hasLastExpr && lastReactedExpr == Expression::NORMAL &&
      now - normalMicroStart >= EAR_MICRO_PERIOD_MS) {
    normalMicroStart = now;
    normalMicroSign = -normalMicroSign;
    setAmbient(
      constrain(EAR_LEFT_UPRIGHT_ANGLE + normalMicroSign * EAR_MICRO_OFFSET, 0, 180),
      constrain(EAR_RIGHT_UPRIGHT_ANGLE - normalMicroSign * EAR_MICRO_OFFSET, 0, 180),
      EAR_EASE_FACTOR_SLOW);
  }

  uint8_t targetL, targetR;
  float easeL, easeR;
  if (overrideActive) {
    const EarStep &st = stepBuf[stepIndex];
    targetL = st.leftAngle;
    targetR = st.rightAngle;
    easeL = st.ease;
    easeR = st.ease;
  } else {
    targetL = (uint8_t)left.ambientTarget;
    targetR = (uint8_t)right.ambientTarget;
    easeL = left.ambientEase;
    easeR = right.ambientEase;
  }

  EarChannel *channels[2] = { &left, &right };
  uint8_t targets[2] = { targetL, targetR };
  float eases[2] = { easeL, easeR };

  for (int i = 0; i < 2; i++) {
    EarChannel &ch = *channels[i];
    float diff = (float)targets[i] - ch.current;
    if (fabsf(diff) > 0.5f) {
      ch.current += diff * eases[i];
    } else {
      ch.current = targets[i];
    }

    int out = (int)roundf(ch.current);
    out = constrain(out, 0, 180);
    if (out != ch.lastWrittenAngle) {
      ch.servo.write(out);
      ch.lastWrittenAngle = out;
    }
  }
}
