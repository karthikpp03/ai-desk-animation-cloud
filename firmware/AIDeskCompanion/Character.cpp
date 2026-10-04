#include "Character.h"
#include <math.h>

Character::Character(Adafruit_SSD1306 &disp)
  : display(disp),
    current(Expression::NORMAL),
    previous(Expression::NORMAL),
    expressionStart(0),
    expressionDuration(0),
    lastBlinkTime(0),
    nextBlinkInterval(3000),
    blinking(false),
    blinkStart(0),
    breathingStart(0),
    moodPhaseStart(0),
    dispPupilLX(0), dispPupilLY(0),
    dispPupilRX(0), dispPupilRY(0) {}

void Character::begin() {
  unsigned long now = millis();
  expressionStart = now;
  breathingStart = now;
  moodPhaseStart = now;
  randomizeNextBlink();
  lastBlinkTime = now;
}

void Character::randomizeNextBlink() {
  // Blinks feel natural somewhere between 2 and 6 seconds apart.
  nextBlinkInterval = random(2000, 6000);
}

bool Character::isBlinkEligible(Expression e) const {
  switch (e) {
    // These expressions already control the eyelids or are deliberately
    // kept stable while their animation is running.
    case Expression::SLEEP:
    case Expression::BLINK:
    case Expression::WINK:
    case Expression::SHOCK:
    case Expression::DIZZY:
    case Expression::FAKE_SLEEP:
    case Expression::YAWN:
      return false;
    default:
      return true;
  }
}

bool Character::isTransientActive() const {
  if (expressionDuration == 0) return false;
  return (millis() - expressionStart) < expressionDuration;
}

void Character::setExpression(Expression e, unsigned long durationMs) {
  if (e == current && durationMs == expressionDuration) return;
  previous = (expressionDuration == 0) ? current : previous;
  current = e;
  expressionStart = millis();
  expressionDuration = durationMs;
  moodPhaseStart = millis();
}

void Character::update() {
  unsigned long now = millis();

  // Expire transient expressions back to what was showing before.
  if (expressionDuration != 0 && (now - expressionStart) >= expressionDuration) {
    Expression revertTo = previous;
    current = revertTo;
    expressionStart = now;
    expressionDuration = 0;
  }

  // Independent blink overlay.
  if (!blinking && isBlinkEligible(current) && (now - lastBlinkTime) >= nextBlinkInterval) {
    blinking = true;
    blinkStart = now;
  }
  if (blinking && (now - blinkStart) >= BLINK_DURATION_MS) {
    blinking = false;
    lastBlinkTime = now;
    randomizeNextBlink();
  }

  // V2: ease the displayed pupil position toward computeFace()'s target
  // for the current mood/phase, so a new look direction glides in rather
  // than snapping there instantly. A ~35%-per-frame catch-up rate keeps
  // it feeling responsive while still visibly smooth at loop() speed.
  FaceShape target = computeFace(current, now - moodPhaseStart);
  dispPupilLX += (target.left.pupilX  - dispPupilLX) * 0.35f;
  dispPupilLY += (target.left.pupilY  - dispPupilLY) * 0.35f;
  dispPupilRX += (target.right.pupilX - dispPupilRX) * 0.35f;
  dispPupilRY += (target.right.pupilY - dispPupilRY) * 0.35f;
}

