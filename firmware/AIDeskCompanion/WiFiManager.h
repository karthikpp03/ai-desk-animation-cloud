#pragma once
#include <Arduino.h>

// Wraps ESP32 Wi-Fi in a non-blocking state machine. In DEMO_MODE this
// class simply reports "connected" immediately without touching any
// radio hardware, so the rest of the app never needs to know the
// difference.
class WiFiManager {
public:
  WiFiManager();

  void begin();
  void update();

  bool isConnected() const;
  bool isConnecting() const { return connecting; }

private:
  bool connecting;
  unsigned long connectStartTime;
  unsigned long lastAttemptTime;
};
