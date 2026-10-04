#pragma once
#include "Character.h"

class Adafruit_SSD1306;

// Internal priority tiers for the animation engine. Lower number wins —
// a request can preempt whatever is currently playing at an equal or
// lower-priority (higher number) tier, but cannot interrupt something
// more important than itself. This is the "priority system" from the
// design brief:
//   1. Wake / sleep            (handled as its own tiny state machine)
//   2. User interaction        -> AnimPriority::INTERACTION
//   3. Important notification  -> AnimPriority::NOTIFICATION
//   4. Screen reaction         -> AnimPriority::SCREEN_REACTION
//   5. Idle personality        -> AnimPriority::IDLE
//   6. Micro animation         -> AnimPriority::MICRO
enum class AnimPriority : uint8_t {
  WAKE_SLEEP = 0,
  INTERACTION = 1,
  NOTIFICATION = 2,
  SCREEN_REACTION = 3,
  IDLE = 4,
  MICRO = 5
};

// One beat of a multi-step animation: hold `expr` for a randomized
// duration between minMs and maxMs (pass minMs == maxMs for a fixed
// duration). Chaining a few of these is how "anticipation -> action ->
// reaction -> recovery" gets built out of the small Expression vocabulary.
struct SeqStep {
  Expression expr;
  uint16_t minMs;
  uint16_t maxMs;
};

// Drives the character's entire moment-to-moment personality: spontaneous
// idle wandering + micro-behaviours with weighted randomness and "never
// repeat immediately", falling asleep after inactivity with an expressive
// transition, the wake-up sequence, and short reactive sequences that
// screens/systems can request (button press, weather, AI, reminders...).
//
// AnimationManager OWNS the character's animation state. ScreenManager
// only ever *suggests* a reaction (suggestMood() / reactXxx()) — it never
// pokes Character::setExpression() directly, so screen changes can never
// cut an in-flight animation off mid-beat in an ugly way. The face keeps
// animating continuously no matter which info screen is showing, or while
// the screen is locked (allowSleep=false keeps the character gently alive
// instead of letting it fall asleep and take over the whole display).
class AnimationManager {
public:
  explicit AnimationManager(Character &c);

  void begin();
  // allowSleep is false while a screen is locked — locked mode promises
  // "information remains visible", so the character stays gently alive
  // (blink/breathing/idle) instead of taking over the whole display to sleep.
  void update(bool allowSleep = true);

  // Called on every button press (short or long) — resets the sleep
  // timer and, if the character was asleep, kicks off the wake sequence.
  void notifyInteraction();

  bool isAsleep() const { return asleep; }

  // ---- Generic one-shot mood suggestion (SCREEN_REACTION tier) --------
  // Used by simple screens that just want a brief, single mood.
  void suggestMood(Expression e, unsigned long holdMs);

  // ---- Named reactions (design-brief "reaction system") ---------------
  // Each plays a short anticipation->action->recovery sequence at the
  // named priority tier, then hands control back to idle wandering.
  void reactButtonShort();     // INTERACTION: notice -> curious/happy
  void reactButtonLong();      // INTERACTION: notice -> curious -> smug (locking)
  void reactUnlock();          // INTERACTION: confused -> happy
  void reactReminder();        // NOTIFICATION: attention -> surprised
  void reactWeatherGood();     // SCREEN_REACTION: happy -> excited -> happy
  void reactWeatherRain();     // SCREEN_REACTION: sad
  void reactWeatherHot();      // SCREEN_REACTION: annoyed
  void reactAIThinking();      // SCREEN_REACTION: thinking (processing)
  void reactAIResponse();      // SCREEN_REACTION: curious -> happy
  void reactAIError();         // SCREEN_REACTION: confused -> sad

  // V2 — Mode 1 <-> Mode 2 double-press confirmation (INTERACTION tier).
  void reactModeEnter();       // entering LED_CHARACTER mode: surprised -> happy
  void reactModeExit();        // back to NORMAL mode: small wink/blink
  // V5 — non-blocking physical-motion reactions
  void reactMotionTap();
  void reactMotionTiltFront();
  void reactMotionTiltBack();
  void reactMotionTiltLeft();
  void reactMotionTiltRight();
  void reactMotionShake(uint8_t level);
  void reactMotionDizzy();
  void reactMotionShock();
  void reactMotionPickup();
  void reactMotionLanding();
  // V5 — supplied full-frame motion reactions, integrated into the same
  // AnimationManager/state machine. They preempt ordinary expression sequences
  // but return control to the existing normal animation automatically.
  void reactMotionIdiot();
  void reactMotionStupid();
  void cancelMotionStupid(); // ends a still-playing Stupid so a new tilt can show its eye movement
  bool isBitmapAnimationActive() const;
  void drawBitmapAnimation(Adafruit_SSD1306 &display);
  void reactMotionWake();
  void reactMotionWorried();

  // V2 — true once, right after a rare "special event" idle behaviour
  // (firework, surprise, suspicious glance, ...) starts, so the .ino can
  // pair it with a fun little buzzer flourish. Clears itself on read.
  bool consumeSpecialBeep();

private:
  Character &character;

  unsigned long lastInteractionTime;

  // ---- Sleep / wake — kept as their own small state machine since they
  // have special semantics (own the whole display, gate everything else).
  bool asleep;
  bool sleepSequenceActive;
  uint8_t sleepStep;
  unsigned long sleepStepStart;
  unsigned long sleepStepDuration;

  bool wakeSequenceActive;
  uint8_t wakeStep;
  unsigned long wakeStepStart;
  unsigned long wakeStepDuration;

  void startSleepSequence();
  void updateSleepSequence();
  void startWakeSequence();
  void updateWakeSequence();

  // ---- Generic sequence engine (INTERACTION..MICRO tiers) -------------
  AnimPriority activePriority;
  const SeqStep *seq;
  uint8_t seqLen;
  uint8_t seqIndex;
  unsigned long stepStart;
  unsigned long stepDuration;
  bool seqBusy;

  SeqStep adhocStep[1]; // scratch buffer for suggestMood()'s one-off step

  bool playSequence(const SeqStep *s, uint8_t len, AnimPriority pr);
  void startStep();
  void advanceSequence(unsigned long now);

  // ---- Idle personality + micro-behaviours -----------------------------
  int8_t lastIdleIndex;
  int8_t lastMicroIndex;
  unsigned long nextIdleAt;
  unsigned long nextMicroAt;

  void scheduleNextIdleChange();
  void scheduleNextMicro();
  void pickAndPlayIdle();
  void pickAndPlayMicro();

  bool pendingSpecialBeep; // V2: set when a rare "special event" idle behaviour is picked

  enum class BitmapAnimation : uint8_t {
    NONE,
    IDIOT,
    STUPID,
    DIZZY
  };

  BitmapAnimation bitmapAnimation;
  uint8_t bitmapPriority; // 1=dizzy, 2=pickup/idiot, 3=return/stupid
  uint16_t bitmapFrameIndex;
  uint32_t bitmapStartMs;
  uint32_t bitmapLastFrameMs;
  uint32_t bitmapDurationMs;
  uint16_t bitmapFrameIntervalMs;
  uint8_t bitmapFrameBuffer[1024];

  bool startBitmapAnimation(BitmapAnimation which, uint8_t priority,
                            uint32_t durationMs, uint16_t frameIntervalMs);
  void updateBitmapAnimation(unsigned long now);
  void loadBitmapFrame();
};