// ---------------------------------------------------------------------
// Face shape table — one entry per expression, evaluated fresh every
// frame so time-based details (rolling eyes, dizzy spin, wiggle, ...)
// stay in motion. Kept purely procedural: no bitmaps, just capsule eyes,
// a pupil dot, an optional brow line, a mouth glyph and a tiny "extra"
// (zzz / sparkle / ? / ! / tear / blush / spin) drawn with GFX primitives.
// ---------------------------------------------------------------------
Character::FaceShape Character::computeFace(Expression e, unsigned long phaseMs) const {
  FaceShape f{};

  // These dimensions intentionally follow the reference project's
  // expressive proportions, scaled for this 128x64 OLED. The important
  // behaviour is procedural: shape, slope, radius, pupil and asymmetry all
  // change smoothly rather than using bitmap frames.
  f.left.w = f.right.w = 22;
  f.left.h = f.right.h = 22;
  f.left.offsetX = f.right.offsetX = 0;
  f.left.offsetY = f.right.offsetY = 0;
  f.left.pupilX = f.right.pupilX = 0;
  f.left.pupilY = f.right.pupilY = 0;
  f.left.showPupil = f.right.showPupil = true;
  f.left.pupilR = f.right.pupilR = 3;
  f.left.slopeTop = f.right.slopeTop = 0.0f;
  f.left.slopeBottom = f.right.slopeBottom = 0.0f;
  f.left.radiusTop = f.right.radiusTop = 7;
  f.left.radiusBottom = f.right.radiusBottom = 7;
  f.browLeftTilt = f.browRightTilt = 0;
  f.mouth = FaceShape::Mouth::O;
  f.extra = FaceShape::Extra::NONE;

  switch (e) {
    case Expression::NORMAL:
      // Living baseline: tiny pupil drift and a breathing mouth.
      f.left.pupilX = (int)(sinf(phaseMs / 1800.0f) * 1.0f);
      f.right.pupilX = f.left.pupilX;
      break;

    case Expression::BLINK:
      f.left.h = f.right.h = 3;
      f.left.showPupil = f.right.showPupil = false;
      f.mouth = FaceShape::Mouth::LINE;
      break;

    case Expression::LOOK_LEFT:
      f.left.pupilX = f.right.pupilX = -6;
      f.mouth = FaceShape::Mouth::SMILE;
      break;
    case Expression::LOOK_RIGHT:
      f.left.pupilX = f.right.pupilX = 6;
      f.mouth = FaceShape::Mouth::SMILE;
      break;
    case Expression::LOOK_UP:
      f.left.pupilY = f.right.pupilY = -6;
      f.mouth = FaceShape::Mouth::O;
      break;
    case Expression::LOOK_DOWN:
      f.left.pupilY = f.right.pupilY = 6;
      f.mouth = FaceShape::Mouth::O;
      break;

    case Expression::BORED:
      f.left.h = f.right.h = 12;
      f.left.offsetY = f.right.offsetY = 5;
      f.left.pupilR = f.right.pupilR = 2;
      f.browLeftTilt = f.browRightTilt = -1;
      f.mouth = FaceShape::Mouth::LINE;
      break;

    case Expression::PLAYFUL:
      f.left.h = 25; f.right.h = 15;
      f.right.offsetY = 3;
      f.left.slopeTop = -0.15f; f.right.slopeTop = 0.15f;
      f.left.pupilR = 3; f.right.pupilR = 2;
      f.mouth = FaceShape::Mouth::SMIRK;
      f.extra = FaceShape::Extra::SPARKLE;
      break;

    case Expression::SLEEPY:
      f.left.h = f.right.h = 10;
      f.left.offsetY = f.right.offsetY = 5;
      f.left.showPupil = f.right.showPupil = false;
      f.left.slopeTop = f.right.slopeTop = -0.45f;
      f.left.slopeBottom = f.right.slopeBottom = -0.45f;
      f.mouth = FaceShape::Mouth::LINE;
      break;

    case Expression::SLEEP:
      f.left.h = f.right.h = 3;
      f.left.showPupil = f.right.showPupil = false;
      f.extra = FaceShape::Extra::ZZZ;
      f.mouth = FaceShape::Mouth::LINE;
      break;

    case Expression::EXCITED: {
      int wig = (int)(sinf(phaseMs / 120.0f) * 2.0f);
      f.left.w = f.right.w = 24;
      f.left.h = f.right.h = 29;
      f.left.pupilX = f.right.pupilX = wig;
      f.left.pupilR = f.right.pupilR = 5;
      f.mouth = FaceShape::Mouth::BIG_SMILE;
      f.extra = FaceShape::Extra::SPARKLE;
      break;
    }

    case Expression::THINKING: {
      int drift = (int)(sinf(phaseMs / 550.0f) * 2.0f);
      f.left.pupilX = f.right.pupilX = drift;
      f.left.pupilY = f.right.pupilY = -5;
      f.left.pupilR = f.right.pupilR = 2;
      f.browLeftTilt = 1;
      f.mouth = FaceShape::Mouth::LINE;
      break;
    }

    case Expression::WINK:
      f.left.h = 3;
      f.left.showPupil = false;
      f.right.pupilR = 4;
      f.mouth = FaceShape::Mouth::SMILE;
      break;

    case Expression::SURPRISED:
      f.left.w = f.right.w = 25;
      f.left.h = f.right.h = 32;
      f.left.pupilR = f.right.pupilR = 6;
      f.left.radiusTop = f.right.radiusTop = 10;
      f.left.radiusBottom = f.right.radiusBottom = 10;
      f.mouth = FaceShape::Mouth::O;
      f.extra = FaceShape::Extra::EXCLAIM;
      break;

    case Expression::CONFUSED:
      f.left.h = 25; f.right.h = 17;
      f.left.slopeTop = -0.18f; f.right.slopeTop = 0.25f;
      f.left.pupilX = -2; f.right.pupilX = 3;
      f.left.pupilR = 2; f.right.pupilR = 4;
      f.browLeftTilt = -1; f.browRightTilt = 1;
      f.mouth = FaceShape::Mouth::SQUIGGLE;
      f.extra = FaceShape::Extra::QUESTION;
      break;

    case Expression::SUSPICIOUS:
      f.left.h = 21; f.right.h = 15;
      f.left.slopeTop = 0.10f; f.right.slopeTop = 0.30f;
      f.left.pupilX = f.right.pupilX = 5;
      f.browRightTilt = -1;
      f.mouth = FaceShape::Mouth::SMIRK;
      break;

    case Expression::WORRIED:
      f.left.h = 25; f.right.h = 30;
      f.left.slopeTop = -0.18f; f.right.slopeTop = 0.18f;
      f.left.pupilY = f.right.pupilY = -3;
      f.left.pupilR = f.right.pupilR = 4;
      f.browLeftTilt = f.browRightTilt = -1;
      f.mouth = FaceShape::Mouth::O;
      break;

    case Expression::SCARED:
      f.left.w = f.right.w = 27;
      f.left.h = f.right.h = 34;
      f.left.pupilR = f.right.pupilR = 6;
      f.mouth = FaceShape::Mouth::O;
      f.extra = FaceShape::Extra::EXCLAIM;
      break;

    case Expression::RELIEVED:
      f.left.h = f.right.h = 8;
      f.left.showPupil = f.right.showPupil = false;
      f.left.slopeTop = f.right.slopeTop = -0.35f;
      f.mouth = FaceShape::Mouth::BIG_SMILE;
      break;

    case Expression::LAUGHING:
      f.left.h = f.right.h = 5;
      f.left.showPupil = f.right.showPupil = false;
      f.mouth = FaceShape::Mouth::BIG_SMILE;
      break;

    case Expression::ROLLING_EYES: {
      float t = phaseMs / 330.0f;
      f.left.pupilX = f.right.pupilX = (int)(cosf(t) * 5.0f);
      f.left.pupilY = f.right.pupilY = (int)(sinf(t) * 3.0f) - 2;
      f.mouth = FaceShape::Mouth::LINE;
      break;
    }

    case Expression::HAPPY:
      f.left.h = f.right.h = 11;
      f.left.slopeTop = f.right.slopeTop = 0.10f;
      f.left.slopeBottom = f.right.slopeBottom = -0.25f;
      f.left.showPupil = f.right.showPupil = false;
      f.mouth = FaceShape::Mouth::BIG_SMILE;
      break;

    case Expression::SAD:
      f.left.h = f.right.h = 16;
      f.left.slopeTop = -0.35f; f.right.slopeTop = 0.35f;
      f.left.offsetY = f.right.offsetY = 2;
      f.browLeftTilt = f.browRightTilt = -1;
      f.mouth = FaceShape::Mouth::FROWN;
      f.extra = FaceShape::Extra::TEAR;
      break;

    case Expression::ANGRY:
      f.left.h = f.right.h = 13;
      f.left.slopeTop = 0.38f; f.right.slopeTop = -0.38f;
      f.left.pupilR = f.right.pupilR = 2;
      f.mouth = FaceShape::Mouth::TIGHT;
      break;

    case Expression::SHY:
      f.left.h = f.right.h = 14;
      f.left.offsetY = f.right.offsetY = 3;
      f.left.pupilY = f.right.pupilY = 4;
      f.left.pupilR = f.right.pupilR = 2;
      f.mouth = FaceShape::Mouth::LINE;
      f.extra = FaceShape::Extra::BLUSH;
      break;

    case Expression::CURIOUS:
      f.left.h = 27; f.right.h = 21;
      f.left.slopeTop = -0.12f; f.right.slopeTop = 0.12f;
      f.left.pupilY = f.right.pupilY = -4;
      f.left.pupilR = f.right.pupilR = 4;
      f.browLeftTilt = 1;
      f.mouth = FaceShape::Mouth::O;
      break;

    case Expression::HAPPY_SQUINT:
      f.left.h = f.right.h = 6;
      f.left.showPupil = f.right.showPupil = false;
      f.mouth = FaceShape::Mouth::BIG_SMILE;
      break;

    case Expression::TINY_EXCITED:
      f.left.w = f.right.w = 16; f.left.h = f.right.h = 16;
      f.left.pupilR = f.right.pupilR = 3;
      f.mouth = FaceShape::Mouth::SMILE;
      f.extra = FaceShape::Extra::SPARKLE;
      break;

    case Expression::SIDE_EYE:
      f.left.h = f.right.h = 16;
      f.left.pupilX = f.right.pupilX = 6;
      f.left.pupilR = f.right.pupilR = 2;
      f.browRightTilt = -1;
      f.mouth = FaceShape::Mouth::LINE;
      break;

    case Expression::SMUG:
      f.left.h = 20; f.right.h = 13;
      f.right.offsetY = 2;
      f.left.slopeTop = -0.15f; f.right.slopeTop = 0.15f;
      f.mouth = FaceShape::Mouth::SMIRK;
      break;

    case Expression::MISCHIEVOUS:
      f.left.h = 22; f.right.h = 13;
      f.right.offsetY = 2;
      f.left.pupilX = f.right.pupilX = 3;
      f.left.slopeTop = -0.10f; f.right.slopeTop = 0.22f;
      f.mouth = FaceShape::Mouth::SMIRK;
      f.extra = FaceShape::Extra::SPARKLE;
      break;

    case Expression::ANNOYED:
      f.left.h = f.right.h = 11;
      f.left.slopeTop = f.right.slopeTop = 0.15f;
      f.left.pupilR = f.right.pupilR = 2;
      f.browLeftTilt = f.browRightTilt = -1;
      f.mouth = FaceShape::Mouth::TIGHT;
      break;

    case Expression::DIZZY: {
      float t = phaseMs / 150.0f;
      int px = (int)(cosf(t) * 6.0f);
      int py = (int)(sinf(t) * 4.0f);
      f.left.pupilX = px; f.left.pupilY = py;
      f.right.pupilX = -px; f.right.pupilY = py;
      f.left.pupilR = f.right.pupilR = 2;
      f.mouth = FaceShape::Mouth::SQUIGGLE;
      f.extra = FaceShape::Extra::SPIN;
      break;
    }

    case Expression::FAKE_SLEEP:
      f.left.h = 4; f.right.h = 9;
      f.left.showPupil = false;
      f.right.pupilY = 3;
      f.mouth = FaceShape::Mouth::LINE;
      break;

    case Expression::SHOCK: {
      int jit = (int)(sinf(phaseMs / 55.0f) * 1.5f);
      f.left.w = f.right.w = 28;
      f.left.h = f.right.h = 35;
      f.left.pupilR = f.right.pupilR = 7;
      f.left.pupilX = f.right.pupilX = jit;
      f.mouth = FaceShape::Mouth::O;
      f.extra = FaceShape::Extra::EXCLAIM;
      break;
    }

    case Expression::JUDGING:
      f.left.h = f.right.h = 13;
      f.left.pupilY = f.right.pupilY = 3;
      f.left.pupilR = f.right.pupilR = 2;
      f.browLeftTilt = f.browRightTilt = -1;
      f.mouth = FaceShape::Mouth::LINE;
      break;

    case Expression::ATTENTION:
      f.left.w = f.right.w = 24; f.left.h = f.right.h = 29;
      f.left.pupilR = f.right.pupilR = 4;
      f.mouth = FaceShape::Mouth::O;
      break;

    case Expression::WAKE: {
      float t = constrain(phaseMs / 420.0f, 0.0f, 1.0f);
      f.left.h = 3 + (int)(24 * t);
      f.right.h = 2 + (int)(22 * t);
      f.mouth = FaceShape::Mouth::O;
      break;
    }

    case Expression::YAWN: {
      f.left.h = f.right.h = 9;
      f.left.showPupil = f.right.showPupil = false;
      f.mouth = FaceShape::Mouth::YAWN;
      break;
    }

    case Expression::STRETCH:
      f.left.h = f.right.h = 23;
      f.left.w = f.right.w = 24;
      f.browLeftTilt = f.browRightTilt = 1;
      f.mouth = FaceShape::Mouth::LINE;
      break;

    case Expression::FIREWORK: {
      int wig = (int)(sinf(phaseMs / 150.0f) * 2.0f);
      f.left.w = f.right.w = 24; f.left.h = f.right.h = 28;
      f.left.pupilR = f.right.pupilR = 5;
      f.left.pupilX = f.right.pupilX = wig;
      f.left.pupilY = f.right.pupilY = -3;
      f.mouth = FaceShape::Mouth::BIG_SMILE;
      f.extra = FaceShape::Extra::FIREWORK;
      break;
    }

    // -----------------------------------------------------------------
    // 18 reference emotions. These are recreated from the reference's
    // procedural presets (height/width/slope/radius/asymmetry), not copied
    // source code or bitmap frames.
    // -----------------------------------------------------------------
    case Expression::GLEE:
      f.left.h = f.right.h = 9;
      f.left.slopeBottom = f.right.slopeBottom = -0.35f;
      f.left.showPupil = f.right.showPupil = false;
      f.mouth = FaceShape::Mouth::BIG_SMILE;
      break;

    case Expression::FOCUSED:
      f.left.h = f.right.h = 13;
      f.left.slopeTop = 0.20f; f.right.slopeTop = -0.20f;
      f.left.pupilR = f.right.pupilR = 2;
      f.mouth = FaceShape::Mouth::TIGHT;
      break;

    case Expression::SKEPTIC:
      f.left.h = 23; f.right.h = 16;
      f.left.pupilX = 3; f.right.pupilX = 5;
      f.right.offsetY = -5;
      f.right.slopeTop = 0.30f;
      f.mouth = FaceShape::Mouth::SMIRK;
      break;

    case Expression::FRUSTRATED:
      f.left.h = f.right.h = 12;
      f.left.offsetX = f.right.offsetX = 2;
      f.left.offsetY = f.right.offsetY = -3;
      f.left.slopeTop = 0.20f; f.right.slopeTop = -0.20f;
      f.mouth = FaceShape::Mouth::TIGHT;
      break;

    case Expression::UNIMPRESSED:
      f.left.h = f.right.h = 12;
      f.left.offsetX = f.right.offsetX = 2;
      f.left.slopeTop = f.right.slopeTop = 0.05f;
      f.mouth = FaceShape::Mouth::LINE;
      break;

    case Expression::SQUINT:
      f.left.w = f.right.w = 19;
      f.left.h = f.right.h = 20;
      f.left.offsetX = -4; f.right.offsetX = 3;
      f.left.offsetY = -2; f.right.offsetY = 0;
      f.left.pupilR = f.right.pupilR = 2;
      f.mouth = FaceShape::Mouth::SMIRK;
      break;

    case Expression::FURIOUS:
      f.left.h = f.right.h = 18;
      f.left.slopeTop = 0.42f; f.right.slopeTop = -0.42f;
      f.left.pupilR = f.right.pupilR = 2;
      f.mouth = FaceShape::Mouth::TIGHT;
      f.extra = FaceShape::Extra::EXCLAIM;
      break;

    case Expression::AWE:
      f.left.w = f.right.w = 25;
      f.left.h = f.right.h = 29;
      f.left.slopeTop = -0.10f; f.right.slopeTop = 0.10f;
      f.left.slopeBottom = 0.10f; f.right.slopeBottom = -0.10f;
      f.left.pupilR = f.right.pupilR = 5;
      f.mouth = FaceShape::Mouth::O;
      f.extra = FaceShape::Extra::SPARKLE;
      break;
  }

  return f;
}

