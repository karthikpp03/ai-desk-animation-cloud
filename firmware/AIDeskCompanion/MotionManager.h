#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "Config.h"
#include "Animation.h"
#include "Buzzer.h"
#include "LEDManager.h"
#include "EarManager.h"

class MotionManager {
public:
  MotionManager(AnimationManager &animation, BuzzerManager &buzzer,
                LEDManager &leds, EarManager &ears);
  void begin();
  void update();
  bool isAvailable() const { return sensorOK; }
  bool isEnabled() const { return sensorOK && MOTION_ENABLED; }

  float accelX() const { return ax; }
  float accelY() const { return ay; }
  float accelZ() const { return az; }
  float gyroX() const { return gx; }
  float gyroY() const { return gy; }
  float gyroZ() const { return gz; }

private:
  AnimationManager &animation;
  BuzzerManager &buzzer;
  LEDManager &ledMgr;
  EarManager &earMgr;
  bool sensorOK;
  unsigned long lastReadMs;
  float ax, ay, az, gx, gy, gz;
  float accelMag, gyroMag, accelRawMag, prevAccelMag, prevZ;
  float accelBaseline, gyroBaseline;

  unsigned long lastTapMs, lastTiltMs, lastShockMs, lastRotationMs;
  unsigned long lastShakeMs, lastWakeMs, lastPickupMs, lastLandingMs;

  uint8_t shakePeakCount;
  unsigned long shakePeakWindowStart;
  uint8_t shakeBurstCount;
  unsigned long shakeBurstWindowStart;

  bool liftCandidate;
  unsigned long liftCandidateStart;
  bool liftMotionSeen;
  bool carried;
  bool tiltOccurred;
  int8_t lastTilt; // -1 none, 0 front, 1 back, 2 left, 3 right, 4 excessive

  bool readSensor();
  bool writeRegister(uint8_t reg, uint8_t value);
  uint8_t readRegister(uint8_t reg);
  bool readRegisters(uint8_t reg, uint8_t *buf, uint8_t len);
  float lerp(float current, float sample, float alpha) const;
  float tiltFrontBackDeg() const;
  float tiltLeftRightDeg() const;
  void handleMotion();
  void triggerTap();
  void triggerShock(bool rotation);
  void triggerShake(uint8_t level);
  void triggerTilt(int8_t dir);
  void triggerPickup();
  void triggerLanding();
  void triggerWake();
  bool cooldownPassed(unsigned long since, unsigned long cd) const;
};
