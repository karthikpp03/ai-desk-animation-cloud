#include "AI.h"
#include "Config.h"

#if !DEMO_MODE
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#endif

static const char *DEMO_MESSAGES[] = {
  "Small steps still count.",
  "You've got this today.",
  "Take a breath. Then go.",
  "Progress, not perfection.",
  "Hydrate. Then conquer.",
};
static const int DEMO_MESSAGE_COUNT = sizeof(DEMO_MESSAGES) / sizeof(DEMO_MESSAGES[0]);

AIManager::AIManager(WiFiManager &wifi)
  : wifiMgr(wifi), message("Hi!"), state(AIState::IDLE),
    lastRequestTime(0), demoIndex(0) {}

void AIManager::begin() {
#if DEMO_MODE
  requestDemo();
#endif
}

void AIManager::requestDemo() {
  demoIndex = (demoIndex + 1) % DEMO_MESSAGE_COUNT;
  message = DEMO_MESSAGES[demoIndex];
  state = AIState::DONE;
}

void AIManager::requestReal() {
#if !DEMO_MODE
  if (!wifiMgr.isConnected()) {
    state = AIState::ERROR;
    return;
  }

  state = AIState::REQUESTING;

  WiFiClientSecure client;
  client.setInsecure(); // demo-grade TLS; pin a cert for production use
  client.setTimeout(5000);

  HTTPClient http;
  if (!http.begin(client, AI_API_ENDPOINT)) {
    state = AIState::ERROR;
    return;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", AI_API_KEY);
  http.addHeader("anthropic-version", "2023-06-01");

  StaticJsonDocument<512> req;
  req["model"] = "claude-sonnet-4-6";
  req["max_tokens"] = 40;
  JsonArray messages = req.createNestedArray("messages");
  JsonObject userMsg = messages.createNestedObject();
  userMsg["role"] = "user";
  userMsg["content"] = "Give me one short, upbeat one-line message for my day. Under 8 words. No quotes.";

  String body;
  serializeJson(req, body);

  // NOTE: this call blocks for the duration of one HTTPS round trip
  // (typically well under a second on a good link). It is scheduled at
  // most once per AI_MESSAGE_INTERVAL_MS, not every loop(), so the
  // character's continuous animation is unaffected in normal use.
  int code = http.POST(body);

  if (code != 200) {
    http.end();
    state = AIState::ERROR;
    return;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<1024> res;
  DeserializationError err = deserializeJson(res, payload);
  if (err) {
    state = AIState::ERROR;
    return;
  }

  const char *text = res["content"][0]["text"] | nullptr;
  if (text == nullptr) {
    state = AIState::ERROR;
    return;
  }

  message = String(text);
  message.trim();
  state = AIState::DONE;
#endif
}

void AIManager::update() {
  unsigned long now = millis();

#if DEMO_MODE
  if (lastRequestTime == 0 || now - lastRequestTime >= AI_MESSAGE_INTERVAL_MS) {
    lastRequestTime = now;
    requestDemo();
  }
#else
  if (lastRequestTime == 0 || now - lastRequestTime >= AI_MESSAGE_INTERVAL_MS) {
    lastRequestTime = now;
    requestReal();
  }
#endif
}