void Character::drawEye(int cx, int cy, const EyeShape &shape, float scale, float openFactor) {
  int w = max(2, (int)(shape.w * scale));
  int h = max(2, (int)(shape.h * scale * openFactor));
  int x = cx + (int)(shape.offsetX * scale) - w / 2;
  int y = cy + (int)(shape.offsetY * scale) - h / 2;
  int r = max(1, min(w, h) / 2);

  // Base organic eye. The reference style uses rounded geometry with
  // independent top/bottom slopes; triangles trim/add the sloped edges
  // without requiring a bitmap or a second graphics library.
  display.fillRoundRect(x, y, w, h, r, SSD1306_WHITE);

  float top = shape.slopeTop;
  float bot = shape.slopeBottom;
  if (fabsf(top) > 0.01f) {
    int d = constrain((int)(fabsf(top) * h * 0.75f), 1, max(1, h / 2));
    if (top > 0) {
      display.fillTriangle(x, y, x + w, y, x + w, y + d, SSD1306_BLACK);
      display.fillTriangle(x, y, x + w, y + d, x, y + d / 2, SSD1306_WHITE);
    } else {
      display.fillTriangle(x, y, x + w, y, x, y + d, SSD1306_BLACK);
      display.fillTriangle(x, y, x + w, y + d, x + w, y + d / 2, SSD1306_WHITE);
    }
  }
  if (fabsf(bot) > 0.01f) {
    int d = constrain((int)(fabsf(bot) * h * 0.65f), 1, max(1, h / 2));
    int by = y + h - 1;
    if (bot > 0) {
      display.fillTriangle(x, by, x + w, by, x, by - d, SSD1306_BLACK);
    } else {
      display.fillTriangle(x, by, x + w, by, x + w, by - d, SSD1306_BLACK);
    }
  }

  if (shape.showPupil && h > (int)(6 * scale)) {
    int pr = max(1, (int)(shape.pupilR * scale));
    int px = cx + (int)((shape.offsetX + shape.pupilX) * scale);
    int py = cy + (int)((shape.offsetY + shape.pupilY) * scale);
    display.fillCircle(px, py, pr, SSD1306_BLACK);
  }
}

