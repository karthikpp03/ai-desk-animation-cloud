#pragma once
#include <Arduino.h>
#include "WiFiManager.h"

enum class WeatherCondition { CLEAR, CLOUDS, RAIN, STORM, SNOW, MIST, UNKNOWN };

class WeatherManager {
public:
  WeatherManager(WiFiManager &wifi);

  void begin();
  void update();

  float getTemperatureC() const { return temperatureC; }
  WeatherCondition getCondition() const { return condition; }
  String getLocationName() const { return locationName; }
  bool hasError() const { return errorState; }
  bool hasData() const { return haveEverFetched; }

  // Small helper other modules use to decide the character's mood.
  bool isGoodWeather() const;

private:
  WiFiManager &wifiMgr;
  float temperatureC;
  WeatherCondition condition;
  String locationName;
  bool errorState;
  bool haveEverFetched;
  unsigned long lastFetchAttempt;
  unsigned long demoWobbleStart;

  void fetchReal();
  void fetchDemo();
};
