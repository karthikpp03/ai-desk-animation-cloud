#pragma once

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsClient.h>
#include "WiFiManager.h"

// Lightweight local web drawing pad. The network callbacks only update a
// 128x64 1bpp framebuffer; OLED I2C writes stay in loop() so the Draw Pad
// cannot race the normal/animation display owners.
class DrawPadManager {
public:
  DrawPadManager(Adafruit_SSD1306 &display, WiFiManager &wifi);

  void begin();
  void update();

  // 1 = enter DRAW_PAD, 0 = leave DRAW_PAD, -1 = no request.
  int8_t takeModeRequest();
  void activate();
  void deactivate();

  bool isActive() const { return active; }
  uint8_t clientCount() const { return clients; }
  uint32_t eventId() const { return lastEventId; }
  const char* lastEvent() const { return eventName; }

private:
  Adafruit_SSD1306 &display;
  WiFiManager &wifi;

  bool active = false;
  volatile int8_t modeRequest = -1;
  volatile uint8_t clients = 0;
  volatile bool dirty = false;
  volatile uint32_t lastClientChangeAt = 0;
  volatile bool remotePadActive = false;
  volatile bool remoteRelayConnected = false;

  uint8_t framebuffer[1024]{};
  SemaphoreHandle_t frameMutex = nullptr;

  uint32_t lastEventId = 0;
  char eventName[32] = "";

  void setModeRequest(int8_t request);
  void recordEvent(const char *name);
  void handleCommand(char *cmd);
  void handleMessage(uint8_t *data, size_t len);
  void handleWebEvent(const char *type);
  void handleRelayMessage(uint8_t *data, size_t len);
  void drawThickLine(int x0, int y0, int x1, int y1, int size);
  void drawDot(int x, int y, int size);
  void clearPanel();
  void setPixel(int x, int y, bool on = true);
  bool hasClient() const { return clients > 0; }

  static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                        AwsEventType type, void *arg, uint8_t *data, size_t len);
  static void onRelayEvent(WStype_t type, uint8_t *payload, size_t length);
  static void relayTask(void *arg);
  TaskHandle_t relayTaskHandle = nullptr;
  static DrawPadManager *instance;
};