void Character::drawBrow(int cx, int cy, int8_t tilt, bool isLeft, float scale) {
  if (tilt == 0) return;
  int w = (int)(14 * scale);
  int x0 = cx - w / 2;
  int y0 = cy - (int)(18 * scale);
  int dy = (int)(3 * scale) * tilt * (isLeft ? 1 : -1);
  display.drawLine(x0, y0 + (tilt > 0 ? dy : 0), x0 + w, y0 + (tilt > 0 ? 0 : dy), SSD1306_WHITE);
}

void Character::drawMouth(int cx, int cy, FaceShape::Mouth m, float scale, unsigned long phaseMs) {
  float wave = sinf(phaseMs / 180.0f) * 0.5f + 0.5f;
  int y = cy + (int)(20 * scale);
  int hw = max(3, (int)(10 * scale));
  int bob = (int)(sinf(phaseMs / 260.0f) * max(1, (int)(1.5f * scale)));
  switch (m) {
    case FaceShape::Mouth::NONE:
      break;
    case FaceShape::Mouth::LINE:
      display.drawLine(cx - hw / 2, y + bob, cx + hw / 2, y + bob, SSD1306_WHITE);
      break;
    case FaceShape::Mouth::SMILE: {
      int sy = y + (int)(2 * scale) + bob;
      display.drawLine(cx - hw, y + bob, cx - hw / 2, sy, SSD1306_WHITE);
      display.drawLine(cx - hw / 2, sy, cx + hw / 2, sy, SSD1306_WHITE);
      display.drawLine(cx + hw / 2, sy, cx + hw, y + bob, SSD1306_WHITE);
      break;
    }
    case FaceShape::Mouth::BIG_SMILE: {
      int mouthH = max(4, (int)((6.0f + wave * 3.0f) * scale));
      display.fillRoundRect(cx - hw, y - (int)(1 * scale) + bob, hw * 2, mouthH, max(2, (int)(3 * scale)), SSD1306_WHITE);
      break;
    }
    case FaceShape::Mouth::FROWN:
      display.drawLine(cx - hw, y + (int)(4 * scale), cx - hw / 2, y, SSD1306_WHITE);
      display.drawLine(cx - hw / 2, y + bob, cx + hw / 2, y + bob, SSD1306_WHITE);
      display.drawLine(cx + hw / 2, y, cx + hw, y + (int)(4 * scale), SSD1306_WHITE);
      break;
    case FaceShape::Mouth::O: {
      int rr = max(2, (int)((4.0f + wave * 2.0f) * scale));
      display.fillCircle(cx, y + bob, rr, SSD1306_WHITE);
      break;
    }
    case FaceShape::Mouth::SQUIGGLE: {
      int sy2 = y + bob;
      int sw = (int)(2 * scale);
      display.drawLine(cx - hw, sy2, cx - hw / 3, sy2 - sw, SSD1306_WHITE);
      display.drawLine(cx - hw / 3, sy2 - sw, cx + hw / 3, sy2 + sw, SSD1306_WHITE);
      display.drawLine(cx + hw / 3, sy2 + sw, cx + hw, sy2, SSD1306_WHITE);
      break;
    }
    case FaceShape::Mouth::TIGHT:
      display.fillRoundRect(cx - hw / 2, y - (int)(1 * scale), hw, max(2, (int)(2 * scale)), 1, SSD1306_WHITE);
      break;
    case FaceShape::Mouth::SMIRK:
      display.drawLine(cx - hw / 2, y, cx + hw / 4, y, SSD1306_WHITE);
      display.drawLine(cx + hw / 4, y, cx + hw, y - (int)(4 * scale), SSD1306_WHITE);
      break;
    case FaceShape::Mouth::YAWN: {
      int yh = max(7, (int)((9.0f + wave * 4.0f) * scale));
      display.fillRoundRect(cx - (int)(4 * scale), y - (int)(3 * scale) + bob, (int)(8 * scale), yh, (int)(4 * scale), SSD1306_WHITE);
      break;
    }
  }
}

