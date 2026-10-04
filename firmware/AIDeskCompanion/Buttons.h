#pragma once
#include <Arduino.h>

// SHORT_PRESS and LONG_PRESS behave exactly as in V1. DOUBLE_PRESS was
// added in V2 (Mode 1 <-> Mode 2 LED-character switch). TRIPLE_PRESS is
// new in V3 (Ear Mode ON/OFF toggle) — see Buttons.cpp for how a run of
// 1/2/3 clicks is told apart with a single generic click counter.
// QUADRUPLE_PRESS enters/exits Animation Display Mode (only produced when
// ENABLE_ANIMATION_MODE is true; otherwise 3 clicks resolve immediately
// exactly as before).
enum class ButtonEvent { NONE, SHORT_PRESS, LONG_PRESS, DOUBLE_PRESS, TRIPLE_PRESS, QUADRUPLE_PRESS };

// One physical button (INPUT_PULLUP, active LOW), fully debounced,
// millis()-based, no delay(). Call update() every loop(); it returns
// an event exactly once, at the moment the event fires.
//
// NOTE on latency: to be able to tell a single short press apart from
// the first click of a double/triple press, delivery of SHORT_PRESS (and
// DOUBLE_PRESS) is held for up to DOUBLE_PRESS_WINDOW_MS after release.
// Each release within that window bumps a click counter instead of
// resolving immediately; if no further press arrives before the window
// closes, the counter resolves to SHORT_PRESS (1 click) or DOUBLE_PRESS
// (2 clicks). A 3rd click resolves TRIPLE_PRESS immediately, since three
// is the highest click count this project uses — no need to keep
// waiting once it's reached. LONG_PRESS is unaffected — it still fires
// immediately while the button is held, exactly as V1/V2, and clears any
// pending click count.
class ButtonManager {
public:
  explicit ButtonManager(uint8_t pin);

  void begin();
  ButtonEvent update();

  bool isPressed() const { return stableState; }

private:
  uint8_t pin;
  bool lastReading;
  bool stableState;         // debounced state: true = pressed
  unsigned long lastEdgeTime;
  unsigned long pressStartTime;
  bool longPressFired;

  // Multi-click bookkeeping (covers single/double/triple).
  uint8_t pendingClicks;        // clicks counted so far in the current run
  unsigned long pendingClickAt; // when the most recent release happened
};
