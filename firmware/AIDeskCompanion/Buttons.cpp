#include "Buttons.h"
#include "Config.h"

ButtonManager::ButtonManager(uint8_t p)
  : pin(p),
    lastReading(false),
    stableState(false),
    lastEdgeTime(0),
    pressStartTime(0),
    longPressFired(false),
    pendingClicks(0),
    pendingClickAt(0) {}

void ButtonManager::begin() {
  pinMode(pin, INPUT_PULLUP);
  lastReading = (digitalRead(pin) == LOW);
  stableState = lastReading;
  lastEdgeTime = millis();
}

ButtonEvent ButtonManager::update() {
  unsigned long now = millis();
  bool raw = (digitalRead(pin) == LOW); // active LOW

  if (raw != lastReading) {
    lastReading = raw;
    lastEdgeTime = now;
  }

  ButtonEvent event = ButtonEvent::NONE;

  if ((now - lastEdgeTime) >= BUTTON_DEBOUNCE_MS && raw != stableState) {
    stableState = raw;

    if (stableState) {
      // Just pressed.
      pressStartTime = now;
      longPressFired = false;
      // (click counting happens on release, below)
    } else {
      // Just released.
      if (!longPressFired) {
        if (pendingClicks > 0 && (now - pendingClickAt) < DOUBLE_PRESS_WINDOW_MS) {
          pendingClicks++;
        } else {
          pendingClicks = 1;
        }
        pendingClickAt = now;

#if ENABLE_ANIMATION_MODE
        if (pendingClicks >= 4) {
          // Four is now the highest click count — resolve immediately.
          // (3 clicks wait out the window below so a 4th can still arrive.)
          pendingClicks = 0;
          event = ButtonEvent::QUADRUPLE_PRESS;
        }
#else
        if (pendingClicks >= 3) {
          // Three is the highest click count this project uses — resolve
          // immediately rather than waiting out the window for nothing.
          pendingClicks = 0;
          event = ButtonEvent::TRIPLE_PRESS;
        }
#endif
      }
    }
  }

  // Long press fires while still held, without waiting for release. If a
  // pending click count was in flight (button pressed again quickly then
  // held long), that pending count is dropped — this is treated as a long
  // press, not a double/triple press.
  if (stableState && !longPressFired && (now - pressStartTime) >= LONG_PRESS_MS) {
    longPressFired = true;
    pendingClicks = 0;
    event = ButtonEvent::LONG_PRESS;
  }

  // No further click arrived in time -> resolve the pending count as a
  // plain short press (1 click) or a double press (2 clicks).
  if (pendingClicks > 0 && !stableState && (now - pendingClickAt) >= DOUBLE_PRESS_WINDOW_MS) {
    event = (pendingClicks == 1) ? ButtonEvent::SHORT_PRESS
          : (pendingClicks == 2) ? ButtonEvent::DOUBLE_PRESS
                                 : ButtonEvent::TRIPLE_PRESS;
    pendingClicks = 0;
  }

  return event;
}
