#include "Animation.h"
#include "Config.h"
#include <Adafruit_SSD1306.h>
#include "MotionAnimations.h"

AnimationManager::AnimationManager(Character &c)
  : character(c),
    lastInteractionTime(0),
    asleep(false),
    sleepSequenceActive(false),
    sleepStep(0),
    sleepStepStart(0),
    sleepStepDuration(0),
    wakeSequenceActive(false),
    wakeStep(0),
    wakeStepStart(0),
    wakeStepDuration(0),
    activePriority(AnimPriority::IDLE),
    seq(nullptr),
    seqLen(0),
    seqIndex(0),
    stepStart(0),
    stepDuration(0),
    seqBusy(false),
    lastIdleIndex(-1),
    lastMicroIndex(-1),
    nextIdleAt(0),
    nextMicroAt(0),
    pendingSpecialBeep(false),
    bitmapAnimation(BitmapAnimation::NONE), bitmapPriority(255),
    bitmapFrameIndex(0), bitmapStartMs(0), bitmapLastFrameMs(0),
    bitmapDurationMs(0), bitmapFrameIntervalMs(100) {
  memset(bitmapFrameBuffer, 0, sizeof(bitmapFrameBuffer));
}

void AnimationManager::begin() {
  unsigned long now = millis();
  lastInteractionTime = now;
  scheduleNextIdleChange();
  scheduleNextMicro();
}

// =======================================================================
// Sleep — normal -> slow blink -> sleepy -> yawn -> eyelids close -> SLEEP
// (persistent, with Character's own breathing/zzz animation carrying it).
// =======================================================================
void AnimationManager::startSleepSequence() {
  asleep = true;              // full-display sleep visuals start right away
  sleepSequenceActive = true;
  sleepStep = 0;
  sleepStepStart = millis();
  sleepStepDuration = 260;    // slow blink
  character.setExpression(Expression::BLINK, 0);
}

void AnimationManager::updateSleepSequence() {
  unsigned long elapsed = millis() - sleepStepStart;
  if (elapsed < sleepStepDuration) return;

  sleepStep++;
  sleepStepStart = millis();
  switch (sleepStep) {
    case 1:
      sleepStepDuration = random(650, 950);
      character.setExpression(Expression::SLEEPY, 0);
      break;
    case 2:
      sleepStepDuration = random(500, 750);
      character.setExpression(Expression::YAWN, 0);
      break;
    case 3:
      sleepStepDuration = 400;
      character.setExpression(Expression::SLEEPY, 0);
      break;
    default:
      character.setExpression(Expression::SLEEP, 0);
      sleepSequenceActive = false;
      break;
  }
}

// =======================================================================
// Wake — closed -> tiny opening -> blink -> confused look-around ->
// surprised -> normal/happy settle.
// =======================================================================
void AnimationManager::startWakeSequence() {
  asleep = false;
  wakeSequenceActive = true;
  wakeStep = 0;
  wakeStepStart = millis();
  wakeStepDuration = 380; // eyes opening
  character.setExpression(Expression::WAKE, 0);
}

void AnimationManager::updateWakeSequence() {
  unsigned long elapsed = millis() - wakeStepStart;
  if (elapsed < wakeStepDuration) return;

  wakeStep++;
  wakeStepStart = millis();
  switch (wakeStep) {
    case 1:
      wakeStepDuration = 160;
      character.setExpression(Expression::BLINK, 0);
      break;
    case 2:
      wakeStepDuration = random(300, 500);
      character.setExpression(Expression::CONFUSED, 0);
      break;
    case 3:
      wakeStepDuration = random(220, 380);
      character.setExpression(Expression::LOOK_LEFT, 0);
      break;
    case 4:
      wakeStepDuration = random(220, 380);
      character.setExpression(Expression::LOOK_RIGHT, 0);
      break;
    case 5:
      wakeStepDuration = random(300, 500);
      character.setExpression(Expression::SURPRISED, 0);
      break;
    case 6:
      wakeStepDuration = random(500, 900);
      character.setExpression(Expression::HAPPY, 0);
      break;
    default:
      character.setExpression(Expression::NORMAL, 0);
      wakeSequenceActive = false;
      scheduleNextIdleChange();
      scheduleNextMicro();
      break;
  }
}

void AnimationManager::notifyInteraction() {
  lastInteractionTime = millis();
  if (asleep && !wakeSequenceActive) {
    startWakeSequence();
  }
}

// =======================================================================
// Generic sequence engine — INTERACTION / NOTIFICATION / SCREEN_REACTION
// / IDLE / MICRO tiers all flow through here. A higher-priority (lower
// number) request preempts whatever is currently playing; a lower-
// priority request is ignored so it can't stomp on something more
// important mid-beat.
// =======================================================================
bool AnimationManager::playSequence(const SeqStep *s, uint8_t len, AnimPriority pr) {
  if (asleep || wakeSequenceActive || bitmapAnimation != BitmapAnimation::NONE) return false; // special bitmap reactions own the face
  if (seqBusy && pr > activePriority) return false;
  seq = s;
  seqLen = len;
  seqIndex = 0;
  activePriority = pr;
  seqBusy = true;
  startStep();
  return true;
}

