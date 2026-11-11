#pragma once
// Stands in for ESP-IDF's task watchdog for one calling task.  Tests set
// whether it is subscribed and count the resets.
typedef int esp_err_t;
#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND 0x105
#endif

struct FakeTaskWdt {
  bool subscribed = false;
  int resets = 0;
  int adds = 0;
};
inline FakeTaskWdt fakeTaskWdt;

inline esp_err_t esp_task_wdt_status(void *) { return fakeTaskWdt.subscribed ? ESP_OK : ESP_ERR_NOT_FOUND; }
inline esp_err_t esp_task_wdt_reset() {
  if(!fakeTaskWdt.subscribed) return ESP_ERR_NOT_FOUND;
  fakeTaskWdt.resets++;
  return ESP_OK;
}
inline esp_err_t esp_task_wdt_delete(void *) {
  if(!fakeTaskWdt.subscribed) return ESP_ERR_NOT_FOUND;
  fakeTaskWdt.subscribed = false;
  return ESP_OK;
}
inline esp_err_t esp_task_wdt_add(void *) {
  fakeTaskWdt.adds++;
  fakeTaskWdt.subscribed = true;
  return ESP_OK;
}
