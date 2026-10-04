#include "Weather.h"
#include "Config.h"
#include <math.h>

#if !DEMO_MODE
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>   // install "ArduinoJson" by Benoit Blanchon via Library Manager
#endif

WeatherManager::WeatherManager(WiFiManager &wifi)
  : wifiMgr(wifi), temperatureC(28.0f), condition(WeatherCondition::CLEAR),
    locationName(WEATHER_LOCATION), errorState(false), haveEverFetched(false),
    lastFetchAttempt(0), demoWobbleStart(0) {}

void WeatherManager::begin() {
  demoWobbleStart = millis();
#if DEMO_MODE
  fetchDemo();
#endif
}

void WeatherManager::fetchDemo() {
  // A gentle sine wobble so the "Temperature" screen isn't static during
  // a demo, without needing any network access.
  float t = (millis() - demoWobbleStart) / 60000.0f;
  temperatureC = 30.0f + sinf(t) * 3.0f;
  condition = WeatherCondition::CLEAR;
  locationName = WEATHER_LOCATION;
  errorState = false;
  haveEverFetched = true;
}

void WeatherManager::fetchReal() {
#if !DEMO_MODE
  if (!wifiMgr.isConnected()) {
    errorState = true;
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // demo-grade TLS; pin a cert for production use

  HTTPClient http;
  String url = String("https://api.openweathermap.org/data/2.5/weather?q=") +
               WEATHER_LOCATION + "&units=metric&appid=" + WEATHER_API_KEY;

  if (!http.begin(client, url)) {
    errorState = true;
    return;
  }

  int code = http.GET();
  if (code != 200) {
    http.end();
    errorState = true;
    return;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    errorState = true;
    return;
  }

  temperatureC = doc["main"]["temp"] | temperatureC;
  const char *main = doc["weather"][0]["main"] | "Clear";
  String mainStr = String(main);
  if (mainStr == "Clear") condition = WeatherCondition::CLEAR;
  else if (mainStr == "Clouds") condition = WeatherCondition::CLOUDS;
  else if (mainStr == "Rain" || mainStr == "Drizzle") condition = WeatherCondition::RAIN;
  else if (mainStr == "Thunderstorm") condition = WeatherCondition::STORM;
  else if (mainStr == "Snow") condition = WeatherCondition::SNOW;
  else if (mainStr == "Mist" || mainStr == "Fog" || mainStr == "Haze") condition = WeatherCondition::MIST;
  else condition = WeatherCondition::UNKNOWN;

  errorState = false;
  haveEverFetched = true;
#endif
}

void WeatherManager::update() {
#if DEMO_MODE
  fetchDemo(); // cheap enough to just recompute each call
#else
  unsigned long now = millis();
  if (now - lastFetchAttempt >= WEATHER_UPDATE_INTERVAL_MS || lastFetchAttempt == 0) {
    lastFetchAttempt = now;
    fetchReal(); // on failure, errorState is set but the last good
                 // temperatureC/condition/locationName are left untouched,
                 // so the screen shows cached data instead of going blank.
  }
#endif
}

bool WeatherManager::isGoodWeather() const {
  return condition == WeatherCondition::CLEAR || condition == WeatherCondition::CLOUDS;
}