void Character::drawExtra(int cx, int cy, FaceShape::Extra ex, float scale, unsigned long phaseMs) {
  switch (ex) {
    case FaceShape::Extra::NONE:
      break;
    case FaceShape::Extra::ZZZ: {
      int bob = (int)(sinf(phaseMs / 400.0f) * 2.0f);
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(cx + (int)(28 * scale), cy - (int)(26 * scale) + bob);
      display.print("z");
      display.setCursor(cx + (int)(33 * scale), cy - (int)(31 * scale) + bob);
      display.print("Z");
      break;
    }
    case FaceShape::Extra::SPARKLE: {
      int bob = (int)(sinf(phaseMs / 250.0f) * 1.5f);
      int sx = cx + (int)(30 * scale);
      int sy = cy - (int)(22 * scale) + bob;
      display.drawLine(sx - 2, sy, sx + 2, sy, SSD1306_WHITE);
      display.drawLine(sx, sy - 2, sx, sy + 2, SSD1306_WHITE);
      break;
    }
    case FaceShape::Extra::QUESTION:
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(cx + (int)(28 * scale), cy - (int)(28 * scale));
      display.print("?");
      break;
    case FaceShape::Extra::EXCLAIM:
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(cx + (int)(28 * scale), cy - (int)(28 * scale));
      display.print("!");
      break;
    case FaceShape::Extra::TEAR: {
      int dx = cx + (int)(24 * scale);
      int dy = cy - (int)(14 * scale) + (int)((phaseMs % 600) / 100);
      display.fillCircle(dx, dy, max(1, (int)(2 * scale)), SSD1306_WHITE);
      break;
    }
    case FaceShape::Extra::BLUSH: {
      int lx = cx - (int)(32 * scale);
      int rx = cx + (int)(32 * scale);
      int by = cy + (int)(6 * scale);
      display.drawCircle(lx, by, max(1, (int)(2 * scale)), SSD1306_WHITE);
      display.drawCircle(rx, by, max(1, (int)(2 * scale)), SSD1306_WHITE);
      break;
    }
    case FaceShape::Extra::SPIN: {
      float t = phaseMs / 150.0f;
      int sx = cx + (int)(cosf(t) * 7.0f);
      int sy = cy - (int)(26 * scale) + (int)(sinf(t) * 3.0f);
      int sx2 = cx + (int)(cosf(t + PI) * 7.0f);
      display.drawCircle(sx, sy, 1, SSD1306_WHITE);
      display.drawCircle(sx2, sy, 1, SSD1306_WHITE);
      break;
    }
    case FaceShape::Extra::FIREWORK: {
      // V2.1: a proper little celebration — 3 staggered bursts, each with
      // several individually-timed particles radiating outward at
      // slightly different angles/speeds, plus a few independent twinkle
      // sparks. Everything here is a pure function of phaseMs (no stored
      // state, no random() calls at draw time), and per-particle
      // angle/speed comes from a tiny integer hash instead of random() —
      // random() would reroll every single frame and just look like
      // static noise instead of particles actually flying outward. Pure
      // geometry (drawPixel/drawLine only) — cheap on flash/RAM, still
      // non-blocking, still bounded well under ~1.5s total.
      static const int8_t BURST_DX[3] = { -22, 20, 0 };   // stay clear of the eyes
      static const int8_t BURST_DY[3] = { -4, -2, -13 };
      static const uint16_t BURST_DELAY_MS[3] = { 0, 140, 260 };
      const uint16_t burstLifeMs = 420;
      const uint8_t particles = 7;

      for (int b = 0; b < 3; b++) {
        unsigned long local = phaseMs + BURST_DELAY_MS[b];
        unsigned long cyc = local % 900; // each burst repeats a couple of
                                          // times across the ~0.9-1.4s event
        if (cyc >= burstLifeMs) continue;
        float t = (float)cyc / (float)burstLifeMs; // 0..1 across this burst

        int bx = cx + (int)(BURST_DX[b] * scale);
        int by = cy + (int)(BURST_DY[b] * scale) - (int)(20 * scale);

        for (int p = 0; p < particles; p++) {
          // Deterministic per-particle "hash" -> a stable, scattered (not
          // neatly symmetric) set of directions/speeds for this burst.
          int h = (b * 13 + p * 7) % 17;
          float ang = (float)((h * 41) % 360) * PI / 180.0f;
          float speed = 0.55f + (float)((h * 3) % 5) / 5.0f; // 0.55..1.35

          // Fade the burst out gradually rather than all particles
          // vanishing on the same frame.
          if (t > 0.7f && ((p + (int)(t * 12)) % 3) == 0) continue;

          float rad = t * speed * 11.0f * scale;
          int px = bx + (int)(cosf(ang) * rad);
          int py = by + (int)(sinf(ang) * rad);
          display.drawPixel(px, py, SSD1306_WHITE);

          if (t < 0.45f) {
            // Short bright trail while the spark is still accelerating out.
            int tx = bx + (int)(cosf(ang) * rad * 0.55f);
            int ty = by + (int)(sinf(ang) * rad * 0.55f);
            display.drawPixel(tx, ty, SSD1306_WHITE);
          }
        }
      }

      // A few independent twinkle sparks, further out, blinking in and
      // out on a short time bucket so they read as "twinkling" rather
      // than static dots.
      static const int8_t TW_DX[4] = { -30, 30, -13, 15 };
      static const int8_t TW_DY[4] = { 3, 1, -19, -17 };
      for (int i = 0; i < 4; i++) {
        unsigned long bucket = (phaseMs / 120UL) + (unsigned long)(i * 3);
        if ((bucket % 3UL) != 0UL) continue; // on for ~1/3 of buckets
        int tx = cx + (int)(TW_DX[i] * scale);
        int ty = cy + (int)(TW_DY[i] * scale) - (int)(18 * scale);
        display.drawPixel(tx, ty, SSD1306_WHITE);
        display.drawPixel(tx + 1, ty, SSD1306_WHITE);
        display.drawPixel(tx, ty + 1, SSD1306_WHITE);
      }
      break;
    }
  }
}

