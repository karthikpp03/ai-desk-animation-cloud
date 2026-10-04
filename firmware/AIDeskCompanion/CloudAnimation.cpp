#include "CloudAnimation.h"
#include "Config.h"

#if ENABLE_ANIMATION_MODE

#include <WiFi.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// API_BASE and DEVICE_TOKEN come from secrets.h (git-ignored, see
// secrets.h.example). If it is missing the firmware still builds; Animation
// Display Mode then just shows "No API config" instead of failing the build.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef SECRET_API_BASE
#define SECRET_API_BASE ""
#endif
#ifndef SECRET_DEVICE_TOKEN
#define SECRET_DEVICE_TOKEN ""
#endif

namespace {

const char* API_BASE = SECRET_API_BASE;
const char* DEVICE_TOKEN = SECRET_DEVICE_TOKEN;

const size_t FRAME_BYTES = 1024;
const uint16_t MAX_FRAMES = 600;
const uint32_t COMMAND_POLL_MS = 1000;
const uint32_t HEARTBEAT_MS = 15000;
const uint32_t IDLE_HEARTBEAT_MS = 5000;  // normal (non-animation) mode: heartbeat + mode-request check
const uint32_t HTTP_TIMEOUT_MS = 10000;
const char* CACHE_FILE = "/animation.bin";
const char* NEXT_FILE = "/next.bin";
const bool PLAYBACK_STATS = false;  // true = print measured FPS to Serial every 5 s

Adafruit_SSD1306* gDisp = nullptr;

// What the OLED shows while no animation is loaded yet.
enum NetStatus : uint8_t { NS_STARTING, NS_NO_WIFI, NS_NO_CONFIG, NS_IDLE, NS_LOADING, NS_ERROR, NS_PLAYING };
volatile uint8_t netStatus = NS_STARTING;
uint8_t lastDrawnStatus = 255;

// ---- Shared playback state, guarded by stateMutex ------------------------------------------
SemaphoreHandle_t stateMutex = nullptr;
File cacheFile;
uint16_t cachedFrames = 0;
uint16_t frameIndex = 0;
uint32_t frameIntervalMs = 100;
uint32_t lastFrameAt = 0;

// ---- Cross-task flags ----------------------------------------------------------------------
volatile bool modeActive = false;         // loop() -> network task: Animation Display Mode on
volatile bool clearRequested = false;     // network task -> loop(): redraw the OLED
volatile bool slideshowAdvance = false;   // loop() -> network task: one full loop finished
volatile bool slideshowMode = false;      // network task -> loop()
volatile bool playbackFailed = false;     // loop() -> network task: cache unreadable, reload it
volatile int8_t modeRequest = -1;         // network task -> loop(): website Mode Change (-1 none, 1 enter, 0 leave)

// ---- Owned by the network task -------------------------------------------------------------
String currentAnimationId;
String currentCommand = "stop";
uint32_t lastCommandPoll = 0;
uint32_t lastHeartbeat = 0;
uint32_t lastIdleBeat = 0;
double lastModeRequestId = 0;       // Date.now() stamp from the Worker; a NEW id means a new button press
bool modeRequestIdKnown = false;    // first id seen after boot is only remembered, never acted on (it is stale)
TaskHandle_t netTaskHandle = nullptr;

// The animation currently sitting in /animation.bin. Survives leaving the mode, so re-entering
// resumes instantly instead of downloading again.
String loadedId;
uint16_t loadedFrames = 0;
uint32_t loadedIntervalMs = 100;

// Slideshow state is kept in RAM only. The complete animation library is not cached.
JsonDocument slideshowDoc;
JsonArray slideshowAnimations;
size_t slideshowIndex = 0;
bool slideshowLoaded = false;

uint8_t frameBuf[FRAME_BYTES];

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

// Reads the website's one-shot mode request out of a Worker response (heartbeat or command poll).
void handleModeRequest(JsonDocument& doc) {
  double id = doc["modeRequestId"] | 0.0;
  const char* want = doc["requestedMode"] | "";
  if (!modeRequestIdKnown) {
    modeRequestIdKnown = true;
    lastModeRequestId = id;
    return;
  }
  if (id == lastModeRequestId) return;
  lastModeRequestId = id;
  if (!strcmp(want, "animation")) modeRequest = 1;
  else if (!strcmp(want, "normal")) modeRequest = 0;
}

// Liveness ping that works in any mode. Tells the website which mode the ESP32 is in and returns any pending
// mode request. (In animation mode with an animation loaded, sendAck() keeps doing this job as before.)
bool sendHeartbeat(const char* mode) {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(String(API_BASE) + "/api/device/heartbeat");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_TOKEN);
  int code = http.POST(String("{\"mode\":\"") + mode + "\"}");
  bool ok = code >= 200 && code < 300;
  if (ok) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) handleModeRequest(doc);
  } else {
    Serial.printf("Heartbeat failed: %d\n", code);
  }
  http.end();
  return ok;
}

