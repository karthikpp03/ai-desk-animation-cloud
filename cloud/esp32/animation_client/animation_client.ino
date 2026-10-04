/*
  AI Desk Companion - Standalone Cloud Animation Client V2
  Hardware: ESP32 + SSD1306 128x64 OLED

  Required Arduino libraries:
    - Adafruit GFX Library
    - Adafruit SSD1306
    - ArduinoJson

  The ESP32 polls the Cloudflare Worker for commands. It downloads only the
  selected animation into LittleFS, then plays it locally on the OLED.

  This firmware is standalone and must NOT be merged into AIDeskCompanion V5 yet.
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Wi-Fi credentials, API_BASE and DEVICE_TOKEN come from secrets.h, which is GENERATED from the
// local .env file (run `npm run gen:firmware`) and is git-ignored. Never type them in this file.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h is missing. Fill in .env, then run: npm run gen:firmware"
#endif

const char* WIFI_SSID = SECRET_WIFI_SSID;
const char* WIFI_PASSWORD = SECRET_WIFI_PASSWORD;
const char* API_BASE = SECRET_API_BASE;
const char* DEVICE_TOKEN = SECRET_DEVICE_TOKEN;

// I2C wiring and speed. 100 kHz (the Wire default) needs ~95 ms to push one 1024-byte frame, which caps
// playback near 10 FPS. 400 kHz needs ~25 ms, which is comfortable for 15-30 FPS animations.
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define I2C_CLOCK_HZ 400000UL

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDR 0x3C

// clkDuring == clkAfter so the library does not drop the bus back to 100 kHz after every frame.
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET, I2C_CLOCK_HZ, I2C_CLOCK_HZ);

static const size_t FRAME_BYTES = 1024;
static const uint16_t MAX_FRAMES = 600;
static const uint32_t COMMAND_POLL_MS = 1000;
static const uint32_t HEARTBEAT_MS = 15000;
static const uint32_t HTTP_TIMEOUT_MS = 10000;
static const char* CACHE_FILE = "/animation.bin";
static const char* NEXT_FILE = "/next.bin";
static const bool PLAYBACK_STATS = true;  // prints measured FPS to Serial every 5 s

// ---------------------------------------------------------------------------------------------
// Threading model
//   loop()        (core 1): ONLY draws frames. Never touches the network, so a slow HTTPS request can
//                 no longer freeze the animation.
//   networkTask() (core 0): Wi-Fi, command polling, heartbeat, downloads, slideshow list.
// Shared playback state is guarded by stateMutex. Only loop() talks to the display (and I2C).
// ---------------------------------------------------------------------------------------------
SemaphoreHandle_t stateMutex;

// Guarded by stateMutex
File cacheFile;
uint16_t cachedFrames = 0;
uint16_t frameIndex = 0;
uint32_t frameIntervalMs = 100;
uint32_t lastFrameAt = 0;

// Cross-task flags
volatile bool clearRequested = false;     // network task -> loop(): blank the display
volatile bool slideshowAdvance = false;   // loop() -> network task: one full loop finished
volatile bool slideshowMode = false;      // network task -> loop()
volatile bool playbackFailed = false;     // loop() -> network task: cache unreadable, reload it

// Owned by the network task
String currentAnimationId;
String currentCommand = "stop";
uint32_t lastCommandPoll = 0;
uint32_t lastHeartbeat = 0;

// Slideshow state is kept in RAM only. The complete animation library is not cached.
JsonDocument slideshowDoc;
JsonArray slideshowAnimations;
size_t slideshowIndex = 0;
bool slideshowLoaded = false;

static uint8_t frameBuf[FRAME_BYTES];

struct AnimationMeta {
  String id;
  String name;
  uint16_t frames = 0;
  float fps = 10.0f;
  uint32_t frameDurationMs = 100;
};

bool apiGet(const String& path, String& body) {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(String(API_BASE) + path);
  http.addHeader("X-Device-Token", DEVICE_TOKEN);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("GET %s failed: %d\n", path.c_str(), code);
    http.end();
    return false;
  }
  body = http.getString();
  http.end();
  return true;
}

bool sendAck(const String& animationId) {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(String(API_BASE) + "/api/device/ack");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_TOKEN);
  String body = String("{\"animationId\":\"") + animationId + "\"}";
  int code = http.POST(body);
  http.end();
  return code >= 200 && code < 300;
}

// Stops playback and asks loop() to blank the OLED. Safe to call from the network task.
void stopPlayback() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (cacheFile) cacheFile.close();
  cachedFrames = 0;
  frameIndex = 0;
  xSemaphoreGive(stateMutex);
  currentAnimationId = "";
  clearRequested = true;
}

bool getMetadata(const String& animationId, AnimationMeta& meta) {
  String body;
  if (!apiGet(String("/api/animations/") + animationId + "/metadata", body)) return false;

  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  meta.id = doc["id"] | animationId;
  meta.name = doc["name"] | "animation";
  meta.frames = doc["frames"] | 0;
  meta.fps = doc["fps"] | 10.0f;
  meta.frameDurationMs = doc["frameDurationMs"] | (uint32_t)0;

  if (!meta.frames || meta.frames > MAX_FRAMES) return false;
  // Prefer the exact per-frame duration; derive it from FPS if only that is present.
  if (meta.frameDurationMs == 0 && meta.fps > 0.0f) meta.frameDurationMs = (uint32_t)(1000.0f / meta.fps + 0.5f);
  if (meta.frameDurationMs < 1 || meta.frameDurationMs > 60000) meta.frameDurationMs = 100;
  return true;
}

bool downloadFrames(const String& animationId, const AnimationMeta& meta) {
  if (!LittleFS.begin(true)) return false;

  // Download into a temp file so the animation that is currently playing keeps running meanwhile.
  const size_t expected = (size_t)meta.frames * FRAME_BYTES;
  if (LittleFS.totalBytes() - LittleFS.usedBytes() < expected + 4096) {
    // Not enough room for two copies: drop the playing animation to make space.
    stopPlayback();
    LittleFS.remove(NEXT_FILE);
  }
  LittleFS.remove(NEXT_FILE);
  File out = LittleFS.open(NEXT_FILE, FILE_WRITE);
  if (!out) {
    Serial.println("Could not open LittleFS cache");
    return false;
  }

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = String(API_BASE) + "/api/animations/" + animationId + "/frames";
  http.begin(url);
  http.addHeader("X-Device-Token", DEVICE_TOKEN);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("Frame download failed: %d\n", code);
    http.end();
    out.close();
    LittleFS.remove(NEXT_FILE);
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[2048];
  size_t total = 0;
  uint32_t lastData = millis();

  while (http.connected() && total < expected) {
    size_t available = stream->available();
    if (available) {
      size_t want = min(available, sizeof(buf));
      int n = stream->readBytes(buf, want);
      if (n > 0) {
        out.write(buf, n);
        total += n;
        lastData = millis();
      }
    } else if (millis() - lastData > HTTP_TIMEOUT_MS) {
      Serial.println("Frame download timed out");
      break;
    } else {
      delay(1);
    }
  }

  http.end();
  out.close();

  if (total != expected) {
    Serial.printf("Frame size mismatch: %u / %u bytes\n", (unsigned)total, (unsigned)expected);
    LittleFS.remove(NEXT_FILE);
    return false;
  }
  return true;
}

// Downloads the animation, then swaps it in between two frames (a few milliseconds under the mutex).
bool loadAnimation(const String& animationId) {
  AnimationMeta meta;
  if (!getMetadata(animationId, meta)) return false;
  Serial.printf("Loading %s (%u frames, %lu ms/frame = %.2f FPS)\n", meta.name.c_str(), meta.frames,
                (unsigned long)meta.frameDurationMs, 1000.0f / meta.frameDurationMs);

  if (!downloadFrames(animationId, meta)) return false;

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (cacheFile) cacheFile.close();
  LittleFS.remove(CACHE_FILE);
  bool ok = LittleFS.rename(NEXT_FILE, CACHE_FILE);
  if (ok) cacheFile = LittleFS.open(CACHE_FILE, FILE_READ);
  if (!ok || !cacheFile) {
    cachedFrames = 0;
    xSemaphoreGive(stateMutex);
    clearRequested = true;
    return false;
  }
  cachedFrames = meta.frames;
  frameIntervalMs = meta.frameDurationMs;
  frameIndex = 0;
  lastFrameAt = millis() - frameIntervalMs;  // first frame is due immediately
  xSemaphoreGive(stateMutex);

  currentAnimationId = animationId;
  return true;
}

bool loadSlideshowList() {
  String body;
  if (!apiGet("/api/animations", body)) return false;

  slideshowDoc.clear();
  if (deserializeJson(slideshowDoc, body)) return false;
  slideshowAnimations = slideshowDoc["animations"].as<JsonArray>();
  if (slideshowAnimations.isNull() || slideshowAnimations.size() == 0) return false;

  slideshowIndex = 0;
  slideshowLoaded = true;
  return true;
}

// Tries each animation at most once, starting at slideshowIndex.
bool startNextSlideshowAnimation() {
  if (!slideshowLoaded || slideshowAnimations.isNull() || slideshowAnimations.size() == 0) return false;

  const size_t count = slideshowAnimations.size();
  for (size_t tried = 0; tried < count; tried++) {
    if (slideshowIndex >= count) slideshowIndex = 0;
    String id = slideshowAnimations[slideshowIndex]["id"] | "";
    if (id.length()) {
      if (id == currentAnimationId && cachedFrames) {  // single-item list: just keep looping it
        sendAck(id);
        return true;
      }
      if (loadAnimation(id)) {
        Serial.printf("Slideshow: %s\n", id.c_str());
        sendAck(id);
        return true;
      }
      Serial.printf("Skipping slideshow animation: %s\n", id.c_str());
    }
    slideshowIndex++;
  }
  return false;
}

bool startSlideshow() {
  slideshowLoaded = false;
  if (!loadSlideshowList()) return false;
  return startNextSlideshowAnimation();
}

bool pollCommand(bool force = false) {
  if (!force && millis() - lastCommandPoll < COMMAND_POLL_MS) return true;
  lastCommandPoll = millis();

  String body;
  if (!apiGet("/api/device/command", body)) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("Command JSON error: %s\n", err.c_str());
    return false;
  }

  String requestedCommand = doc["command"] | "stop";
  String requestedId = doc["animationId"] | "";
  String previousCommand = currentCommand;

  if (requestedCommand == "stop") {
    currentCommand = "stop";
    slideshowMode = false;
    slideshowLoaded = false;
    if (previousCommand != "stop" || currentAnimationId.length() || cachedFrames) stopPlayback();
    return true;
  }

  if (requestedCommand == "play" && requestedId.length()) {
    currentCommand = "play";
    slideshowMode = false;
    slideshowLoaded = false;
    if (requestedId != currentAnimationId) {
      // The previous animation keeps playing until the new one is fully downloaded.
      if (loadAnimation(requestedId)) {
        sendAck(currentAnimationId);
      } else {
        Serial.printf("Could not load animation: %s\n", requestedId.c_str());
        stopPlayback();
      }
    }
    return true;
  }

  if (requestedCommand == "slideshow") {
    currentCommand = "slideshow";
    slideshowMode = true;
    if (previousCommand != "slideshow" || !slideshowLoaded || !cachedFrames) {
      if (!startSlideshow()) {
        Serial.println("Could not start slideshow");
        slideshowLoaded = false;
      }
    }
    return true;
  }

  return false;
}

void slideshowTick() {
  if (!slideshowAdvance) return;
  slideshowAdvance = false;
  if (currentCommand != "slideshow" || !slideshowLoaded) return;
  slideshowIndex++;
  if (!startNextSlideshowAnimation()) {
    slideshowIndex = 0;
    slideshowLoaded = false;
  }
}

void heartbeatTick() {
  if (!currentAnimationId.length()) return;
  if (millis() - lastHeartbeat < HEARTBEAT_MS) return;
  lastHeartbeat = millis();
  sendAck(currentAnimationId);
}

// Core 0: everything that can block on the network.
void networkTask(void*) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();  // playback of the cached animation continues meanwhile
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (playbackFailed) {  // cache file became unreadable: forget it so the next poll reloads it
      playbackFailed = false;
      currentAnimationId = "";
    }
    slideshowTick();
    pollCommand();
    heartbeatTick();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// Core 1: frame pacing and drawing only. Non-blocking: returns immediately unless a frame is due.
void playbackTick() {
  if (xSemaphoreTake(stateMutex, 0) != pdTRUE) return;  // network task is swapping files; retry next pass

  if (!cachedFrames) {
    xSemaphoreGive(stateMutex);
    if (clearRequested) {
      clearRequested = false;
      display.clearDisplay();
      display.display();
    }
    return;
  }

  uint32_t now = millis();
  if (now - lastFrameAt < frameIntervalMs) {
    xSemaphoreGive(stateMutex);
    return;
  }
  // Drift-free pacing: advance by exactly one interval; resync only if we fell more than a frame behind.
  lastFrameAt += frameIntervalMs;
  if (now - lastFrameAt >= frameIntervalMs) lastFrameAt = now;

  bool ok = cacheFile.seek((size_t)frameIndex * FRAME_BYTES) && cacheFile.read(frameBuf, FRAME_BYTES) == FRAME_BYTES;
  if (!ok) {
    Serial.println("Frame read failed; stopping playback");
    cacheFile.close();
    cachedFrames = 0;
    frameIndex = 0;
    clearRequested = true;
    playbackFailed = true;
    xSemaphoreGive(stateMutex);
    return;
  }

  frameIndex++;
  if (frameIndex >= cachedFrames) {
    frameIndex = 0;  // animations loop forever; a slideshow moves on after one complete loop
    if (slideshowMode) slideshowAdvance = true;
  }
  xSemaphoreGive(stateMutex);

  // Slow part (about 25 ms of I2C at 400 kHz) runs outside the mutex.
  // The normalized format matches Adafruit_GFX drawBitmap() row-major 1bpp data.
  display.clearDisplay();
  display.drawBitmap(0, 0, frameBuf, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.display();

  if (PLAYBACK_STATS) {
    static uint32_t statsStart = 0, statsFrames = 0;
    statsFrames++;
    if (millis() - statsStart >= 5000) {
      if (statsStart) Serial.printf("Playback: %.1f FPS (target %.1f)\n", statsFrames * 1000.0f / (millis() - statsStart), 1000.0f / frameIntervalMs);
      statsStart = millis();
      statsFrames = 0;
    }
  }
}

void setup() {
  Serial.begin(115200);
  stateMutex = xSemaphoreCreateMutex();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);
  // periphBegin=false: keep the pins and clock configured above instead of re-running Wire.begin().
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR, true, false)) {
    Serial.println("SSD1306 allocation failed");
    while (true) delay(1000);
  }
  display.clearDisplay();
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print('.');
  }
  Serial.printf("\nConnected: %s\n", WiFi.localIP().toString().c_str());

  LittleFS.begin(true);
  LittleFS.remove(NEXT_FILE);  // leftover from an interrupted download

  // TLS + JSON need a big stack; pin to core 0 (with the Wi-Fi stack) so core 1 stays free for drawing.
  xTaskCreatePinnedToCore(networkTask, "network", 20480, nullptr, 1, nullptr, 0);
}

void loop() {
  playbackTick();
  delay(1);  // yields to the idle task / watchdog; far shorter than any frame interval
}
