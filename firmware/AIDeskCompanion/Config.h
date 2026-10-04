#pragma once
// =====================================================================
// Config.h — all hardware pins, mode flags, credentials and timing
// constants live here. Nothing else in the project should hardcode a
// pin number, timing value, or API key.
// =====================================================================

// ---------------------------------------------------------------------
// MODE
// ---------------------------------------------------------------------
// When true: no Wi-Fi is used at all. Clock, weather, quotes, reminders
// and AI messages are all simulated so the character + screen system
// can be fully tested with just the ESP32 + OLED + button + buzzer.
// Flip to false once the generated WiFi/API secrets and optional keys are configured.
#define DEMO_MODE false

// ---------------------------------------------------------------------
// DISPLAY (SSD1306, I2C)
// ---------------------------------------------------------------------
#define OLED_SDA_PIN     21
#define OLED_SCL_PIN     22
#define OLED_I2C_ADDRESS 0x3C
#define SCREEN_WIDTH     128
#define SCREEN_HEIGHT    64
#define OLED_RESET_PIN   -1   // shares the ESP32 reset line

// ---------------------------------------------------------------------
// INPUT — exactly one button
// ---------------------------------------------------------------------
#define BUTTON_PIN 19   // INPUT_PULLUP, other leg to GND

// ---------------------------------------------------------------------
// OUTPUT — one active buzzer
// ---------------------------------------------------------------------
#define BUZZER_PIN 18

// ---------------------------------------------------------------------
// WI-FI
// ---------------------------------------------------------------------
// Credentials are generated into secrets.h from the cloud project's .env.
// Home WiFi is always preferred; the mobile hotspot is used as fallback.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifdef SECRET_HOME_WIFI_SSID
#define HOME_WIFI_SSID     SECRET_HOME_WIFI_SSID
#define HOME_WIFI_PASSWORD SECRET_HOME_WIFI_PASSWORD
#else
#define HOME_WIFI_SSID     ""
#define HOME_WIFI_PASSWORD ""
#endif
#ifdef SECRET_HOTSPOT_WIFI_SSID
#define HOTSPOT_WIFI_SSID     SECRET_HOTSPOT_WIFI_SSID
#define HOTSPOT_WIFI_PASSWORD SECRET_HOTSPOT_WIFI_PASSWORD
#else
#define HOTSPOT_WIFI_SSID     ""
#define HOTSPOT_WIFI_PASSWORD ""
#endif

// Backward-compatible aliases for modules that still reference the original
// single-network names. New code should use HOME_WIFI_* / HOTSPOT_WIFI_*.
#define WIFI_SSID     HOME_WIFI_SSID
#define WIFI_PASSWORD HOME_WIFI_PASSWORD

#define WIFI_CONNECT_TIMEOUT_MS       10000UL
#define WIFI_RECONNECT_INTERVAL_MS   5000UL
#define WIFI_HOME_RETRY_INTERVAL_MS  60000UL

// ---------------------------------------------------------------------
// WEATHER (OpenWeatherMap-style REST API)
// ---------------------------------------------------------------------
// Free key from openweathermap.org. Either set WEATHER_API_KEY / WEATHER_LOCATION in the
// cloud project's .env (picked up via secrets.h) or edit the fallbacks here.
#ifdef SECRET_WEATHER_API_KEY
#define WEATHER_API_KEY  SECRET_WEATHER_API_KEY
#else
#define WEATHER_API_KEY  "your_openweathermap_api_key"
#endif
#ifdef SECRET_WEATHER_LOCATION
#define WEATHER_LOCATION SECRET_WEATHER_LOCATION
#else
#define WEATHER_LOCATION "Chennai,IN"
#endif
#define WEATHER_UPDATE_INTERVAL_MS 600000UL   // refresh every 10 minutes

// ---------------------------------------------------------------------
// AI (provider-independent — swap the endpoint/body in AI.cpp)
// ---------------------------------------------------------------------
#define AI_API_KEY            "your_ai_api_key"
#define AI_API_ENDPOINT       "https://api.anthropic.com/v1/messages"
#define AI_MESSAGE_INTERVAL_MS 3600000UL      // refresh the daily message hourly

