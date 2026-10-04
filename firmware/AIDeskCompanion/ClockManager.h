#pragma once
#include <Arduino.h>
#include "WiFiManager.h"

class ClockManager {
public:
  ClockManager(WiFiManager &wifi);

  void begin();
  void update();

  // Formatted for display — always safe to call, even before any sync.
  String getTimeString() const;   // "14:32"
  String getDayString() const;    // "Tuesday"
  int getHour24() const { return hour; }

private:
  WiFiManager &wifiMgr;
  bool synced;
  unsigned long lastSyncAttempt;
  unsigned long lastTickMillis;

  int hour, minute, second;
  int dayOfWeek; // 0 = Sunday

  void tickSimulatedClock();
  void trySyncNTP();
};
