#include "WiFiManager.h"
#include "Config.h"

#if !DEMO_MODE
#include <WiFi.h>
#endif

WiFiManager::WiFiManager()
  : connecting(false), connectStartTime(0), lastAttemptTime(0) {}

void WiFiManager::begin() {
#if DEMO_MODE
  // Nothing to do — isConnected() always reports true below.
#else
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  connecting = true;
  connectStartTime = millis();
  lastAttemptTime = millis();
#endif
}

bool WiFiManager::isConnected() const {
#if DEMO_MODE
  return true; // simulated — everything downstream believes it has a link
#else
  return WiFi.status() == WL_CONNECTED;
#endif
}

void WiFiManager::update() {
#if DEMO_MODE
  return;
#else
  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    connecting = false;
    return;
  }

  if (connecting) {
    if (now - connectStartTime >= WIFI_CONNECT_TIMEOUT_MS) {
      // Give up this attempt; a fresh one will be tried on the next
      // WIFI_RECONNECT_INTERVAL_MS window below.
      connecting = false;
    }
    return;
  }

  // Not connected and not currently trying — retry periodically without
  // ever blocking the loop.
  if (now - lastAttemptTime >= WIFI_RECONNECT_INTERVAL_MS) {
    lastAttemptTime = now;
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    connecting = true;
    connectStartTime = now;
  }
#endif
}
