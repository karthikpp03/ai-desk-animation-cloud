#include "WiFiManager.h"
#include "Config.h"

#if !DEMO_MODE
#include <WiFi.h>
#endif

WiFiManager::WiFiManager()
  : connecting(false), connectStartTime(0), lastAttemptTime(0),
    lastHomeProbeTime(0), activeNetwork(255), attemptNetwork(255) {}

void WiFiManager::begin() {
#if DEMO_MODE
  // Nothing to do — isConnected() always reports true below.
#else
  WiFi.mode(WIFI_STA);
  beginAttempt(0, millis());
#endif
}

void WiFiManager::beginAttempt(uint8_t network, unsigned long now) {
#if !DEMO_MODE
  const char* ssid = network == 0 ? HOME_WIFI_SSID : HOTSPOT_WIFI_SSID;
  const char* password = network == 0 ? HOME_WIFI_PASSWORD : HOTSPOT_WIFI_PASSWORD;
  if (!ssid || !ssid[0]) {
    connecting = false;
    attemptNetwork = network;
    if (network == 0 && HOTSPOT_WIFI_SSID[0]) beginAttempt(1, now);
    return;
  }

  WiFi.disconnect(false, false);
  WiFi.begin(ssid, password);
  connecting = true;
  connectStartTime = now;
  lastAttemptTime = now;
  attemptNetwork = network;
#endif
}

void WiFiManager::startHomeAttempt(unsigned long now) {
#if !DEMO_MODE
  beginAttempt(0, now);
#endif
}

bool WiFiManager::isConnected() const {
#if DEMO_MODE
  return true;
#else
  return WiFi.status() == WL_CONNECTED;
#endif
}

String WiFiManager::localIP() const {
#if DEMO_MODE
  return String("127.0.0.1");
#else
  return isConnected() ? WiFi.localIP().toString() : String();
#endif
}

String WiFiManager::currentSSID() const {
#if DEMO_MODE
  return String("DEMO");
#else
  return isConnected() ? WiFi.SSID() : String();
#endif
}

void WiFiManager::update() {
#if DEMO_MODE
  return;
#else
  const unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (connecting) {
      connecting = false;
      activeNetwork = attemptNetwork;
      // When on the fallback network, periodically give the preferred Home WiFi
      // another chance. If it fails, beginAttempt(1) restores the hotspot.
      if (activeNetwork == 1) lastHomeProbeTime = now;
    }

    if (activeNetwork == 1 && HOTSPOT_WIFI_SSID[0] &&
        now - lastHomeProbeTime >= WIFI_HOME_RETRY_INTERVAL_MS) {
      lastHomeProbeTime = now;
      startHomeAttempt(now);
    }
    return;
  }

  if (connecting) {
    if (now - connectStartTime < WIFI_CONNECT_TIMEOUT_MS) return;

    connecting = false;
    // Home failed -> try Mobile Hotspot immediately. Hotspot failed -> retry
    // Home on the next cycle so the preferred network always gets priority.
    if (attemptNetwork == 0) {
      beginAttempt(1, now);
    } else {
      beginAttempt(0, now);
    }
    return;
  }

  if (now - lastAttemptTime < WIFI_RECONNECT_INTERVAL_MS) return;

  // Every fresh cycle starts with Home WiFi. This keeps the preferred network
  // first without blocking the main application loop.
  beginAttempt(0, now);
#endif
}