void Character::draw(int originX, int originY, float scale) {
  unsigned long now = millis();
  unsigned long phaseMs = now - moodPhaseStart;

  FaceShape face = computeFace(current, phaseMs);

  // V2: draw with the eased pupil position (see update()) rather than
  // the instantaneous target, so pupils glide into a new look direction
  // instead of jumping there in one frame. Shape/size/brows/mouth/extras
  // still come straight from the fresh computeFace() above.
  face.left.pupilX  = (int)roundf(dispPupilLX);
  face.left.pupilY  = (int)roundf(dispPupilLY);
  face.right.pupilX = (int)roundf(dispPupilRX);
  face.right.pupilY = (int)roundf(dispPupilRY);

  // Gentle always-on breathing: whole face bobs +-1px over ~2.4s.
  int breathe = (int)round(sinf((now - breathingStart) / 2400.0f * 2.0f * PI));

  int eyeGap = (int)(34 * scale);
  int leftCx = originX - eyeGap / 2;
  int rightCx = originX + eyeGap / 2;
  int eyeCy = originY + breathe;

  // Blink overlay squashes whichever eyes are currently blink-eligible.
  float openFactor = 1.0f;
  if (blinking && isBlinkEligible(current)) {
    unsigned long bt = now - blinkStart;
    float half = BLINK_DURATION_MS / 2.0f;
    float t = (bt < half) ? (bt / half) : (1.0f - (bt - half) / half);
    openFactor = 1.0f - constrain(t, 0.0f, 1.0f) * 0.92f;
  }

  drawEye(leftCx, eyeCy, face.left, scale, openFactor);
  drawEye(rightCx, eyeCy, face.right, scale, openFactor);
  drawBrow(leftCx, eyeCy, face.browLeftTilt, true, scale);
  drawBrow(rightCx, eyeCy, face.browRightTilt, false, scale);
  drawMouth(originX, eyeCy, face.mouth, scale, phaseMs);
  drawExtra(originX, eyeCy, face.extra, scale, phaseMs);
}
