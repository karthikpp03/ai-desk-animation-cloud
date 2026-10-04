#pragma once
#include <Arduino.h>
#include "ClockManager.h"

#define MAX_REMINDERS 8

struct Reminder {
  bool used;
  int hour;
  int minute;
  char text[24];
  bool firedToday;
};

// A tiny, fully local reminder list — no server needed. Add entries in
// Reminders.cpp (REMINDER_TABLE) or call addReminder() at runtime.
// In DEMO_MODE, a reminder is synthesized to fire ~30s after boot so the
// whole "reminder trigger -> buzzer -> excited character" path is easy
// to see without waiting for a real clock time.
class ReminderManager {
public:
  ReminderManager(ClockManager &clock);

  void begin();
  void update();

  void addReminder(int hour, int minute, const char *text);

  // True exactly once, the moment a reminder becomes due — callers
  // (ScreenManager) should react to it right away.
  bool consumeDueReminder(String &outText);

  // For display on the REMINDERS screen even when nothing is currently due.
  String getNextReminderPreview() const;

private:
  ClockManager &clockMgr;
  Reminder reminders[MAX_REMINDERS];
  int lastCheckedMinuteOfDay;
  String pendingDueText;
  bool hasPendingDue;

  unsigned long demoTriggerAt;
  bool demoFired;
};