// ---------------------------------------------------------------------
// NTP / CLOCK
// ---------------------------------------------------------------------
#define NTP_SERVER            "pool.ntp.org"
#define GMT_OFFSET_SEC        19800   // IST = UTC+5:30 — change for your timezone
#define DAYLIGHT_OFFSET_SEC   0

// ---------------------------------------------------------------------
// BUTTON TIMING
// ---------------------------------------------------------------------
#define BUTTON_DEBOUNCE_MS   35
#define LONG_PRESS_MS        600
#define BUTTON_REACTION_MS   700   // how long the button reaction (notice->curious/happy) plays before the next screen

// Max gap between a release and the next press for it to count as the
// second half of a DOUBLE_PRESS (mode switch) instead of a plain
// SHORT_PRESS (next screen). Short press delivery is delayed by up to
// this long so ButtonManager can tell the two apart — see Buttons.cpp.
#define DOUBLE_PRESS_WINDOW_MS 320UL

// ---------------------------------------------------------------------
// V2 — TOP LEDs (character "eyes/emotion" extension)
// ---------------------------------------------------------------------
// Each LED sits behind its own 220-ohm resistor. Pins are independent
// of the OLED/button/buzzer pins above and must stay that way.
#define LED_LEFT_PIN    25
#define LED_CENTER_PIN  26
#define LED_RIGHT_PIN   27

// Master switch for the LED-character feature. false = double-press mode
// switching is disabled entirely and the device behaves like V1 (OLED
// only); the OLED animation never depends on this being true (see
// LEDManager — LEDs are always an optional extra layer, never a
// dependency of Character/Animation).
#define ENABLE_LED_MODE true

// Software-PWM refresh period for the LEDs (no ledc/analogWrite dependency
// needed — plain digitalWrite time-sliced this fast reads as smooth
// brightness/fades to the eye). 20ms = 50Hz.
#define LED_PWM_PERIOD_MS 20UL

// ---------------------------------------------------------------------
// V3 — DUAL SERVO EARS (character "ears" extension, EarManager)
// ---------------------------------------------------------------------
// Two SG90 micro servos, one per ear. Pins are independent of every V2/V1
// pin above and must stay that way. Any PWM-capable GPIO works with the
// ESP32Servo library (LEDC under the hood) — these two are just unused.
#define EAR_LEFT_SERVO_PIN  32
#define EAR_RIGHT_SERVO_PIN 33

// Master switch for the ear feature, mirroring ENABLE_LED_MODE. false =
// triple-press ear-mode toggling is disabled entirely and the device
// behaves exactly as if the servos were never added; EarManager is a
// strictly optional extra layer, never a dependency of Character/
// Animation/ScreenManager (same rule LEDManager already follows).
#define ENABLE_EAR_MODE true

// Standard hobby-servo pulse range in microseconds. Adjust if your SG90s
// need a narrower/wider range to reach 0-180 cleanly.
#define EAR_SERVO_MIN_US 500
#define EAR_SERVO_MAX_US 2400

// How often EarManager is allowed to push a new pulse to the servos.
// SG90s don't need updates anywhere near as often as the LED software-PWM
// above; this just paces the eased position updates smoothly. 20ms = 50Hz.
#define EAR_UPDATE_INTERVAL_MS 20UL

// ---- Calibration (degrees, 0-180) -------------------------------------
// Every named pose below is per-ear and independently adjustable, since
// two physically-mirrored SG90 horns rarely land on identical angles for
// the "same" pose. Tweak these to match your build; nothing else in
// EarManager needs to change.
#define EAR_LEFT_UPRIGHT_ANGLE      90   // Normal — ears up
#define EAR_LEFT_TILT_FWD_ANGLE     60   // Curious / Thinking — forward tilt
#define EAR_LEFT_TILT_BACK_ANGLE   130   // Angry — tilted backward
#define EAR_LEFT_PERK_ANGLE        105   // Surprised/Shock — quick perk past upright
#define EAR_LEFT_DROOP_ANGLE        25   // Sad / Sleepy / Sleep / Yawn — drooped
#define EAR_LEFT_LOWERED_ANGLE      55   // Suspicious — slightly lowered
#define EAR_LEFT_DOWN_ANGLE         10   // Ear Mode OFF — neutral/rest position

