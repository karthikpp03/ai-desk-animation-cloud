#pragma once
#include <Arduino.h>
#include <Adafruit_SSD1306.h>

// =====================================================================
// CloudAnimationPlayer — "Animation Display Mode".
//
// This is the existing animation-cloud ESP32 client (command polling,
// LittleFS download, 128x64 1bpp playback, slideshow) wrapped so it can
// live inside AIDeskCompanion. The Worker/GitHub/website side is unchanged.
//
// Threading (same model as the standalone client):
//   update()      (loop(), core 1): ONLY draws. Never touches the network.
//   network task  (core 0): polling, heartbeat, downloads, slideshow.
// The network task is created on first start() and sits idle whenever the
// mode is off, so normal AIDeskCompanion operation never pays for it.
// The OLED is only ever touched from update() and only while active.
// =====================================================================
class CloudAnimationPlayer {
public:
  void begin(Adafruit_SSD1306 &d);  // cheap: stores the display, makes the mutex
  void start();                     // enter Animation Display Mode
  void stop();                      // leave it; releases the OLED and closes the file
  void update();                    // call every loop(); no-op unless active

  bool isActive() const;
};
