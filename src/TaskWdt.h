#ifndef TASK_WDT_H
#define TASK_WDT_H
#include <esp_task_wdt.h>

// Task watchdog calls that are safe from any task.
//
// Only loop() is subscribed to the watchdog.  Code it shares with other tasks
// is not: the WiFi event task runs ESPNetwork::setConnected() and async_tcp
// runs the web and socket handlers.  A plain esp_task_wdt_reset() there logs
// "esp_task_wdt_reset(707): task not found", and an esp_task_wdt_add() there
// subscribes a task that nothing resets, so it panics once the timeout runs out.

// Resets the watchdog if the calling task is subscribed to it.
inline void resetTaskWdt() {
  if(esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
}
// Stops watching the calling task around a call that blocks past the timeout.
// True when it was watched, so resumeTaskWdt() knows to add it back.
inline bool pauseTaskWdt() { return esp_task_wdt_delete(NULL) == ESP_OK; }
inline void resumeTaskWdt(bool paused) { if(paused) esp_task_wdt_add(NULL); }
#endif