#define EAR_RIGHT_UPRIGHT_ANGLE     90
#define EAR_RIGHT_TILT_FWD_ANGLE   120
#define EAR_RIGHT_TILT_BACK_ANGLE   50
#define EAR_RIGHT_PERK_ANGLE        75
#define EAR_RIGHT_DROOP_ANGLE      155
#define EAR_RIGHT_LOWERED_ANGLE    125
#define EAR_RIGHT_DOWN_ANGLE       170

// Easing factors: fraction of remaining distance-to-target covered per
// EAR_UPDATE_INTERVAL_MS tick. Higher = snappier (perk/shock/wiggle),
// lower = the slow, deliberate settle the brief asks for on droop/sleepy/
// yawn — same "ease toward target" trick LEDManager uses for fades.

// ---------------------------------------------------------------------
// ANIMATION DISPLAY MODE (animation-cloud integration, CloudAnimation.cpp)
// ---------------------------------------------------------------------
// Master switch. true = a QUADRUPLE press enters/exits Animation Display
// Mode, which plays animations picked on the animation-cloud website.
// API_BASE / DEVICE_TOKEN are NOT stored here: put them in secrets.h
// (see secrets.h.example). false = feature compiled out and the button
// behaves exactly as before (triple press resolves immediately again).
#define ENABLE_ANIMATION_MODE true

// ---------------------------------------------------------------------
// FIRMWARE IDENTITY
// ---------------------------------------------------------------------
#define FIRMWARE_VERSION "V5"

// ---------------------------------------------------------------------
// V5 — MPU6050 motion system (GY-521)
// ---------------------------------------------------------------------
#define MPU6050_ADDRESS 0x68
#define MPU6050_INT_PIN 23
#define MOTION_ENABLED true
#define MOTION_UPDATE_INTERVAL_MS 20UL
#define MOTION_ACCEL_FILTER_ALPHA 0.18f
#define MOTION_GYRO_FILTER_ALPHA 0.16f

// Tilt: deliberately needs a meaningful physical lean and uses hysteresis.
#define MOTION_TILT_START_DEG 15.0f
#define MOTION_TILT_STRONG_DEG 32.0f
#define MOTION_TILT_RELEASE_DEG 9.0f
#define MOTION_TILT_COOLDOWN_MS 700UL
#define MOTION_EXCESSIVE_TILT_COOLDOWN_MS 1400UL

// Tap / impact. A single small impulse is a tap; a larger event becomes shock.
#define MOTION_TAP_DELTA_G 0.78f
#define MOTION_TAP_MIN_ACCEL_G 1.12f
#define MOTION_TAP_MAX_ACCEL_G 1.85f
#define MOTION_TAP_MAX_GYRO_DPS 85.0f
#define MOTION_TAP_COOLDOWN_MS 700UL
#define MOTION_SUDDEN_ACCEL_G 2.10f
#define MOTION_SUDDEN_DYNAMIC_G 1.05f
#define MOTION_EVENT_COOLDOWN_MS 1200UL

// Rotation: strong gyro + acceleration evidence; avoids touch/pickup false positives.
#define MOTION_SUDDEN_GYRO_DPS 230.0f
#define MOTION_DIZZY_GYRO_DPS 330.0f
// Fast physical shake/roll gate: requires ~60 ms of consecutive strong motion.
// High enough to ignore ordinary tilting, but less dependent on a single 330 dps peak.
#define MOTION_DIZZY_DIRECT_GYRO_DPS 280.0f
#define MOTION_DIZZY_DIRECT_MIN_GYRO_DPS 210.0f
#define MOTION_DIZZY_DIRECT_ACCEL_G 0.45f
#define MOTION_DIZZY_DIRECT_SAMPLES 3
#define MOTION_ROTATION_ACCEL_G 0.55f
#define MOTION_ROTATION_DELTA_G 0.55f
#define MOTION_ROTATION_COOLDOWN_MS 1500UL

