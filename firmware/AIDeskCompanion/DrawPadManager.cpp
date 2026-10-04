#include "DrawPadManager.h"
#include "Config.h"
#include "DrawPadPage.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsClient.h>

namespace {
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WebSocketsClient relay;
constexpr uint32_t CLIENT_IDLE_TIMEOUT_MS = 30000UL;
constexpr uint32_t RELAY_RECONNECT_MS = 5000UL;

#ifndef SECRET_API_BASE
#define SECRET_API_BASE ""
#endif
#ifndef SECRET_DEVICE_TOKEN
#define SECRET_DEVICE_TOKEN ""
#endif
const char *RELAY_API_BASE = SECRET_API_BASE;
const char *RELAY_DEVICE_TOKEN = SECRET_DEVICE_TOKEN;
}

DrawPadManager* DrawPadManager::instance = nullptr;

DrawPadManager::DrawPadManager(Adafruit_SSD1306 &d, WiFiManager &w)
  : display(d), wifi(w) {}

void DrawPadManager::begin() {
  instance = this;
  frameMutex = xSemaphoreCreateMutex();
  memset(framebuffer, 0, sizeof(framebuffer));

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!instance) return;
    instance->recordEvent("opened");
    instance->setModeRequest(1);
    request->send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });

  server.on("/event", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (instance && request->hasParam("type")) {
      instance->handleWebEvent(request->getParam("type")->value().c_str());
    }
    request->send(204);
  });

  ws.onEvent(DrawPadManager::onWsEvent);
  server.addHandler(&ws);
  server.begin();

  // Global Draw Pad: ESP32 opens an outbound WSS connection to the existing
  // Cloudflare Worker, so browsers never need the ESP32's private IP.
  if (RELAY_API_BASE[0] && RELAY_DEVICE_TOKEN[0]) {
    String base = String(RELAY_API_BASE);
    base.replace("https://", "");
    base.replace("http://", "");
    int slash = base.indexOf('/');
    if (slash >= 0) base.remove(slash);
    relay.setExtraHeaders((String("X-Device-Token: ") + RELAY_DEVICE_TOKEN).c_str());
    relay.setReconnectInterval(RELAY_RECONNECT_MS);
    relay.onEvent(DrawPadManager::onRelayEvent);
    relay.beginSSL(base.c_str(), 443, "/api/drawpad/device");
    xTaskCreatePinnedToCore(DrawPadManager::relayTask, "drawRelay", 8192, this, 1, &relayTaskHandle, 0);
  }
}

void DrawPadManager::update() {
  ws.cleanupClients();

  // A browser closing the Draw Pad releases the OLED from the WebSocket
  // disconnect callback. The timeout remains only as a stale-client safety net.
  if (active && !remotePadActive && clients == 0 && lastClientChangeAt &&
      millis() - lastClientChangeAt >= CLIENT_IDLE_TIMEOUT_MS) {
    modeRequest = 0;
  }

  if (!active || !dirty || !frameMutex) return;

  uint8_t copy[1024];
  if (xSemaphoreTake(frameMutex, 0) != pdTRUE) return;
  memcpy(copy, framebuffer, sizeof(copy));
  dirty = false;
  xSemaphoreGive(frameMutex);

  display.clearDisplay();
  display.drawBitmap(0, 0, copy, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.display();
}

int8_t DrawPadManager::takeModeRequest() {
  int8_t request = modeRequest;
  if (request >= 0) modeRequest = -1;
  return request;
}

void DrawPadManager::setModeRequest(int8_t request) {
  modeRequest = request;
  lastClientChangeAt = millis();
}

void DrawPadManager::activate() {
  if (active) return;
  active = true;
  if (frameMutex && xSemaphoreTake(frameMutex, portMAX_DELAY) == pdTRUE) {
    memset(framebuffer, 0, sizeof(framebuffer));
    dirty = true;
    xSemaphoreGive(frameMutex);
  }
}

void DrawPadManager::deactivate() {
  if (!active) return;
  active = false;
  clients = 0;
  lastClientChangeAt = millis();
  if (frameMutex && xSemaphoreTake(frameMutex, portMAX_DELAY) == pdTRUE) {
    memset(framebuffer, 0, sizeof(framebuffer));
    dirty = false;
    xSemaphoreGive(frameMutex);
  }
}

void DrawPadManager::recordEvent(const char *name) {
  lastEventId++;
  strncpy(eventName, name ? name : "event", sizeof(eventName) - 1);
  eventName[sizeof(eventName) - 1] = '\0';
}

void DrawPadManager::handleWebEvent(const char *type) {
  if (!type) return;
  if (!strcmp(type, "failed")) recordEvent("failed");
}

void DrawPadManager::setPixel(int x, int y, bool on) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
  const size_t index = static_cast<size_t>(y) * 16 + (x >> 3);
  const uint8_t mask = static_cast<uint8_t>(0x80 >> (x & 7));
  if (on) framebuffer[index] |= mask;
  else framebuffer[index] &= static_cast<uint8_t>(~mask);
}

