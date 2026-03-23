#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include "ConfigSettings.h"
#include "Network.h"
#include "Web.h"
#include "Sockets.h"
#include "Utils.h"
#include "Somfy.h"
#include "MQTT.h"
#include "GitOTA.h"
#include "esp_core_dump.h"

ConfigSettings settings;
Web webServer;
SocketEmitter sockEmit;
Network net;
rebootDelay_t rebootDelay;
SomfyShadeController somfy;
MQTTClass mqtt;
GitUpdater git;

uint32_t oldheap = 0;

void listDir(const char *dirname, uint8_t levels) {
    Serial.printf("Listing: %s\n", dirname);
    File root = LittleFS.open(dirname);
    if (!root || !root.isDirectory()) {
        Serial.println("Failed to open directory");
        return;
    }
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.printf("  DIR : %s\n", file.name());
            if (levels) listDir(file.path(), levels - 1);
        } else {
            Serial.printf("  FILE: %-30s  %d bytes\n", file.name(), file.size());
        }
        file = root.openNextFile();
    }
}

void setup() {  
  Serial.begin(115200);
  Serial.println();
  Serial.println("Startup/Boot....");
  esp_core_dump_summary_t summary;
  if (esp_core_dump_get_summary(&summary) == ESP_OK) {
    Serial.println("*** Previous crash coredump found ***");
    Serial.printf("  Task: %s\n", summary.exc_task);
    Serial.printf("  PC: 0x%08x\n", summary.exc_pc);
    Serial.printf("  Cause: %d\n", summary.ex_info.exc_cause);
    Serial.printf("  Backtrace:");
    for (int i = 0; i < summary.exc_bt_info.depth; i++) {
      Serial.printf(" 0x%08x", summary.exc_bt_info.bt[i]);
    }
    Serial.println();
  }
  Serial.println("Mounting File System...");


  if (LittleFS.begin()) {
      Serial.printf("\nTotal: %d bytes\n", LittleFS.totalBytes());
      Serial.printf("Used:  %d bytes\n", LittleFS.usedBytes());
      Serial.printf("Free:  %d bytes\n", LittleFS.totalBytes() - LittleFS.usedBytes());
      Serial.println();
      listDir("/", 3);
  } else {
      Serial.println("LittleFS mount failed!");
  }

  if(LittleFS.begin()) Serial.println("File system mounted successfully");
  else Serial.println("Error mounting file system");
  settings.begin();
  if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
  delay(10);
  Serial.println();
  webServer.startup();
  webServer.begin();
  delay(1000);
  net.setup();  
  somfy.begin();
  //git.checkForUpdate();
  esp_task_wdt_init(15, true); //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL); //add current thread to WDT watch

}

void loop() {
  // put your main code here, to run repeatedly:
  //uint32_t heap = ESP.getFreeHeap();
  if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
    Serial.print("Rebooting after ");
    Serial.print(rebootDelay.rebootTime);
    Serial.println("ms");
    net.end();
    ESP.restart();
    return;
  }
  uint32_t timing = millis();
  
  net.loop();
  if(millis() - timing > 100) Serial.printf("Timing Net: %ldms\n", millis() - timing);
  timing = millis();
  esp_task_wdt_reset();
  somfy.loop();
  if(millis() - timing > 100) Serial.printf("Timing Somfy: %ldms\n", millis() - timing);
  timing = millis();
  esp_task_wdt_reset();
  if(net.connected() || net.softAPOpened) {
    if(!rebootDelay.reboot && net.connected() && !net.softAPOpened) {
      git.loop();
      esp_task_wdt_reset();
    }
    webServer.loop();
    esp_task_wdt_reset();
    if(millis() - timing > 100) Serial.printf("Timing WebServer: %ldms\n", millis() - timing);
    esp_task_wdt_reset();
    timing = millis();
    sockEmit.loop();
    if(millis() - timing > 100) Serial.printf("Timing Socket: %ldms\n", millis() - timing);
    esp_task_wdt_reset();
    timing = millis();
  }
  if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
    net.end();
    ESP.restart();
  }
  esp_task_wdt_reset();
}
