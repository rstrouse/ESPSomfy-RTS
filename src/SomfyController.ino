#include "esp_log.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include "ConfigSettings.h"
#include "ESPNetwork.h"
#include "Web.h"
#include "Sockets.h"
#include "Utils.h"
#include "Somfy.h"
#include "MQTT.h"
#include "GitOTA.h"
#include "esp_core_dump.h"

static const char *TAG = "Main";

ConfigSettings settings;
Web webServer;
SocketEmitter sockEmit;
ESPNetwork net;
rebootDelay_t rebootDelay;
SomfyShadeController somfy;
MQTTClass mqtt;
GitUpdater git;

uint32_t oldheap = 0;

void listDir(const char *dirname, uint8_t levels) {
    ESP_LOGI(TAG, "Listing: %s", dirname);
    File root = LittleFS.open(dirname);
    if (!root || !root.isDirectory()) {
        ESP_LOGE(TAG, "Failed to open directory");
        return;
    }
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            ESP_LOGI(TAG, "  DIR : %s", file.name());
            if (levels) listDir(file.path(), levels - 1);
        } else {
            ESP_LOGI(TAG, "  FILE: %-30s  %d bytes", file.name(), file.size());
        }
        file = root.openNextFile();
    }
}

void setup() {  
  Serial.begin(115200);
  ESP_LOGI(TAG, "Startup/Boot....");
  esp_core_dump_summary_t summary;
  if (esp_core_dump_get_summary(&summary) == ESP_OK) {
    ESP_LOGW(TAG, "*** Previous crash coredump found ***");
    ESP_LOGW(TAG, "  Task: %s", summary.exc_task);
    ESP_LOGW(TAG, "  PC: 0x%08x", summary.exc_pc);
#ifdef CONFIG_IDF_TARGET_ARCH_XTENSA
    ESP_LOGW(TAG, "  Cause: %d", summary.ex_info.exc_cause);
    char bt_buf[256] = {0};
    int pos = 0;
    for (int i = 0; i < summary.exc_bt_info.depth; i++) {
      pos += snprintf(bt_buf + pos, sizeof(bt_buf) - pos, " 0x%08x", summary.exc_bt_info.bt[i]);
    }
    ESP_LOGW(TAG, "  Backtrace:%s", bt_buf);
#elif CONFIG_IDF_TARGET_ARCH_RISCV
    ESP_LOGW(TAG, "  Cause: %d", summary.ex_info.mcause);
    ESP_LOGW(TAG, "  MTVAL: 0x%08x  RA: 0x%08x  SP: 0x%08x",
             summary.ex_info.mtval, summary.ex_info.ra, summary.ex_info.sp);
#endif
  }
  ESP_LOGI(TAG, "Mounting File System...");


  if (LittleFS.begin()) {
      ESP_LOGI(TAG, "Total: %d bytes", LittleFS.totalBytes());
      ESP_LOGI(TAG, "Used:  %d bytes", LittleFS.usedBytes());
      ESP_LOGI(TAG, "Free:  %d bytes", LittleFS.totalBytes() - LittleFS.usedBytes());
      listDir("/", 3);
  } else {
      ESP_LOGE(TAG, "LittleFS mount failed!");
  }

  if(LittleFS.begin()) ESP_LOGI(TAG, "File system mounted successfully");
  else ESP_LOGE(TAG, "Error mounting file system");
  settings.begin();
  if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
  delay(10);
  webServer.startup();
  webServer.begin();
  delay(1000);
  net.setup();  
  somfy.begin();
  //git.checkForUpdate();
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  const esp_task_wdt_config_t wdt_config = { .timeout_ms = 15000, .idle_core_mask = 1, .trigger_panic = true };
  esp_task_wdt_init(&wdt_config);
#else
  esp_task_wdt_init(15, true); //enable panic so ESP32 restarts
#endif
  esp_task_wdt_add(NULL); //add current thread to WDT watch

}

void loop() {
  // put your main code here, to run repeatedly:
  //uint32_t heap = ESP.getFreeHeap();
  if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
    ESP_LOGI(TAG, "Rebooting after %lums", rebootDelay.rebootTime);
    net.end();
    ESP.restart();
    return;
  }
  uint32_t timing = millis();
  
  net.loop();
  if(millis() - timing > 100) ESP_LOGD(TAG, "Timing Net: %ldms", millis() - timing);
  timing = millis();
  esp_task_wdt_reset();
  somfy.loop();
  if(millis() - timing > 100) ESP_LOGD(TAG, "Timing Somfy: %ldms", millis() - timing);
  timing = millis();
  esp_task_wdt_reset();
  if(net.connected() || net.softAPOpened) {
    if(!rebootDelay.reboot && net.connected() && !net.softAPOpened) {
      git.loop();
      esp_task_wdt_reset();
    }
    webServer.loop();
    esp_task_wdt_reset();
    if(millis() - timing > 100) ESP_LOGD(TAG, "Timing WebServer: %ldms", millis() - timing);
    esp_task_wdt_reset();
    timing = millis();
    sockEmit.loop();
    if(millis() - timing > 100) ESP_LOGD(TAG, "Timing Socket: %ldms", millis() - timing);
    esp_task_wdt_reset();
    timing = millis();
  }
  if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
    net.end();
    ESP.restart();
  }
  esp_task_wdt_reset();
}
