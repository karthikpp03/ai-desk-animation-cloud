#pragma once
#include <Arduino.h>

// Simple active buzzer (just HIGH/LOW, no tone() needed). Patterns are
// arrays of alternating on/off durations in ms, stepped via millis() —
// never blocks the main loop.
class BuzzerManager {
public:
  explicit BuzzerManager(uint8_t pin);

  void begin();
  void update();

  void beepButton();        // very short soft tick — every short press
  void beepLock();          // confirmation — entering locked mode
  void beepUnlock();        // confirmation — leaving locked mode
  void beepNotification();  // reminder pattern — a few short pulses
  void beepError();         // longer low pattern
  void beepBoot();          // happy little chirp on startup
  void beepSuccess();       // AI response / success chirp
  void beepFun();           // playful little doodle — rare idle "special event" flourish
  void beepMotionShock();   // V5 physical shock / landing
  void beepMotionDizzy();   // V5 quick rotation / strong shake

  void stop();

private:
  uint8_t pin;
  const uint16_t *pattern;
  uint8_t patternLength;
  uint8_t patternIndex;
  unsigned long stepStart;
  bool active;

  void startPattern(const uint16_t *p, uint8_t len);
};
