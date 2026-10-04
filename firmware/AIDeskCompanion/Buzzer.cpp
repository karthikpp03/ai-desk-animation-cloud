#include "Buzzer.h"

// Each pattern is a sequence of durations in ms, alternating ON, OFF, ON, OFF...
// starting with ON. The buzzer pin is driven HIGH during "on" segments.
static const uint16_t PATTERN_BUTTON[]       = { 25 };
static const uint16_t PATTERN_LOCK[]         = { 60, 40, 60 };
static const uint16_t PATTERN_UNLOCK[]       = { 60, 40, 90 };
static const uint16_t PATTERN_NOTIFICATION[] = { 80, 60, 80, 60, 80 };
static const uint16_t PATTERN_ERROR[]        = { 200, 80, 200 };
static const uint16_t PATTERN_BOOT[]         = { 40, 30, 40, 30, 90 };
static const uint16_t PATTERN_SUCCESS[]      = { 50, 30, 100 };
static const uint16_t PATTERN_FUN[]          = { 35, 35, 35, 35, 35, 35, 90 };
static const uint16_t PATTERN_MOTION_SHOCK[] = { 45, 35, 90 };
static const uint16_t PATTERN_MOTION_DIZZY[] = { 55, 45, 55, 45, 110 };

BuzzerManager::BuzzerManager(uint8_t p)
  : pin(p), pattern(nullptr), patternLength(0), patternIndex(0),
    stepStart(0), active(false) {}

void BuzzerManager::begin() {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void BuzzerManager::startPattern(const uint16_t *p, uint8_t len) {
  pattern = p;
  patternLength = len;
  patternIndex = 0;
  stepStart = millis();
  active = true;
  digitalWrite(pin, HIGH); // index 0 is always an "on" segment
}

void BuzzerManager::update() {
  if (!active) return;

  unsigned long now = millis();
  if ((now - stepStart) >= pattern[patternIndex]) {
    patternIndex++;
    stepStart = now;

    if (patternIndex >= patternLength) {
      active = false;
      digitalWrite(pin, LOW);
      return;
    }
    // even index = on segment, odd index = off segment
    digitalWrite(pin, (patternIndex % 2 == 0) ? HIGH : LOW);
  }
}

void BuzzerManager::stop() {
  active = false;
  digitalWrite(pin, LOW);
}

void BuzzerManager::beepButton()       { startPattern(PATTERN_BUTTON, sizeof(PATTERN_BUTTON) / sizeof(uint16_t)); }
void BuzzerManager::beepLock()         { startPattern(PATTERN_LOCK, sizeof(PATTERN_LOCK) / sizeof(uint16_t)); }
void BuzzerManager::beepUnlock()       { startPattern(PATTERN_UNLOCK, sizeof(PATTERN_UNLOCK) / sizeof(uint16_t)); }
void BuzzerManager::beepNotification() { startPattern(PATTERN_NOTIFICATION, sizeof(PATTERN_NOTIFICATION) / sizeof(uint16_t)); }
void BuzzerManager::beepError()        { startPattern(PATTERN_ERROR, sizeof(PATTERN_ERROR) / sizeof(uint16_t)); }
void BuzzerManager::beepBoot()         { startPattern(PATTERN_BOOT, sizeof(PATTERN_BOOT) / sizeof(uint16_t)); }
void BuzzerManager::beepSuccess()      { startPattern(PATTERN_SUCCESS, sizeof(PATTERN_SUCCESS) / sizeof(uint16_t)); }
void BuzzerManager::beepFun()          { startPattern(PATTERN_FUN, sizeof(PATTERN_FUN) / sizeof(uint16_t)); }
void BuzzerManager::beepMotionShock()   { startPattern(PATTERN_MOTION_SHOCK, sizeof(PATTERN_MOTION_SHOCK) / sizeof(uint16_t)); }
void BuzzerManager::beepMotionDizzy()   { startPattern(PATTERN_MOTION_DIZZY, sizeof(PATTERN_MOTION_DIZZY) / sizeof(uint16_t)); }
