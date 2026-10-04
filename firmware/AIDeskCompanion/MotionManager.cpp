#include "MotionManager.h"
#include <math.h>

static const uint8_t REG_SMPLRT_DIV = 0x19;
static const uint8_t REG_CONFIG = 0x1A;
static const uint8_t REG_GYRO_CONFIG = 0x1B;
static const uint8_t REG_ACCEL_CONFIG = 0x1C;
static const uint8_t REG_INT_PIN_CFG = 0x37;
static const uint8_t REG_INT_ENABLE = 0x38;
static const uint8_t REG_ACCEL_XOUT_H = 0x3B;
static const uint8_t REG_PWR_MGMT_1 = 0x6B;
static const uint8_t REG_WHO_AM_I = 0x75;

MotionManager::MotionManager(AnimationManager &a, BuzzerManager &b,
                             LEDManager &l, EarManager &e)
  : animation(a), buzzer(b), ledMgr(l), earMgr(e),
    sensorOK(false), lastReadMs(0),
    ax(0), ay(0), az(1), gx(0), gy(0), gz(0),
    accelMag(1), gyroMag(0), accelRawMag(1), prevAccelMag(1),
    prevZ(1), accelBaseline(1), gyroBaseline(0),
    lastTapMs(0), lastTiltMs(0), lastShockMs(0), lastRotationMs(0),
    lastShakeMs(0), lastWakeMs(0), lastPickupMs(0), lastLandingMs(0),
    shakePeakCount(0), shakePeakWindowStart(0), shakeBurstCount(0), shakeBurstWindowStart(0),
    liftCandidate(false), liftCandidateStart(0), liftMotionSeen(false), carried(false),
    tiltOccurred(false), lastTilt(-1) {}

bool MotionManager::writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

uint8_t MotionManager::readRegister(uint8_t reg) {
  uint8_t b = 0;
  if (!readRegisters(reg, &b, 1)) return 0;
  return b;
}

