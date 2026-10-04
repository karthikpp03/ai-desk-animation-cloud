#pragma once
#include <Arduino.h>
#include "WiFiManager.h"

enum class AIState { IDLE, REQUESTING, DONE, ERROR };

// A small, provider-independent "daily message" client. There is no
// microphone or speaker on this device, so this is a one-way, periodic
// fetch — not a conversation. Swap the request body in requestNow() to
// point at a different provider later without touching anything else.
class AIManager {
public:
  AIManager(WiFiManager &wifi);

  void begin();
  void update();

  String getMessage() const { return message; }
  AIState getState() const { return state; }

private:
  WiFiManager &wifiMgr;
  String message;
  AIState state;
  unsigned long lastRequestTime;
  int demoIndex;

  void requestDemo();
  void requestReal();
};
