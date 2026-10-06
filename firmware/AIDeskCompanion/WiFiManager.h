#pragma once
#include <Arduino.h>

class AsyncWebServer; // only used by reference; full type is included in WiFiManager.cpp

// Wraps ESP32 Wi-Fi in a non-blocking state machine. In DEMO_MODE this
// class simply reports "connected" immediately without touching any
// radio hardware, so the rest of the app never needs to know the
// difference.
//
// Connection priority: runtime-saved network (set via the setup page) ->
// Home Wi-Fi (secrets.h) -> Mobile Hotspot (secrets.h). If none connect for
// WIFI_SETUP_AP_AFTER_MS, a temporary setup access point is started.
class WiFiManager {
public:
  WiFiManager();

  void begin();
  void update();

  bool isConnected() const;
  bool isConnecting() const { return connecting; }
  String localIP() const;
  String currentSSID() const;
  // 0 = Home, 1 = Hotspot, 2 = runtime-saved network, 255 = none yet.
  uint8_t currentNetwork() const { return activeNetwork; }

  // True while the temporary "AIDeskCompanion-Setup" access point is up.
  bool isSetupMode() const { return setupMode; }
  // Adds the Wi-Fi setup routes to an existing server (call before server.begin()).
  void registerSetupRoutes(AsyncWebServer &server);

private:
  bool connecting;
  unsigned long connectStartTime;
  unsigned long lastAttemptTime;
  unsigned long lastHomeProbeTime;
  uint8_t activeNetwork;
  uint8_t attemptNetwork;

  void beginAttempt(uint8_t network, unsigned long now);
  void startHomeAttempt(unsigned long now); // starts the highest-priority network

  // ---- Runtime Wi-Fi setup (saved network + setup AP) ----
  bool hasSaved = false;
  char savedSsid[33] = "";
  char savedPass[64] = "";

  bool setupMode = false;
  bool setupRetrying = false;
  bool diagScan = false;           // one-shot scan report when the setup AP opens
  bool downTracking = false;
  unsigned long downSince = 0;
  unsigned long setupLastRetry = 0;
  unsigned long apStopAt = 0;      // 0 = no pending AP shutdown

  // Written by the web handlers, consumed by update() in loop().
  volatile bool pendingConnect = false;
  volatile bool pendingForget = false;
  volatile unsigned long forgetDueAt = 0;
  volatile uint8_t trialState = 0;  // 0 idle, 1 connecting, 2 connected, 3 failed
  bool trialActive = false;
  unsigned long trialStart = 0;
  char trialSsid[33] = "";
  char trialPass[64] = "";

  bool networkConfigured(uint8_t network) const;
  uint8_t firstNetwork() const;
  uint8_t nextNetwork(uint8_t after) const;
  uint8_t configuredCount() const;
  void loadSaved();
  void saveCredentials(const char *ssid, const char *pass);
  void processForget(unsigned long now);
  void startSetup(unsigned long now);
  void stopSetup();
  bool serviceSetup(unsigned long now); // true = run the normal reconnect logic too
};
