#include "Reminders.h"
#include "Config.h"

ReminderManager::ReminderManager(ClockManager &clock)
  : clockMgr(clock), lastCheckedMinuteOfDay(-1),
    hasPendingDue(false), demoTriggerAt(0), demoFired(false) {
  for (int i = 0; i < MAX_REMINDERS; i++) reminders[i].used = false;
}

void ReminderManager::begin() {
  // Example fixed reminders — edit freely, or call addReminder() elsewhere.
  addReminder(17, 30, "Meeting 5:30");
  addReminder(20, 0, "Stretch break");

  demoTriggerAt = millis() + 30000UL; // demo: fire ~30s after boot
}

void ReminderManager::addReminder(int hour, int minute, const char *text) {
  for (int i = 0; i < MAX_REMINDERS; i++) {
    if (!reminders[i].used) {
      reminders[i].used = true;
      reminders[i].hour = hour;
      reminders[i].minute = minute;
      reminders[i].firedToday = false;
      strncpy(reminders[i].text, text, sizeof(reminders[i].text) - 1);
      reminders[i].text[sizeof(reminders[i].text) - 1] = '\0';
      return;
    }
  }
}

void ReminderManager::update() {
#if DEMO_MODE
  if (!demoFired && millis() >= demoTriggerAt) {
    demoFired = true;
    pendingDueText = "Stand up & stretch";
    hasPendingDue = true;
  }
  return;
#else
  int currentMinuteOfDay = clockMgr.getHour24() * 60 + (clockMgr.getTimeString().substring(3).toInt());
  if (currentMinuteOfDay == lastCheckedMinuteOfDay) return; // only check once per minute
  lastCheckedMinuteOfDay = currentMinuteOfDay;

  int nowHour = clockMgr.getHour24();
  int nowMinute = currentMinuteOfDay % 60;

  // Reset "firedToday" flags at midnight.
  if (nowHour == 0 && nowMinute == 0) {
    for (int i = 0; i < MAX_REMINDERS; i++) reminders[i].firedToday = false;
  }

  for (int i = 0; i < MAX_REMINDERS; i++) {
    if (!reminders[i].used || reminders[i].firedToday) continue;
    if (reminders[i].hour == nowHour && reminders[i].minute == nowMinute) {
      reminders[i].firedToday = true;
      pendingDueText = String(reminders[i].text);
      hasPendingDue = true;
      break; // surface one at a time
    }
  }
#endif
}

bool ReminderManager::consumeDueReminder(String &outText) {
  if (!hasPendingDue) return false;
  outText = pendingDueText;
  hasPendingDue = false;
  return true;
}

String ReminderManager::getNextReminderPreview() const {
#if DEMO_MODE
  return demoFired ? String("No more reminders") : String("Stand up & stretch");
#else
  for (int i = 0; i < MAX_REMINDERS; i++) {
    if (reminders[i].used && !reminders[i].firedToday) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%02d:%02d %s", reminders[i].hour, reminders[i].minute, reminders[i].text);
      return String(buf);
    }
  }
  return String("No reminders");
#endif
}