// Stops playback and asks loop() to redraw the OLED. Safe to call from the network task.
void stopPlayback() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (cacheFile) cacheFile.close();
  cachedFrames = 0;
  frameIndex = 0;
  xSemaphoreGive(stateMutex);
  currentAnimationId = "";
  loadedId = "";
  loadedFrames = 0;
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

  // modeActive: give up promptly if the user left Animation Display Mode mid-download.
  while (modeActive && http.connected() && total < expected) {
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
      vTaskDelay(pdMS_TO_TICKS(1));
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
  if (!cachedFrames) netStatus = NS_LOADING;
  if (!getMetadata(animationId, meta)) return false;
  Serial.printf("Loading %s (%u frames, %lu ms/frame = %.2f FPS)\n", meta.name.c_str(), meta.frames,
                (unsigned long)meta.frameDurationMs, 1000.0f / meta.frameDurationMs);

  if (!downloadFrames(animationId, meta)) return false;

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (cacheFile) cacheFile.close();
  LittleFS.remove(CACHE_FILE);
  bool ok = LittleFS.rename(NEXT_FILE, CACHE_FILE);
  // Only open it for playback if the mode is still on (the user may have left during the download;
  // resumeCache() reopens the file next time the mode is entered).
  const bool playNow = modeActive;
  if (ok && playNow) cacheFile = LittleFS.open(CACHE_FILE, FILE_READ);
  if (!ok || (playNow && !cacheFile)) {
    cachedFrames = 0;
    xSemaphoreGive(stateMutex);
    loadedId = "";
    loadedFrames = 0;
    clearRequested = true;
    return false;
  }
  if (playNow) {
    cachedFrames = meta.frames;
    frameIntervalMs = meta.frameDurationMs;
    frameIndex = 0;
    lastFrameAt = millis() - frameIntervalMs;  // first frame is due immediately
  }
  xSemaphoreGive(stateMutex);

  loadedId = animationId;
  loadedFrames = meta.frames;
  loadedIntervalMs = meta.frameDurationMs;
  currentAnimationId = animationId;
  if (playNow) netStatus = NS_PLAYING;
  return true;
}

// On (re)entering the mode: reopen the animation left in flash by the previous session, if any.
void resumeCache() {
  currentAnimationId = "";
  if (!loadedId.length() || !loadedFrames) return;
  if (!LittleFS.begin(true)) return;

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (cacheFile) cacheFile.close();
  cacheFile = LittleFS.open(CACHE_FILE, FILE_READ);
  if (cacheFile && cacheFile.size() >= (size_t)loadedFrames * FRAME_BYTES) {
    cachedFrames = loadedFrames;
    frameIntervalMs = loadedIntervalMs;
    frameIndex = 0;
    lastFrameAt = millis() - frameIntervalMs;
    currentAnimationId = loadedId;
  } else {
    if (cacheFile) cacheFile.close();
    cachedFrames = 0;
    loadedId = "";
    loadedFrames = 0;
  }
  xSemaphoreGive(stateMutex);
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
  if (!apiGet("/api/device/command", body)) {
    if (!cachedFrames) netStatus = NS_ERROR;  // keep playing whatever is cached; just report it
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("Command JSON error: %s\n", err.c_str());
    if (!cachedFrames) netStatus = NS_ERROR;
    return false;
  }

  handleModeRequest(doc);  // website Mode Change while in animation mode (-> leave it)

  String requestedCommand = doc["command"] | "stop";
  String requestedId = doc["animationId"] | "";
  String previousCommand = currentCommand;

  if (requestedCommand == "stop") {
    currentCommand = "stop";
    slideshowMode = false;
    slideshowLoaded = false;
    if (previousCommand != "stop" || currentAnimationId.length() || cachedFrames) stopPlayback();
    netStatus = NS_IDLE;
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
        netStatus = NS_ERROR;
      }
    } else if (cachedFrames) {
      netStatus = NS_PLAYING;
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
        if (!cachedFrames) netStatus = NS_ERROR;
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
  if (millis() - lastHeartbeat < HEARTBEAT_MS) return;
  lastHeartbeat = millis();
  if (currentAnimationId.length()) sendAck(currentAnimationId);
  else sendHeartbeat("animation");  // nothing loaded yet: still tell the website the ESP32 is alive in this mode
}

// Normal AIDeskCompanion mode: one tiny request every few seconds. Never touches the OLED or any playback state.
void idleHeartbeatTick() {
  if (!API_BASE[0] || !DEVICE_TOKEN[0]) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (lastIdleBeat && millis() - lastIdleBeat < IDLE_HEARTBEAT_MS) return;
  lastIdleBeat = millis();  // stamped before the request so a failing server is retried at the normal pace
  sendHeartbeat("normal");
}