void DrawPadManager::drawThickLine(int x0, int y0, int x1, int y1, int size) {
  x0 = constrain(x0, 0, SCREEN_WIDTH - 1);
  x1 = constrain(x1, 0, SCREEN_WIDTH - 1);
  y0 = constrain(y0, 0, SCREEN_HEIGHT - 1);
  y1 = constrain(y1, 0, SCREEN_HEIGHT - 1);
  size = constrain(size, 1, 8);

  const int r = size / 2;
  int dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
  int dy = -abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy;

  while (true) {
    for (int yy = -r; yy <= r; ++yy) {
      for (int xx = -r; xx <= r; ++xx) {
        if (xx * xx + yy * yy <= r * r) setPixel(x0 + xx, y0 + yy);
      }
    }
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
  dirty = true;
}

void DrawPadManager::drawDot(int x, int y, int size) {
  x = constrain(x, 0, SCREEN_WIDTH - 1);
  y = constrain(y, 0, SCREEN_HEIGHT - 1);
  size = constrain(size, 1, 8);
  const int r = max(0, size / 2);
  for (int yy = -r; yy <= r; ++yy) {
    for (int xx = -r; xx <= r; ++xx) {
      if (xx * xx + yy * yy <= r * r) setPixel(x + xx, y + yy);
    }
  }
  dirty = true;
}

void DrawPadManager::clearPanel() {
  memset(framebuffer, 0, sizeof(framebuffer));
  dirty = true;
  recordEvent("cleared");
}

void DrawPadManager::handleCommand(char *cmd) {
  char *type = strtok(cmd, ",");
  if (!type) return;

  if (strcmp(type, "CLR") == 0) {
    clearPanel();
  } else if (strcmp(type, "L") == 0) {
    char *a = strtok(nullptr, ","); char *b = strtok(nullptr, ",");
    char *c = strtok(nullptr, ","); char *d = strtok(nullptr, ",");
    char *e = strtok(nullptr, ",");
    if (a && b && c && d && e) drawThickLine(atoi(a), atoi(b), atoi(c), atoi(d), atoi(e));
  } else if (strcmp(type, "P") == 0) {
    char *a = strtok(nullptr, ","); char *b = strtok(nullptr, ",");
    char *c = strtok(nullptr, ",");
    if (a && b && c) drawDot(atoi(a), atoi(b), atoi(c));
  }
}

void DrawPadManager::handleMessage(uint8_t *data, size_t len) {
  static char buf[600];
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, data, len);
  buf[len] = '\0';

  char *saveptr = nullptr;
  char *token = strtok_r(buf, ";", &saveptr);
  if (!frameMutex || xSemaphoreTake(frameMutex, portMAX_DELAY) != pdTRUE) return;
  while (token) {
    handleCommand(token);
    token = strtok_r(nullptr, ";", &saveptr);
  }
  xSemaphoreGive(frameMutex);
}

void DrawPadManager::handleRelayMessage(uint8_t *data, size_t len) {
  static char buf[600];
  if (!data || !len) return;
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, data, len);
  buf[len] = '\0';

  if (strstr(buf, "\"type\":\"open\"")) {
    remotePadActive = true;
    recordEvent("opened");
    setModeRequest(1);
    return;
  }
  if (strstr(buf, "\"type\":\"release\"")) {
    remotePadActive = false;
    recordEvent("disconnected");
    setModeRequest(0);
    return;
  }
  if (strstr(buf, "\"type\":\"device-online\"")) return;
  if (strstr(buf, "\"type\":\"device-offline\"")) return;

  // Everything else from the authenticated browser session is a Draw Pad
  // command (CLR / P / L) and follows the same local command path.
  handleMessage(data, len);
}

void DrawPadManager::relayTask(void *arg) {
  auto *self = static_cast<DrawPadManager*>(arg);
  (void)self;
  for (;;) {
    if (WiFi.status() == WL_CONNECTED && RELAY_API_BASE[0] && RELAY_DEVICE_TOKEN[0]) {
      relay.loop();
      vTaskDelay(pdMS_TO_TICKS(10));
    } else {
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}

void DrawPadManager::onRelayEvent(WStype_t type, uint8_t *payload, size_t length) {
  if (!instance) return;
  if (type == WStype_CONNECTED) {
    instance->remoteRelayConnected = true;
    return;
  }
  if (type == WStype_DISCONNECTED || type == WStype_ERROR) {
    instance->remoteRelayConnected = false;
    if (instance->remotePadActive) {
      instance->remotePadActive = false;
      instance->setModeRequest(0);
    }
    return;
  }
  if (type == WStype_TEXT) {
    instance->handleRelayMessage(payload, length);
  }
}

void DrawPadManager::onWsEvent(AsyncWebSocket *serverPtr, AsyncWebSocketClient *client,
                               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  (void)serverPtr;
  if (!instance) return;

  if (type == WS_EVT_CONNECT) {
    instance->clients++;
    instance->lastClientChangeAt = millis();
    instance->recordEvent("connected");
  } else if (type == WS_EVT_DISCONNECT) {
    if (instance->clients) instance->clients--;
    instance->lastClientChangeAt = millis();
    if (instance->clients == 0) {
      instance->recordEvent("disconnected");
      if (!instance->remotePadActive) instance->setModeRequest(0);
    }
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = static_cast<AwsFrameInfo*>(arg);
    if (info && info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      instance->handleMessage(data, len);
    }
  }
}
