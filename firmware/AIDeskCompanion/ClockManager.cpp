#include "ClockManager.h"
#include "Config.h"

#if !DEMO_MODE
#include <time.h>
#endif

static const char *DAY_NAMES[] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

ClockManager::ClockManager(WiFiManager &wifi)
  : wifiMgr(wifi), synced(false), lastSyncAttempt(0), lastTickMillis(0),
    hour(9), minute(41), second(0), dayOfWeek(2) {} // demo default: Tue 09:41

void ClockManager::begin() {
  lastTickMillis = millis();
#if DEMO_MODE
  synced = true; // the simulated clock is always "good enough"
#else
  trySyncNTP();
#endif
}

void ClockManager::trySyncNTP() {
#if !DEMO_MODE
  if (!wifiMgr.isConnected()) return;
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 2000)) {
    hour = timeinfo.tm_hour;
    minute = timeinfo.tm_min;
    second = timeinfo.tm_sec;
    dayOfWeek = timeinfo.tm_wday;
    synced = true;
  }
#endif
}

void ClockManager::tickSimulatedClock() {
  unsigned long now = millis();
  // Advance the simulated clock in real time so DEMO_MODE still looks alive.
  while (now - lastTickMillis >= 1000) {
    lastTickMillis += 1000;
    second++;
    if (second >= 60) { second = 0; minute++; }
    if (minute >= 60) { minute = 0; hour++; }
    if (hour >= 24) { hour = 0; dayOfWeek = (dayOfWeek + 1) % 7; }
  }
}

void ClockManager::update() {
#if DEMO_MODE
  tickSimulatedClock();
#else
  // Keep local h:m:s ticking between NTP syncs so the clock is smooth
  // even if the network is flaky.
  tickSimulatedClock();

  static unsigned long lastAttempt = 0;
  unsigned long now = millis();
  if (!synced || (now - lastAttempt > 3600000UL)) { // re-sync hourly
    if (wifiMgr.isConnected() && (now - lastAttempt > 5000)) {
      lastAttempt = now;
      trySyncNTP();
    }
  }
#endif
}

String ClockManager::getTimeString() const {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
  return String(buf);
}

String ClockManager::getDayString() const {
  return String(DAY_NAMES[dayOfWeek % 7]);
}