void AnimationManager::startStep() {
  stepStart = millis();
  const SeqStep &st = seq[seqIndex];
  stepDuration = (st.maxMs > st.minMs) ? random(st.minMs, st.maxMs) : st.minMs;
  character.setExpression(st.expr, 0); // AnimationManager drives the timing itself
}

void AnimationManager::advanceSequence(unsigned long now) {
  if (!seqBusy) return;
  if (now - stepStart < stepDuration) return;

  seqIndex++;
  if (seqIndex >= seqLen) {
    seqBusy = false;
    activePriority = AnimPriority::IDLE;
    character.setExpression(Expression::NORMAL, 0);
    scheduleNextIdleChange();
    scheduleNextMicro();
  } else {
    startStep();
  }
}

// =======================================================================
// Supplied full-frame motion reactions
// =======================================================================

bool AnimationManager::startBitmapAnimation(BitmapAnimation which, uint8_t priority,
                                             uint32_t durationMs, uint16_t frameIntervalMs) {
  if (asleep || wakeSequenceActive) return false;

  // Lower number = stronger motion reaction. Do not let a weaker event
  // restart an animation that is already on screen.
  if (bitmapAnimation != BitmapAnimation::NONE && priority >= bitmapPriority) return false;

  bitmapAnimation = which;
  bitmapPriority = priority;
  bitmapFrameIndex = 0;
  bitmapStartMs = millis();
  bitmapLastFrameMs = bitmapStartMs;
  bitmapDurationMs = durationMs;
  bitmapFrameIntervalMs = frameIntervalMs;

  // A full-frame reaction owns the face, so stop any expression sequence
  // cleanly rather than allowing two animation systems to fight.
  seqBusy = false;
  activePriority = AnimPriority::IDLE;
  loadBitmapFrame();
  return true;
}

void AnimationManager::loadBitmapFrame() {
  const uint16_t *offsets = nullptr;
  const uint8_t *data = nullptr;
  uint16_t frameCount = 0;

  switch (bitmapAnimation) {
    case BitmapAnimation::IDIOT:
      offsets = IDIOT_FRAME_OFFSETS;
      data = IDIOT_RLE_DATA;
      frameCount = IDIOT_FRAME_COUNT;
      break;
    case BitmapAnimation::STUPID:
      offsets = STUPID_FRAME_OFFSETS;
      data = STUPID_RLE_DATA;
      frameCount = STUPID_FRAME_COUNT;
      break;
    case BitmapAnimation::DIZZY:
      offsets = DIZZY_FRAME_OFFSETS;
      data = DIZZY_RLE_DATA;
      frameCount = DIZZY_FRAME_COUNT;
      break;
    default:
      return;
  }

  uint16_t start = pgm_read_word(&offsets[bitmapFrameIndex]);
  uint16_t end = pgm_read_word(&offsets[bitmapFrameIndex + 1]);
  uint16_t out = 0;
  for (uint16_t i = start; i + 1 < end && out < sizeof(bitmapFrameBuffer); i += 2) {
    uint8_t count = pgm_read_byte(&data[i]);
    uint8_t value = pgm_read_byte(&data[i + 1]);
    uint16_t room = (uint16_t)sizeof(bitmapFrameBuffer) - out;
    uint8_t n = (count > room) ? (uint8_t)room : count;
    memset(bitmapFrameBuffer + out, value, n);
    out += n;
  }

  // Guard against a malformed/partial generated frame without affecting
  // normal animation playback.
  if (out < sizeof(bitmapFrameBuffer)) {
    memset(bitmapFrameBuffer + out, 0, sizeof(bitmapFrameBuffer) - out);
  }

  (void)frameCount;
}

void AnimationManager::updateBitmapAnimation(unsigned long now) {
  if (bitmapAnimation == BitmapAnimation::NONE) return;

  if (now - bitmapStartMs >= bitmapDurationMs) {
    bitmapAnimation = BitmapAnimation::NONE;
    bitmapPriority = 255;
    seqBusy = false;
    activePriority = AnimPriority::IDLE;
    character.setExpression(Expression::NORMAL, 0);
    scheduleNextIdleChange();
    scheduleNextMicro();
    return;
  }

  if (now - bitmapLastFrameMs < bitmapFrameIntervalMs) return;

  bitmapLastFrameMs = now;
  uint16_t frameCount = 0;
  switch (bitmapAnimation) {
    case BitmapAnimation::IDIOT: frameCount = IDIOT_FRAME_COUNT; break;
    case BitmapAnimation::STUPID: frameCount = STUPID_FRAME_COUNT; break;
    case BitmapAnimation::DIZZY: frameCount = DIZZY_FRAME_COUNT; break;
    default: return;
  }

  bitmapFrameIndex++;
  if (bitmapFrameIndex >= frameCount) bitmapFrameIndex = 0;
  loadBitmapFrame();
}