// Shake: multiple distinct peaks are required before a shake reaction fires.
#define MOTION_SHAKE_PEAK_ACCEL_G 0.62f
#define MOTION_SHAKE_PEAK_GYRO_DPS 125.0f
#define MOTION_SHAKE_PEAK_DELTA_G 0.35f
#define MOTION_SHAKE_REQUIRED_PEAKS 3
#define MOTION_SHAKE_DIZZY_PEAKS 5
#define MOTION_SHAKE_PEAK_WINDOW_MS 900UL
#define MOTION_SHAKE_PEAK_DEBOUNCE_MS 150UL
#define MOTION_SHAKE_BURST_WINDOW_MS 4200UL
#define MOTION_SHAKE_COOLDOWN_MS 900UL
#define MOTION_SHAKE_DIZZY_GYRO_DPS 300.0f

// Vertical Z is used primarily for pickup / landing / impact, not as a tilt.
#define MOTION_PICKUP_ACCEL_G 0.38f
#define MOTION_PICKUP_Z_G 0.34f
#define MOTION_PICKUP_MIN_GYRO_DPS 22.0f
#define MOTION_PICKUP_SETTLE_G 1.16f
#define MOTION_PICKUP_SETTLE_GYRO_DPS 65.0f
#define MOTION_PICKUP_CONFIRM_MS 260UL
#define MOTION_PICKUP_TIMEOUT_MS 1400UL
#define MOTION_PICKUP_COOLDOWN_MS 1600UL
// V5 motion special-animation timing. These use the supplied reference
// bitmap animations through AnimationManager; playback is non-blocking.
#define MOTION_IDIOT_DURATION_MS   5000UL
#define MOTION_STUPID_DURATION_MS  2500UL
#define MOTION_DIZZY_DURATION_MS   3000UL

#define MOTION_LANDING_ACCEL_G 1.65f
#define MOTION_LANDING_DYNAMIC_Z_G 0.55f
#define MOTION_LANDING_COOLDOWN_MS 1100UL

#define MOTION_WAKE_ACCEL_DELTA_G 0.12f
#define MOTION_WAKE_GYRO_DPS 28.0f
#define MOTION_WAKE_COOLDOWN_MS 1200UL

#define MOTION_FRONT_Y_SIGN -1.0f
#define MOTION_LEFT_X_SIGN   1.0f

// ---------------------------------------------------------------------
// V5 — expressive ear calibration
// ---------------------------------------------------------------------
#define EAR_FRONT_BACK_OFFSET  34
#define EAR_SIDE_OFFSET        38
#define EAR_SUBTLE_OFFSET      17
#define EAR_SHAKE_OFFSET       32
#define EAR_DIZZY_OFFSET       40
#define EAR_MICRO_OFFSET        4
#define EAR_MICRO_PERIOD_MS 4200UL
#define EAR_EASE_FACTOR_FAST   0.32f
#define EAR_EASE_FACTOR_NORMAL 0.20f
#define EAR_EASE_FACTOR_SLOW   0.055f

// ---------------------------------------------------------------------
// WI-FI RECONNECT
// ---------------------------------------------------------------------
#define WIFI_RECONNECT_INTERVAL_MS 15000UL
#define WIFI_CONNECT_TIMEOUT_MS    12000UL

// ---------------------------------------------------------------------
// IDLE / SLEEP PERSONALITY
// ---------------------------------------------------------------------
#define SLEEP_TIMEOUT_MS        90000UL   // ~1.5 min of no button press -> sleep
#define IDLE_MIN_CHANGE_MS      1800UL
#define IDLE_MAX_CHANGE_MS      4500UL
// Micro-behaviours (tiny twitch/glance/shake) are rarer and subtler than
// full idle personality beats — they should feel like an occasional extra
// flourish, not a second parallel animation loop.
#define MICRO_MIN_CHANGE_MS     5000UL
#define MICRO_MAX_CHANGE_MS     11000UL

// ---------------------------------------------------------------------
// LOCK INDICATOR
// ---------------------------------------------------------------------
#define LOCK_INDICATOR_MS 700UL

// ---------------------------------------------------------------------
// REMINDERS (DEMO_MODE fake reminder; real mode would fetch from an API
// or a small persisted list — see Reminders.cpp)
// ---------------------------------------------------------------------
#define REMINDER_CHECK_INTERVAL_MS 1000UL