// Core 0: everything that can block on the network. Idle while Animation Display Mode is off.
void networkTask(void*) {
  bool wasActive = false;
  for (;;) {
    if (!modeActive) {
      if (wasActive) lastIdleBeat = 0;  // just left animation mode: report "normal" right away
      wasActive = false;
      idleHeartbeatTick();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (!wasActive) {
      // Mode just turned on: start from a clean command state and reopen any cached animation.
      wasActive = true;
      currentCommand = "stop";
      slideshowMode = false;
      slideshowLoaded = false;
      slideshowAdvance = false;
      playbackFailed = false;
      lastCommandPoll = 0;
      lastHeartbeat = millis() - HEARTBEAT_MS;  // report "animation" mode to the website right away
      netStatus = NS_STARTING;
      resumeCache();
    }
    if (!API_BASE[0] || !DEVICE_TOKEN[0]) {
      netStatus = NS_NO_CONFIG;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      // WiFiManager owns (re)connecting; just wait. Cached playback continues meanwhile.
      if (!cachedFrames) netStatus = NS_NO_WIFI;
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

// The network task is created once and then idles (or sends the light normal-mode heartbeat) until needed.
void ensureNetworkTask() {
  if (netTaskHandle) return;
  // TLS + JSON need a big stack; pin to core 0 (with the Wi-Fi stack) so core 1 stays free for drawing.
  xTaskCreatePinnedToCore(networkTask, "animNet", 20480, nullptr, 1, &netTaskHandle, 0);
}

void drawStatusScreen() {
  const uint8_t st = netStatus;
  const char* l1 = "";
  const char* l2 = "";
  switch (st) {
    case NS_NO_WIFI:    l1 = "Connecting WiFi..."; break;
    case NS_NO_CONFIG:  l1 = "No API config";     l2 = "add secrets.h";     break;
    case NS_IDLE:       l1 = "Pick an animation"; l2 = "on the website";    break;
    case NS_LOADING:
    case NS_PLAYING:    l1 = "Loading...";        break;
    case NS_ERROR:      l1 = "Server unreachable"; l2 = "Retrying...";      break;
    default:            l1 = "Starting...";       break;
  }
  gDisp->clearDisplay();
  gDisp->setTextSize(1);
  gDisp->setTextColor(SSD1306_WHITE);
  gDisp->setCursor(22, 2);
  gDisp->print("ANIMATION MODE");
  gDisp->drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);
  gDisp->setCursor(4, 26);
  gDisp->print(l1);
  gDisp->setCursor(4, 38);
  gDisp->print(l2);
  gDisp->setCursor(4, 54);
  gDisp->print("4x press = exit");
  gDisp->display();
  lastDrawnStatus = st;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------------

void CloudAnimationPlayer::begin(Adafruit_SSD1306 &d) {
  gDisp = &d;
  if (!stateMutex) stateMutex = xSemaphoreCreateMutex();
  ensureNetworkTask();  // idle until Wi-Fi is up; only sends the normal-mode heartbeat so the website can see this device
}

bool CloudAnimationPlayer::isActive() const {
  return modeActive;
}

int8_t CloudAnimationPlayer::takeModeRequest() {
  int8_t r = modeRequest;
  if (r >= 0) modeRequest = -1;
  return r;
}

void CloudAnimationPlayer::start() {
  if (modeActive || !gDisp || !stateMutex) return;
  lastDrawnStatus = 255;     // force the status screen to draw on the first update()
  clearRequested = false;
  netStatus = NS_STARTING;
  modeActive = true;

  ensureNetworkTask();
}

void CloudAnimationPlayer::stop() {
  if (!modeActive) return;
  modeActive = false;  // the network task goes idle and any in-flight download aborts

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (cacheFile) cacheFile.close();
  cachedFrames = 0;
  frameIndex = 0;
  xSemaphoreGive(stateMutex);
  // The OLED is handed back by the caller (ScreenManager::setSuspended(false) -> next draw()).
}

// Core 1: frame pacing and drawing only. Non-blocking: returns immediately unless a frame is due.
void CloudAnimationPlayer::update() {
  if (!modeActive) return;
  if (xSemaphoreTake(stateMutex, 0) != pdTRUE) return;  // network task is swapping files; retry next pass

  if (!cachedFrames) {
    xSemaphoreGive(stateMutex);
    if (clearRequested || lastDrawnStatus != netStatus) {
      clearRequested = false;
      drawStatusScreen();
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
  lastDrawnStatus = 254;  // an animation frame is on screen; any later status screen must redraw

  // Slow part (about 25 ms of I2C at 400 kHz) runs outside the mutex.
  // The normalized format matches Adafruit_GFX drawBitmap() row-major 1bpp data.
  gDisp->clearDisplay();
  gDisp->drawBitmap(0, 0, frameBuf, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  gDisp->display();

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

#else  // !ENABLE_ANIMATION_MODE — feature compiled out, same API as no-ops

void CloudAnimationPlayer::begin(Adafruit_SSD1306 &) {}
void CloudAnimationPlayer::start() {}
void CloudAnimationPlayer::stop() {}
void CloudAnimationPlayer::update() {}
bool CloudAnimationPlayer::isActive() const { return false; }
int8_t CloudAnimationPlayer::takeModeRequest() { return -1; }

#endif
