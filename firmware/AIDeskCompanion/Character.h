#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// All the moods the character can be in. This is the entire personality
// vocabulary of the device — everything else (screens, animation manager,
// reminders, AI) just picks from this list.
//
// NOTE: a handful of expressions requested in the design brief (double
// blink, rapid blink, tiny eye twitch, ...) are intentionally NOT separate
// enum values here. They are built by AnimationManager as short *sequences*
// that chain a couple of these base expressions together (e.g. double
// blink = BLINK, pause, BLINK) — that keeps the face vocabulary small and
// procedural while still producing lots of distinct on-screen behaviour.
enum class Expression {
  NORMAL,
  BLINK,
  LOOK_LEFT,
  LOOK_RIGHT,
  LOOK_UP,
  LOOK_DOWN,
  BORED,
  PLAYFUL,
  SLEEPY,
  SLEEP,
  EXCITED,
  THINKING,
  WINK,
  SURPRISED,
  CONFUSED,
  SUSPICIOUS,
  WORRIED,
  SCARED,
  RELIEVED,
  LAUGHING,
  ROLLING_EYES,
  HAPPY,
  SAD,
  ANGRY,
  SHY,
  CURIOUS,
  HAPPY_SQUINT,
  TINY_EXCITED,
  SIDE_EYE,
  SMUG,
  MISCHIEVOUS,
  ANNOYED,
  DIZZY,
  FAKE_SLEEP,
  SHOCK,
  JUDGING,
  ATTENTION,   // brief "!" reaction to a button press / wake / notification
  WAKE,        // eyes-opening transition out of SLEEP
  YAWN,
  STRETCH,     // brief "alert stretch" used mid wake-up
  FIREWORK,    // V2: rare idle "special event" — excited look-up + tiny procedural burst

  // Reference-eye emotion vocabulary recreated procedurally from the
  // esp32-eyes visual language. These are real expressions, not aliases.
  GLEE,
  FOCUSED,
  SKEPTIC,
  FRUSTRATED,
  UNIMPRESSED,
  SQUINT,
  FURIOUS,
  AWE
};

// Procedural, bitmap-free face. Everything is drawn with Adafruit_GFX
// primitives so it costs almost no flash/RAM regardless of how many
// expressions exist.
class Character {
public:
  explicit Character(Adafruit_SSD1306 &disp);

  void begin();

  // Call every loop() — advances blink timers / breathing phase.
  // Does NOT draw anything.
  void update();

  // Draws the face centered at (originX, originY). scale 1.0 = full
  // ~60px-wide face (used on the CHARACTER/HOME and SLEEP screens),
  // smaller scale (e.g. 0.4) draws a compact badge usable on info screens.
  void draw(int originX, int originY, float scale = 1.0f);

  // Sets the current mood.
  //   durationMs == 0  -> persistent, stays until something else changes it.
  //   durationMs  > 0  -> transient; after it elapses the character reverts
  //                       to whatever mood was active before (usually NORMAL).
  // AnimationManager normally drives this with durationMs == 0 for every
  // step of a sequence and owns the timing itself — see Animation.cpp.
  void setExpression(Expression e, unsigned long durationMs = 0);

  Expression getExpression() const { return current; }

  // True while a transient (timed) expression is still playing — other
  // systems should not stomp on it while this is true.
  bool isTransientActive() const;

private:
  Adafruit_SSD1306 &display;

  Expression current;
  Expression previous;
  unsigned long expressionStart;
  unsigned long expressionDuration; // 0 = indefinite

  // Independent, always-running blink overlay so the character keeps
  // blinking even while a screen is locked or another mood is showing.
  unsigned long lastBlinkTime;
  unsigned long nextBlinkInterval;
  bool blinking;
  unsigned long blinkStart;
  static const unsigned long BLINK_DURATION_MS = 140;

  // Subtle up/down breathing motion, always running.
  unsigned long breathingStart;

  // Phase clock for any time-based expression detail (rolling eyes,
  // dizzy spin, thinking drift, excited wiggle, sparkle bob, ...).
  unsigned long moodPhaseStart;

  // V2: smoothed/eased pupil position actually drawn each frame, so the
  // pupils glide toward a new look direction instead of snapping there
  // the instant the expression changes. computeFace()'s pupilX/pupilY
  // stay the *target* the eyes ease toward.
  float dispPupilLX, dispPupilLY;
  float dispPupilRX, dispPupilRY;

  struct EyeShape {
    int w, h;              // capsule width/height at scale 1.0
    int offsetX, offsetY;  // shift of the eye center
    int pupilX, pupilY;    // pupil offset within the eye
    bool showPupil;
    int pupilR;            // Pupil radius at scale 1.0.
    float slopeTop;        // Reference-style organic eye angle.
    float slopeBottom;
    int radiusTop;
    int radiusBottom;
  };

  struct FaceShape {
    EyeShape left, right;
    int8_t browLeftTilt;   // -1 down-in, 0 none, 1 up-out (used as pixel offsets)
    int8_t browRightTilt;
    enum class Mouth { NONE, LINE, SMILE, BIG_SMILE, FROWN, O, SQUIGGLE, TIGHT, SMIRK, YAWN } mouth;
    enum class Extra { NONE, ZZZ, SPARKLE, QUESTION, EXCLAIM, TEAR, BLUSH, SPIN, FIREWORK } extra;
  };

  FaceShape computeFace(Expression e, unsigned long phaseMs) const;
  void drawEye(int cx, int cy, const EyeShape &shape, float scale, float openFactor);
  void drawBrow(int cx, int cy, int8_t tilt, bool isLeft, float scale);
  void drawMouth(int cx, int cy, FaceShape::Mouth m, float scale, unsigned long phaseMs);
  void drawExtra(int cx, int cy, FaceShape::Extra ex, float scale, unsigned long phaseMs);

  bool isBlinkEligible(Expression e) const;
  void randomizeNextBlink();
};