bool AnimationManager::isBitmapAnimationActive() const {
  return bitmapAnimation != BitmapAnimation::NONE;
}

void AnimationManager::drawBitmapAnimation(Adafruit_SSD1306 &display) {
  if (bitmapAnimation == BitmapAnimation::NONE) return;
  display.clearDisplay();
  display.drawBitmap(0, 0, bitmapFrameBuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.display();
}

void AnimationManager::reactMotionIdiot() {
  startBitmapAnimation(BitmapAnimation::IDIOT, 2, MOTION_IDIOT_DURATION_MS, 100);
}

void AnimationManager::reactMotionStupid() {
  startBitmapAnimation(BitmapAnimation::STUPID, 3, MOTION_STUPID_DURATION_MS, 100);
}

void AnimationManager::reactMotionDizzy() {
  startBitmapAnimation(BitmapAnimation::DIZZY, 1, MOTION_DIZZY_DURATION_MS, 50);
}

// ---- Public one-shot / named reactions --------------------------------

void AnimationManager::suggestMood(Expression e, unsigned long holdMs) {
  adhocStep[0] = { e, (uint16_t)holdMs, (uint16_t)holdMs };
  playSequence(adhocStep, 1, AnimPriority::SCREEN_REACTION);
}

static const SeqStep BUTTON_SHORT_A[] = {
  { Expression::ATTENTION, 120, 180 },
  { Expression::CURIOUS,   150, 260 },
};
static const SeqStep BUTTON_SHORT_B[] = {
  { Expression::ATTENTION, 120, 180 },
  { Expression::HAPPY,     150, 260 },
};
void AnimationManager::reactButtonShort() {
  // A touch should never feel like the same canned response. Pick a small
  // weighted family of reference-style emotions and give each a tiny
  // anticipation beat. This is deliberately event-driven, not a constant
  // random loop.
  static const SeqStep TOUCH_CURIOUS[] = {
    { Expression::ATTENTION, 100, 150 },
    { Expression::CURIOUS, 450, 700 },
    { Expression::GLEE, 280, 420 }
  };
  static const SeqStep TOUCH_SKEPTIC[] = {
    { Expression::ATTENTION, 100, 150 },
    { Expression::SKEPTIC, 500, 800 },
    { Expression::SIDE_EYE, 300, 500 }
  };
  static const SeqStep TOUCH_HAPPY[] = {
    { Expression::ATTENTION, 100, 150 },
    { Expression::HAPPY, 500, 850 },
    { Expression::GLEE, 300, 450 }
  };
  static const SeqStep TOUCH_SURPRISE[] = {
    { Expression::ATTENTION, 100, 150 },
    { Expression::SURPRISED, 320, 500 },
    { Expression::AWE, 400, 650 }
  };
  static const SeqStep TOUCH_ANNOYED[] = {
    { Expression::ATTENTION, 100, 150 },
    { Expression::UNIMPRESSED, 450, 700 },
    { Expression::ANNOYED, 300, 500 }
  };

  int pick = random(0, 100);
  if (pick < 34) {
    playSequence(TOUCH_CURIOUS, 3, AnimPriority::INTERACTION);
  } else if (pick < 55) {
    playSequence(TOUCH_HAPPY, 3, AnimPriority::INTERACTION);
  } else if (pick < 73) {
    playSequence(TOUCH_SKEPTIC, 3, AnimPriority::INTERACTION);
  } else if (pick < 88) {
    playSequence(TOUCH_SURPRISE, 3, AnimPriority::INTERACTION);
  } else {
    playSequence(TOUCH_ANNOYED, 3, AnimPriority::INTERACTION);
  }
}

static const SeqStep BUTTON_LONG_SEQ[] = {
  { Expression::ATTENTION, 120, 180 },
  { Expression::CURIOUS,   300, 500 },
  { Expression::SMUG,      400, 650 },
};
void AnimationManager::reactButtonLong() {
  playSequence(BUTTON_LONG_SEQ, sizeof(BUTTON_LONG_SEQ) / sizeof(SeqStep), AnimPriority::INTERACTION);
}

static const SeqStep UNLOCK_SEQ[] = {
  { Expression::CONFUSED, 250, 400 },
  { Expression::HAPPY,    300, 500 },
};
void AnimationManager::reactUnlock() {
  playSequence(UNLOCK_SEQ, sizeof(UNLOCK_SEQ) / sizeof(SeqStep), AnimPriority::INTERACTION);
}

static const SeqStep REMINDER_SEQ[] = {
  { Expression::ATTENTION, 150, 220 },
  { Expression::SURPRISED, 300, 500 },
};
void AnimationManager::reactReminder() {
  playSequence(REMINDER_SEQ, sizeof(REMINDER_SEQ) / sizeof(SeqStep), AnimPriority::NOTIFICATION);
}

static const SeqStep WEATHER_GOOD_SEQ[] = {
  { Expression::HAPPY,   150, 250 },
  { Expression::EXCITED, 500, 900 },
  { Expression::HAPPY,   300, 500 },
};
void AnimationManager::reactWeatherGood() {
  playSequence(WEATHER_GOOD_SEQ, sizeof(WEATHER_GOOD_SEQ) / sizeof(SeqStep), AnimPriority::SCREEN_REACTION);
}

static const SeqStep WEATHER_RAIN_SEQ[] = {
  { Expression::SAD, 150, 250 },
  { Expression::SAD, 600, 1000 },
};
void AnimationManager::reactWeatherRain() {
  playSequence(WEATHER_RAIN_SEQ, sizeof(WEATHER_RAIN_SEQ) / sizeof(SeqStep), AnimPriority::SCREEN_REACTION);
}

static const SeqStep WEATHER_HOT_SEQ[] = {
  { Expression::ANNOYED, 150, 250 },
  { Expression::ANNOYED, 600, 1000 },
};
void AnimationManager::reactWeatherHot() {
  playSequence(WEATHER_HOT_SEQ, sizeof(WEATHER_HOT_SEQ) / sizeof(SeqStep), AnimPriority::SCREEN_REACTION);
}

static const SeqStep AI_THINKING_SEQ[] = {
  { Expression::THINKING, 150, 250 },
  { Expression::THINKING, 700, 1300 },
};
void AnimationManager::reactAIThinking() {
  playSequence(AI_THINKING_SEQ, sizeof(AI_THINKING_SEQ) / sizeof(SeqStep), AnimPriority::SCREEN_REACTION);
}

static const SeqStep AI_RESPONSE_SEQ[] = {
  { Expression::CURIOUS, 150, 200 },
  { Expression::HAPPY,   350, 600 },
};
void AnimationManager::reactAIResponse() {
  playSequence(AI_RESPONSE_SEQ, sizeof(AI_RESPONSE_SEQ) / sizeof(SeqStep), AnimPriority::SCREEN_REACTION);
}

static const SeqStep AI_ERROR_SEQ[] = {
  { Expression::CONFUSED, 150, 250 },
  { Expression::SAD,      450, 800 },
};
void AnimationManager::reactAIError() {
  playSequence(AI_ERROR_SEQ, sizeof(AI_ERROR_SEQ) / sizeof(SeqStep), AnimPriority::SCREEN_REACTION);
}

// ---- V2: Mode 1 <-> Mode 2 confirmation ------------------------------
static const SeqStep MODE_ENTER_SEQ[] = {
  { Expression::SURPRISED, 200, 320 },
  { Expression::HAPPY,     350, 550 },
};
void AnimationManager::reactModeEnter() {
  playSequence(MODE_ENTER_SEQ, sizeof(MODE_ENTER_SEQ) / sizeof(SeqStep), AnimPriority::INTERACTION);
}

static const SeqStep MODE_EXIT_SEQ[] = {
  { Expression::WINK,  220, 320 },
  { Expression::BLINK, 120, 180 },
};
void AnimationManager::reactModeExit() {
  playSequence(MODE_EXIT_SEQ, sizeof(MODE_EXIT_SEQ) / sizeof(SeqStep), AnimPriority::INTERACTION);
}

// ---- V5: physical motion reactions ------------------------------------
// Every major physical reaction is deliberately a little theatrical:
// anticipation -> big action -> hold -> secondary beat -> recovery.
static const SeqStep MOTION_TAP_SEQ[] = {
  { Expression::ATTENTION, 100, 150 },
  { Expression::SURPRISED, 180, 260 },
  { Expression::CONFUSED,  420, 620 },
  { Expression::NORMAL,    300, 450 },
};
static const SeqStep MOTION_FRONT_SEQ[] = {
  { Expression::CURIOUS,   160, 240 },
  { Expression::LOOK_DOWN, 350, 500 },
  { Expression::LOOK_DOWN, 650, 900 },
  { Expression::WORRIED,   250, 380 },
  { Expression::NORMAL,    450, 650 },
};
static const SeqStep MOTION_BACK_SEQ[] = {
  { Expression::CURIOUS, 160, 240 },
  { Expression::LOOK_UP, 350, 500 },
  { Expression::LOOK_UP, 650, 900 },
  { Expression::HAPPY,   250, 380 },
  { Expression::NORMAL,  450, 650 },
};
static const SeqStep MOTION_LEFT_SEQ[] = {
  { Expression::CURIOUS,   150, 220 },
  { Expression::LOOK_LEFT, 350, 500 },
  { Expression::LOOK_LEFT, 650, 900 },
  { Expression::SIDE_EYE,  280, 420 },
  { Expression::NORMAL,    450, 650 },
};
static const SeqStep MOTION_RIGHT_SEQ[] = {
  { Expression::CURIOUS,    150, 220 },
  { Expression::LOOK_RIGHT, 350, 500 },
  { Expression::LOOK_RIGHT, 650, 900 },
  { Expression::SIDE_EYE,   280, 420 },
  { Expression::NORMAL,     450, 650 },
};
static const SeqStep MOTION_SHAKE_1_SEQ[] = {
  { Expression::CURIOUS, 120, 180 },
  { Expression::CONFUSED, 700, 950 },
  { Expression::SUSPICIOUS, 400, 650 },
  { Expression::NORMAL, 450, 650 },
};
static const SeqStep MOTION_SHAKE_2_SEQ[] = {
  { Expression::ANNOYED, 180, 260 },
  { Expression::SIDE_EYE, 700, 1000 },
  { Expression::ANNOYED, 500, 750 },
  { Expression::NORMAL, 500, 700 },
};
static const SeqStep MOTION_SHAKE_3_SEQ[] = {
  { Expression::SHOCK,   160, 240 },
  { Expression::DIZZY,  2400, 3200 },
  { Expression::CONFUSED, 650, 950 },
  { Expression::RELIEVED, 450, 700 },
  { Expression::NORMAL, 500, 750 },
};
static const SeqStep MOTION_DIZZY_SEQ[] = {
  { Expression::SHOCK, 150, 230 },
  { Expression::DIZZY, 2600, 3400 },
  { Expression::CONFUSED, 700, 1000 },
  { Expression::RELIEVED, 450, 700 },
  { Expression::NORMAL, 500, 750 },
};
static const SeqStep MOTION_PICKUP_SEQ[] = {
  { Expression::LOOK_UP, 180, 260 },
  { Expression::SURPRISED, 350, 500 },
  { Expression::HAPPY, 700, 1000 },
  { Expression::EXCITED, 450, 700 },
  { Expression::NORMAL, 500, 750 },
};
static const SeqStep MOTION_LANDING_SEQ[] = {
  { Expression::SHOCK, 120, 180 },
  { Expression::CONFUSED, 350, 500 },
  { Expression::RELIEVED, 450, 650 },
  { Expression::HAPPY, 350, 550 },
  { Expression::NORMAL, 500, 700 },
};
static const SeqStep MOTION_WAKE_SEQ[] = {
  { Expression::WAKE, 400, 520 },
  { Expression::CONFUSED, 350, 500 },
  { Expression::LOOK_LEFT, 280, 420 },
  { Expression::LOOK_RIGHT, 280, 420 },
  { Expression::SURPRISED, 350, 500 },
  { Expression::HAPPY, 500, 800 },
};
static const SeqStep MOTION_WORRIED_SEQ[] = {
  { Expression::SCARED, 180, 260 },
  { Expression::WORRIED, 900, 1250 },
  { Expression::CONFUSED, 550, 800 },
  { Expression::RELIEVED, 450, 650 },
  { Expression::NORMAL, 500, 700 },
};

void AnimationManager::reactMotionTap()       { playSequence(MOTION_TAP_SEQ, sizeof(MOTION_TAP_SEQ)/sizeof(SeqStep), AnimPriority::NOTIFICATION); }
void AnimationManager::reactMotionTiltFront() { playSequence(MOTION_FRONT_SEQ, sizeof(MOTION_FRONT_SEQ)/sizeof(SeqStep), AnimPriority::SCREEN_REACTION); }
void AnimationManager::reactMotionTiltBack()  { playSequence(MOTION_BACK_SEQ, sizeof(MOTION_BACK_SEQ)/sizeof(SeqStep), AnimPriority::SCREEN_REACTION); }
void AnimationManager::reactMotionTiltLeft()  { playSequence(MOTION_LEFT_SEQ, sizeof(MOTION_LEFT_SEQ)/sizeof(SeqStep), AnimPriority::SCREEN_REACTION); }
void AnimationManager::reactMotionTiltRight() { playSequence(MOTION_RIGHT_SEQ, sizeof(MOTION_RIGHT_SEQ)/sizeof(SeqStep), AnimPriority::SCREEN_REACTION); }
void AnimationManager::reactMotionShake(uint8_t level) {
  if (level >= 3) playSequence(MOTION_SHAKE_3_SEQ, sizeof(MOTION_SHAKE_3_SEQ)/sizeof(SeqStep), AnimPriority::NOTIFICATION);
  else if (level == 2) playSequence(MOTION_SHAKE_2_SEQ, sizeof(MOTION_SHAKE_2_SEQ)/sizeof(SeqStep), AnimPriority::NOTIFICATION);
  else playSequence(MOTION_SHAKE_1_SEQ, sizeof(MOTION_SHAKE_1_SEQ)/sizeof(SeqStep), AnimPriority::NOTIFICATION);
}
void AnimationManager::reactMotionShock()  { playSequence(MOTION_WORRIED_SEQ, sizeof(MOTION_WORRIED_SEQ)/sizeof(SeqStep), AnimPriority::NOTIFICATION); }
void AnimationManager::reactMotionPickup() { playSequence(MOTION_PICKUP_SEQ, sizeof(MOTION_PICKUP_SEQ)/sizeof(SeqStep), AnimPriority::NOTIFICATION); }
void AnimationManager::reactMotionLanding(){ playSequence(MOTION_LANDING_SEQ, sizeof(MOTION_LANDING_SEQ)/sizeof(SeqStep), AnimPriority::NOTIFICATION); }
void AnimationManager::reactMotionWake()   { playSequence(MOTION_WAKE_SEQ, sizeof(MOTION_WAKE_SEQ)/sizeof(SeqStep), AnimPriority::WAKE_SLEEP); }
void AnimationManager::reactMotionWorried(){ playSequence(MOTION_WORRIED_SEQ, sizeof(MOTION_WORRIED_SEQ)/sizeof(SeqStep), AnimPriority::NOTIFICATION); }

// =======================================================================
// Idle personality — the reference project has 18 distinct emotional
// presets. We keep all 18 in the existing AnimationManager and let a
// weighted roulette choose a short sequence. The character spends most of
// its time resting, but it periodically looks around, blinks, changes mood,
// yawns, or does a rare personality beat. Immediate repeats are avoided.
// =======================================================================
static const SeqStep IDLE_NORMAL[] = {
  { Expression::NORMAL, 900, 1700 }
};
static const SeqStep IDLE_LOOK_AROUND[] = {
  { Expression::LOOK_LEFT, 220, 360 },
  { Expression::NORMAL, 120, 220 },
  { Expression::LOOK_RIGHT, 260, 420 },
  { Expression::NORMAL, 250, 420 }
};
static const SeqStep IDLE_CURIOUS[] = {
  { Expression::LOOK_RIGHT, 180, 300 },
  { Expression::CURIOUS, 600, 1000 },
  { Expression::LOOK_UP, 220, 360 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_GLEE[] = {
  { Expression::CURIOUS, 160, 260 },
  { Expression::GLEE, 450, 750 },
  { Expression::HAPPY, 350, 600 }
};
static const SeqStep IDLE_HAPPY[] = {
  { Expression::HAPPY, 650, 1100 },
  { Expression::GLEE, 250, 450 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_SAD[] = {
  { Expression::SAD, 550, 950 },
  { Expression::RELIEVED, 300, 500 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_WORRIED[] = {
  { Expression::WORRIED, 550, 900 },
  { Expression::LOOK_LEFT, 180, 280 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_FOCUSED[] = {
  { Expression::LOOK_UP, 180, 280 },
  { Expression::FOCUSED, 650, 1050 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_ANNOYED[] = {
  { Expression::UNIMPRESSED, 400, 650 },
  { Expression::ANNOYED, 500, 850 },
  { Expression::NORMAL, 300, 500 }
};
static const SeqStep IDLE_SURPRISED[] = {
  { Expression::CURIOUS, 180, 280 },
  { Expression::SURPRISED, 350, 550 },
  { Expression::AWE, 350, 600 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_SKEPTIC[] = {
  { Expression::LOOK_LEFT, 180, 280 },
  { Expression::SKEPTIC, 550, 900 },
  { Expression::SIDE_EYE, 300, 500 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_FRUSTRATED[] = {
  { Expression::FOCUSED, 200, 300 },
  { Expression::FRUSTRATED, 500, 800 },
  { Expression::UNIMPRESSED, 300, 500 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_UNIMPRESSED[] = {
  { Expression::UNIMPRESSED, 550, 900 },
  { Expression::SIDE_EYE, 300, 500 },
  { Expression::NORMAL, 300, 500 }
};
static const SeqStep IDLE_SLEEPY[] = {
  { Expression::SLEEPY, 650, 1050 },
  { Expression::YAWN, 500, 800 },
  { Expression::SLEEPY, 400, 650 }
};
static const SeqStep IDLE_SUSPICIOUS[] = {
  { Expression::LOOK_LEFT, 180, 260 },
  { Expression::LOOK_RIGHT, 180, 260 },
  { Expression::SUSPICIOUS, 550, 900 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_SQUINT[] = {
  { Expression::SQUINT, 500, 800 },
  { Expression::LOOK_RIGHT, 220, 340 },
  { Expression::NORMAL, 300, 500 }
};
static const SeqStep IDLE_FURIOUS[] = {
  { Expression::ANGRY, 220, 320 },
  { Expression::FURIOUS, 450, 700 },
  { Expression::RELIEVED, 300, 500 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_SCARED[] = {
  { Expression::SCARED, 350, 550 },
  { Expression::CONFUSED, 350, 550 },
  { Expression::RELIEVED, 350, 550 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_AWE[] = {
  { Expression::AWE, 650, 1000 },
  { Expression::HAPPY, 300, 500 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_PLAYFUL[] = {
  { Expression::PLAYFUL, 500, 850 },
  { Expression::WINK, 180, 300 },
  { Expression::MISCHIEVOUS, 450, 750 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_THINKING[] = {
  { Expression::LOOK_UP, 180, 300 },
  { Expression::THINKING, 650, 1100 },
  { Expression::FOCUSED, 350, 550 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_WINK[] = {
  { Expression::WINK, 220, 340 },
  { Expression::SMUG, 450, 700 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_ROLLING[] = {
  { Expression::ROLLING_EYES, 600, 950 },
  { Expression::CONFUSED, 300, 500 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_SHY[] = {
  { Expression::SHY, 550, 900 },
  { Expression::LOOK_DOWN, 220, 340 },
  { Expression::NORMAL, 300, 500 }
};
static const SeqStep IDLE_MISCHIEVOUS[] = {
  { Expression::MISCHIEVOUS, 450, 750 },
  { Expression::SIDE_EYE, 250, 400 },
  { Expression::HAPPY, 300, 500 }
};
static const SeqStep IDLE_DOUBLE_BLINK[] = {
  { Expression::BLINK, 120, 180 },
  { Expression::NORMAL, 140, 240 },
  { Expression::BLINK, 120, 180 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep IDLE_YAWN[] = {
  { Expression::YAWN, 550, 850 },
  { Expression::SLEEPY, 450, 700 },
  { Expression::NORMAL, 300, 500 }
};

// Rare events remain rare, but now use the same richer reference-style
// vocabulary instead of falling back to NORMAL too quickly.
static const SeqStep EVENT_FIREWORK[] = {
  { Expression::CURIOUS, 150, 250 },
  { Expression::LOOK_UP, 150, 250 },
  { Expression::AWE, 450, 650 },
  { Expression::FIREWORK, 850, 1300 },
  { Expression::HAPPY, 400, 700 }
};
static const SeqStep EVENT_SURPRISE[] = {
  { Expression::NORMAL, 180, 300 },
  { Expression::SHOCK, 350, 550 },
  { Expression::CONFUSED, 350, 550 },
  { Expression::NORMAL, 250, 450 }
};
static const SeqStep EVENT_I_SAW_THAT[] = {
  { Expression::LOOK_LEFT, 200, 320 },
  { Expression::SKEPTIC, 500, 800 },
  { Expression::SIDE_EYE, 350, 550 },
  { Expression::NORMAL, 200, 350 }
};
static const SeqStep EVENT_HAPPY_DANCE[] = {
  { Expression::EXCITED, 180, 280 },
  { Expression::GLEE, 250, 400 },
  { Expression::EXCITED, 180, 280 },
  { Expression::HAPPY, 350, 550 }
};

struct IdleBehavior {
  const SeqStep *seq;
  uint8_t len;
  uint8_t weight;
  bool special;
};

static const IdleBehavior IDLE_POOL[] = {
  { IDLE_NORMAL,        1, 13, false },
  { IDLE_LOOK_AROUND,   4, 9,  false },
  { IDLE_CURIOUS,       4, 8,  false },
  { IDLE_GLEE,          3, 5,  false },
  { IDLE_HAPPY,         3, 5,  false },
  { IDLE_SAD,           3, 2,  false },
  { IDLE_WORRIED,       3, 3,  false },
  { IDLE_FOCUSED,       3, 4,  false },
  { IDLE_ANNOYED,       3, 3,  false },
  { IDLE_SURPRISED,     4, 3,  false },
  { IDLE_SKEPTIC,       4, 5,  false },
  { IDLE_FRUSTRATED,    4, 2,  false },
  { IDLE_UNIMPRESSED,   3, 4,  false },
  { IDLE_SLEEPY,        3, 3,  false },
  { IDLE_SUSPICIOUS,    4, 4,  false },
  { IDLE_SQUINT,        3, 2,  false },
  { IDLE_FURIOUS,       4, 1,  false },
  { IDLE_SCARED,        4, 1,  false },
  { IDLE_AWE,           3, 3,  false },
  { IDLE_PLAYFUL,       4, 4,  false },
  { IDLE_THINKING,      4, 5,  false },
  { IDLE_WINK,          3, 3,  false },
  { IDLE_ROLLING,       3, 2,  false },
  { IDLE_SHY,           3, 2,  false },
  { IDLE_MISCHIEVOUS,   3, 3,  false },
  { IDLE_DOUBLE_BLINK,  4, 5,  false },
  { IDLE_YAWN,          3, 2,  false },
  { EVENT_FIREWORK,     5, 1,  true },
  { EVENT_SURPRISE,     4, 1,  true },
  { EVENT_I_SAW_THAT,   4, 2,  true },
  { EVENT_HAPPY_DANCE,  4, 1,  true },
};
static const int IDLE_POOL_SIZE = sizeof(IDLE_POOL) / sizeof(IDLE_POOL[0]);

// A smaller, subtler pool for the even-rarer "just a tiny twitch" layer.
static const SeqStep MICRO_TWITCH[]  = { { Expression::LOOK_LEFT, 60, 110 } };
static const SeqStep MICRO_GLANCE[]  = { { Expression::LOOK_RIGHT, 140, 280 } };
static const SeqStep MICRO_UP_PAUSE[] = {
  { Expression::LOOK_UP, 250, 500 },
  { Expression::NORMAL,  120, 220 },
};
static const SeqStep MICRO_SHAKE[] = {
  { Expression::LOOK_LEFT,  60, 100 },
  { Expression::LOOK_RIGHT, 60, 100 },
};
static const SeqStep MICRO_FREEZE[] = { { Expression::NORMAL, 1400, 2200 } };

static const IdleBehavior MICRO_POOL[] = {
  { MICRO_TWITCH,   1, 5 },
  { MICRO_GLANCE,   1, 5 },
  { MICRO_UP_PAUSE, 2, 4 },
  { MICRO_SHAKE,    2, 3 },
  { MICRO_FREEZE,   1, 3 },
};
static const int MICRO_POOL_SIZE = sizeof(MICRO_POOL) / sizeof(MICRO_POOL[0]);

static int pickWeightedIndex(const IdleBehavior *pool, int size, int8_t avoidIndex) {
  int totalWeight = 0;
  for (int i = 0; i < size; i++) totalWeight += pool[i].weight;

  int pick = -1;
  for (int tries = 0; tries < 4; tries++) {
    long r = random(0, totalWeight);
    int idx = 0;
    for (; idx < size; idx++) {
      r -= pool[idx].weight;
      if (r < 0) break;
    }
    if (idx >= size) idx = size - 1;
    pick = idx;
    if (pick != avoidIndex || size <= 1) break; // avoid immediate repetition
  }
  return pick;
}

void AnimationManager::scheduleNextIdleChange() {
  nextIdleAt = millis() + random(IDLE_MIN_CHANGE_MS, IDLE_MAX_CHANGE_MS);
}

void AnimationManager::scheduleNextMicro() {
  nextMicroAt = millis() + random(MICRO_MIN_CHANGE_MS, MICRO_MAX_CHANGE_MS);
}

void AnimationManager::pickAndPlayIdle() {
  int idx = pickWeightedIndex(IDLE_POOL, IDLE_POOL_SIZE, lastIdleIndex);
  lastIdleIndex = (int8_t)idx;
  const IdleBehavior &b = IDLE_POOL[idx];
  if (playSequence(b.seq, b.len, AnimPriority::IDLE) && b.special) {
    pendingSpecialBeep = true;
  }
}

bool AnimationManager::consumeSpecialBeep() {
  if (!pendingSpecialBeep) return false;
  pendingSpecialBeep = false;
  return true;
}

void AnimationManager::pickAndPlayMicro() {
  int idx = pickWeightedIndex(MICRO_POOL, MICRO_POOL_SIZE, lastMicroIndex);
  lastMicroIndex = (int8_t)idx;
  const IdleBehavior &b = MICRO_POOL[idx];
  playSequence(b.seq, b.len, AnimPriority::MICRO);
}

// =======================================================================
// Main update — advances whichever tier currently owns the face, then
// (only once nothing is busy) checks the sleep timeout and spontaneous
// idle/micro scheduling.
// =======================================================================
void AnimationManager::update(bool allowSleep) {
  unsigned long now = millis();

  if (bitmapAnimation != BitmapAnimation::NONE) {
    updateBitmapAnimation(now);
    return;
  }

  if (wakeSequenceActive) {
    updateWakeSequence();
    return;
  }

  if (asleep) {
    if (sleepSequenceActive) updateSleepSequence();
    return; // Character::draw() renders SLEEP full-screen the whole time.
  }

  advanceSequence(now);

  if (!seqBusy) {
    // Fall asleep after prolonged inactivity (unless a screen is locked).
    if (allowSleep && (now - lastInteractionTime) >= SLEEP_TIMEOUT_MS) {
      startSleepSequence();
      return;
    }

    if (now >= nextIdleAt) {
      pickAndPlayIdle();
    } else if (now >= nextMicroAt) {
      pickAndPlayMicro();
    }
  }
}