bool MotionManager::readRegisters(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  uint8_t got = Wire.requestFrom((int)MPU6050_ADDRESS, (int)len, true);
  if (got != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

void MotionManager::begin() {
#if MOTION_ENABLED
  pinMode(MPU6050_INT_PIN, INPUT);

  uint8_t who = readRegister(REG_WHO_AM_I);
  // This particular GY-521 reports 0x70 while exposing the normal
  // MPU6050 register map at 0x68. Keep compatibility with both IDs.
  if (who != 0x68 && who != 0x70) {
    sensorOK = false;
    return;
  }

  sensorOK = writeRegister(REG_PWR_MGMT_1, 0x01);
  sensorOK = sensorOK && writeRegister(REG_SMPLRT_DIV, 19);
  sensorOK = sensorOK && writeRegister(REG_CONFIG, 0x03);
  sensorOK = sensorOK && writeRegister(REG_GYRO_CONFIG, 0x08);  // ±500 dps
  sensorOK = sensorOK && writeRegister(REG_ACCEL_CONFIG, 0x08); // ±4g
  sensorOK = sensorOK && writeRegister(REG_INT_PIN_CFG, 0x00);
  sensorOK = sensorOK && writeRegister(REG_INT_ENABLE, 0x01);

  lastReadMs = 0;
  prevZ = 1.0f;
#else
  sensorOK = false;
#endif
}

float MotionManager::lerp(float current, float sample, float alpha) const {
  return current + (sample - current) * alpha;
}

bool MotionManager::readSensor() {
  uint8_t b[14];
  if (!readRegisters(REG_ACCEL_XOUT_H, b, sizeof(b))) {
    sensorOK = false;
    return false;
  }

  int16_t rax = (int16_t)((b[0] << 8) | b[1]);
  int16_t ray = (int16_t)((b[2] << 8) | b[3]);
  int16_t raz = (int16_t)((b[4] << 8) | b[5]);
  int16_t rgx = (int16_t)((b[8] << 8) | b[9]);
  int16_t rgy = (int16_t)((b[10] << 8) | b[11]);
  int16_t rgz = (int16_t)((b[12] << 8) | b[13]);

  float sx = rax / 8192.0f;
  float sy = ray / 8192.0f;
  float sz = raz / 8192.0f;
  float wx = rgx / 65.5f;
  float wy = rgy / 65.5f;
  float wz = rgz / 65.5f;

  accelRawMag = sqrtf(sx*sx + sy*sy + sz*sz);
  float rawGyroMag = sqrtf(wx*wx + wy*wy + wz*wz);

  ax = lerp(ax, sx, MOTION_ACCEL_FILTER_ALPHA);
  ay = lerp(ay, sy, MOTION_ACCEL_FILTER_ALPHA);
  az = lerp(az, sz, MOTION_ACCEL_FILTER_ALPHA);
  gx = lerp(gx, wx, MOTION_GYRO_FILTER_ALPHA);
  gy = lerp(gy, wy, MOTION_GYRO_FILTER_ALPHA);
  gz = lerp(gz, wz, MOTION_GYRO_FILTER_ALPHA);

  prevAccelMag = accelMag;
  accelMag = sqrtf(ax*ax + ay*ay + az*az);
  gyroMag = sqrtf(gx*gx + gy*gy + gz*gz);

  // Very slow baselines represent the resting box. They intentionally do
  // not chase short impacts, which makes tiny movements much less noisy.
  accelBaseline = lerp(accelBaseline, accelMag, 0.012f);
  gyroBaseline = lerp(gyroBaseline, rawGyroMag, 0.025f);
  return true;
}

float MotionManager::tiltFrontBackDeg() const {
  return atan2f(MOTION_FRONT_Y_SIGN * ay, fabsf(az) + 0.001f) * 180.0f / PI;
}

float MotionManager::tiltLeftRightDeg() const {
  return atan2f(MOTION_LEFT_X_SIGN * ax, fabsf(az) + 0.001f) * 180.0f / PI;
}

bool MotionManager::cooldownPassed(unsigned long since, unsigned long cd) const {
  return millis() - since >= cd;
}

void MotionManager::triggerTap() {
  unsigned long now = millis();
  if (!cooldownPassed(lastTapMs, MOTION_TAP_COOLDOWN_MS)) return;
  lastTapMs = now;
  animation.reactMotionTap();
  if (earMgr.isEnabled()) earMgr.reactMotionTap();
  buzzer.beepButton();
}

void MotionManager::triggerShock(bool rotation) {
  unsigned long now = millis();
  if (!cooldownPassed(lastShockMs, MOTION_EVENT_COOLDOWN_MS)) return;
  lastShockMs = now;

  if (rotation) {
    animation.reactMotionDizzy();
    if (earMgr.isEnabled()) earMgr.reactMotionDizzy();
    buzzer.beepMotionDizzy();
    if (ledMgr.isEnabled()) ledMgr.playMotionShake(true);
  } else {
    animation.reactMotionShock();
    if (earMgr.isEnabled()) earMgr.reactMotionShock();
    buzzer.beepMotionShock();
    if (ledMgr.isEnabled()) ledMgr.playMotionShock();
  }
}

void MotionManager::triggerShake(uint8_t level) {
  unsigned long now = millis();
  if (!cooldownPassed(lastShakeMs, MOTION_SHAKE_COOLDOWN_MS)) return;
  lastShakeMs = now;

  if (shakeBurstWindowStart == 0 || now - shakeBurstWindowStart > MOTION_SHAKE_BURST_WINDOW_MS) {
    shakeBurstWindowStart = now;
    shakeBurstCount = 0;
  }
  if (shakeBurstCount < 3) shakeBurstCount++;

  uint8_t reactionLevel = min((uint8_t)3, max(level, shakeBurstCount));
  animation.reactMotionShake(reactionLevel);
  if (earMgr.isEnabled()) earMgr.reactMotionShake(reactionLevel >= 3);

  if (reactionLevel >= 3) {
    // Level-3 shake is the actual rapid-shake/roll event. Route it to the
    // supplied full-frame Dizzy animation instead of the old expression-only
    // shake reaction.
    animation.reactMotionDizzy();
    buzzer.beepMotionDizzy();
    if (ledMgr.isEnabled()) ledMgr.playMotionShake(true);
  } else {
    buzzer.beepButton();
    if (ledMgr.isEnabled()) ledMgr.playMotionShake(false);
  }
}

void MotionManager::triggerTilt(int8_t dir) {
  unsigned long now = millis();
  // A tilt that has already been released is a fresh physical event. Do not
  // let the previous tilt's cooldown swallow that next event.
  if (lastTilt == -1) {
    lastTiltMs = now;
  } else if (!cooldownPassed(lastTiltMs, MOTION_TILT_COOLDOWN_MS)) {
    return;
  } else {
    lastTiltMs = now;
  }
  lastTilt = dir;
  tiltOccurred = true;

  switch (dir) {
    case 0: animation.reactMotionTiltFront(); if (earMgr.isEnabled()) earMgr.reactMotionFront(); break;
    case 1: animation.reactMotionTiltBack();  if (earMgr.isEnabled()) earMgr.reactMotionBack();  break;
    case 2: animation.reactMotionTiltLeft();  if (earMgr.isEnabled()) earMgr.reactMotionLeft();  break;
    case 3: animation.reactMotionTiltRight(); if (earMgr.isEnabled()) earMgr.reactMotionRight(); break;
  }
}

void MotionManager::triggerPickup() {
  unsigned long now = millis();
  if (!cooldownPassed(lastPickupMs, MOTION_PICKUP_COOLDOWN_MS)) return;
  lastPickupMs = now;
  liftCandidate = false;
  liftMotionSeen = false;
  tiltOccurred = false;
  carried = true;
  animation.reactMotionIdiot();
  if (earMgr.isEnabled()) earMgr.reactMotionPickup();
  buzzer.beepFun();
  if (ledMgr.isEnabled()) ledMgr.playWake();
}

void MotionManager::triggerLanding() {
  unsigned long now = millis();
  if (!cooldownPassed(lastLandingMs, MOTION_LANDING_COOLDOWN_MS)) return;
  lastLandingMs = now;
  liftCandidate = false;
  liftMotionSeen = false;
  tiltOccurred = false;
  carried = false;
  animation.reactMotionLanding();
  if (earMgr.isEnabled()) earMgr.reactMotionLanding();
  buzzer.beepMotionShock();
  if (ledMgr.isEnabled()) ledMgr.playMotionShock();
}

void MotionManager::triggerWake() {
  unsigned long now = millis();
  if (!cooldownPassed(lastWakeMs, MOTION_WAKE_COOLDOWN_MS)) return;
  lastWakeMs = now;
  if (animation.isAsleep()) {
    animation.notifyInteraction();
    if (earMgr.isEnabled()) earMgr.playWakeAnimation();
    if (ledMgr.isEnabled()) ledMgr.playWake();
  }
}

void MotionManager::handleMotion() {
  unsigned long now = millis();

  if (animation.isAsleep()) {
    if (fabsf(accelMag - 1.0f) > MOTION_WAKE_ACCEL_DELTA_G || gyroMag > MOTION_WAKE_GYRO_DPS) {
      triggerWake();
    }
    return;
  }

  float tiltFB = tiltFrontBackDeg();
  float tiltLR = tiltLeftRightDeg();
  bool nearGravity = accelMag > 0.82f && accelMag < 1.20f;
  float deltaAccel = fabsf(accelMag - prevAccelMag);
  float deltaZ = fabsf(az - prevZ);
  float dynamicAccel = fabsf(accelMag - accelBaseline);
  float dynamicZ = fabsf(az - 1.0f);
  prevZ = az;

  // ---------------------------------------------------------------
  // Pickup/landing owns vertical movement. This is intentionally before
  // generic shock/rotation so lifting the box cannot become "dizzy".
  // ---------------------------------------------------------------
  bool verticalLiftBurst = dynamicZ > MOTION_PICKUP_Z_G &&
                           accelMag > (1.0f + MOTION_PICKUP_ACCEL_G) &&
                           gyroMag > MOTION_PICKUP_MIN_GYRO_DPS;

  if (!carried && !liftCandidate && cooldownPassed(lastPickupMs, MOTION_PICKUP_COOLDOWN_MS) && verticalLiftBurst) {
    liftCandidate = true;
    liftMotionSeen = true;
    liftCandidateStart = now;
  }

  if (liftCandidate) {
    if (verticalLiftBurst || gyroMag > MOTION_PICKUP_MIN_GYRO_DPS) liftMotionSeen = true;

    // A lift is a burst followed by a quieter suspended state. Require both
    // the vertical component and gyro/movement evidence before confirming.
    if (liftMotionSeen && now - liftCandidateStart >= MOTION_PICKUP_CONFIRM_MS &&
        accelMag < MOTION_PICKUP_SETTLE_G && gyroMag < MOTION_PICKUP_SETTLE_GYRO_DPS) {
      triggerPickup();
      return;
    }
    if (now - liftCandidateStart > MOTION_PICKUP_TIMEOUT_MS) {
      liftCandidate = false;
      liftMotionSeen = false;
    }
  }

  // A carried box landing is a vertical impact, not a generic shock.
  if (carried && accelMag >= MOTION_LANDING_ACCEL_G && dynamicZ > MOTION_LANDING_DYNAMIC_Z_G) {
    triggerLanding();
    return;
  }

  // ---------------------------------------------------------------
  // Real rotation: require a stronger gyro burst and a little acceleration
  // evidence. This prevents hand pickup/touch from being classified as dizzy.
  // ---------------------------------------------------------------
  bool suddenRotation =
      (gyroMag >= MOTION_DIZZY_GYRO_DPS) ||
      (gyroMag >= MOTION_SUDDEN_GYRO_DPS &&
       (dynamicAccel > MOTION_ROTATION_ACCEL_G || deltaAccel > MOTION_ROTATION_DELTA_G));
  if (suddenRotation && !liftCandidate && !carried) {
    if (cooldownPassed(lastRotationMs, MOTION_ROTATION_COOLDOWN_MS)) {
      lastRotationMs = now;
      triggerShock(gyroMag >= MOTION_DIZZY_GYRO_DPS);
    }
    return;
  }

  // ---------------------------------------------------------------
  // Strong shake: count multiple distinct peaks before calling it a shake.
  // A single impact is therefore a tap/shock, never a full dizzy reaction.
  // ---------------------------------------------------------------
  bool strongShakePeak = !liftCandidate && !carried &&
                         (dynamicAccel >= MOTION_SHAKE_PEAK_ACCEL_G ||
                          (gyroMag >= MOTION_SHAKE_PEAK_GYRO_DPS && deltaAccel >= MOTION_SHAKE_PEAK_DELTA_G));

  if (strongShakePeak && cooldownPassed(lastShakeMs, MOTION_SHAKE_PEAK_DEBOUNCE_MS)) {
    if (shakePeakWindowStart == 0 || now - shakePeakWindowStart > MOTION_SHAKE_PEAK_WINDOW_MS) {
      shakePeakWindowStart = now;
      shakePeakCount = 0;
    }
    if (shakePeakCount < 5) shakePeakCount++;

    if (shakePeakCount >= MOTION_SHAKE_REQUIRED_PEAKS) {
      uint8_t level = (shakePeakCount >= MOTION_SHAKE_DIZZY_PEAKS || gyroMag >= MOTION_SHAKE_DIZZY_GYRO_DPS) ? 3 : 1;
      triggerShake(level);
      shakePeakCount = 0;
      shakePeakWindowStart = now;
      return;
    }
  }

  if (shakePeakWindowStart && now - shakePeakWindowStart > MOTION_SHAKE_PEAK_WINDOW_MS) {
    shakePeakCount = 0;
    shakePeakWindowStart = 0;
  }

  // ---------------------------------------------------------------
  // Sudden acceleration / impact. Pickup candidates are excluded.
  // ---------------------------------------------------------------
  bool suddenAccel = !liftCandidate &&
                     (accelMag >= MOTION_SUDDEN_ACCEL_G || dynamicAccel >= MOTION_SUDDEN_DYNAMIC_G);
  if (suddenAccel) {
    triggerShock(false);
    return;
  }

  // Light tap: deliberately lower energy than shake and with little gyro.
  if (!liftCandidate && !carried &&
      deltaZ >= MOTION_TAP_DELTA_G &&
      accelMag > MOTION_TAP_MIN_ACCEL_G &&
      accelMag < MOTION_TAP_MAX_ACCEL_G &&
      gyroMag < MOTION_TAP_MAX_GYRO_DPS) {
    triggerTap();
    return;
  }

  // ---------------------------------------------------------------
  // Tilt only when gravity is stable. A held tilt fires once and must
  // return through the release band before it can fire again.
  // ---------------------------------------------------------------
  if (nearGravity && !liftCandidate && !carried) {
    if (fabsf(tiltFB) >= MOTION_TILT_STRONG_DEG || fabsf(tiltLR) >= MOTION_TILT_STRONG_DEG) {
      if (lastTilt != 4 && cooldownPassed(lastTiltMs, MOTION_EXCESSIVE_TILT_COOLDOWN_MS)) {
        lastTiltMs = now;
        lastTilt = 4;
        tiltOccurred = true;
        animation.reactMotionWorried();
        if (earMgr.isEnabled()) earMgr.reactMotionWorried();
      }
    } else if (fabsf(tiltFB) >= MOTION_TILT_START_DEG || fabsf(tiltLR) >= MOTION_TILT_START_DEG) {
      int8_t dir = (fabsf(tiltFB) >= fabsf(tiltLR))
                     ? (tiltFB > 0 ? 1 : 0)
                     : (tiltLR > 0 ? 2 : 3);
      if (lastTilt != dir) triggerTilt(dir);
    } else if (fabsf(tiltFB) <= MOTION_TILT_RELEASE_DEG && fabsf(tiltLR) <= MOTION_TILT_RELEASE_DEG) {
      if (tiltOccurred) {
        tiltOccurred = false;
        animation.reactMotionStupid();
      }
      lastTilt = -1;
    }
  }
}

void MotionManager::update() {
#if MOTION_ENABLED
  if (!sensorOK) return;
  unsigned long now = millis();
  if (now - lastReadMs < MOTION_UPDATE_INTERVAL_MS) return;
  lastReadMs = now;
  if (!readSensor()) return;
  handleMotion();
#endif
}
