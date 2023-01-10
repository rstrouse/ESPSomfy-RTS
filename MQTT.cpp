#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "MQTT.h"
#include "ConfigSettings.h"
#include "Somfy.h"

WiFiClient tcpClient;
PubSubClient mqttClient(tcpClient);

#define MQTT_MAX_RESPONSE 2048
static char g_content[MQTT_MAX_RESPONSE];

extern ConfigSettings settings;
extern SomfyShadeController somfy;
bool MQTTClass::begin() {
  return true;
}
bool MQTTClass::end() {
  this->disconnect();
  return true;
}
bool MQTTClass::loop() {
  if(settings.MQTT.enabled && !mqttClient.connected())
    this->connect();
  mqttClient.loop();
  return true;
}
bool MQTTClass::connect() {
  if(mqttClient.connected()) {
    if(!settings.MQTT.enabled)
      return this->disconnect();
    else
      return true;
  }
  if(settings.MQTT.enabled) {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(this->clientId, sizeof(this->clientId), "client-%08lx%08lx", (uint32_t)((mac >> 32) & 0xFFFFFFFF), (uint32_t)(mac & 0xFFFFFFFF));
    if(strlen(settings.MQTT.protocol) > 0 && strlen(settings.MQTT.hostname) > 0) {
      mqttClient.setServer(settings.MQTT.hostname, settings.MQTT.port);
      if(mqttClient.connect(this->clientId, settings.MQTT.username, settings.MQTT.password)) {
        Serial.print("Successfully connected MQTT client ");
        Serial.println(this->clientId);
        somfy.publish();
        return true;
      }
      else {
        Serial.print("MQTT Connection failed for ");
        Serial.println(mqttClient.state());
      }
    }
    else
      return true;
  }
  return true;
}
bool MQTTClass::disconnect() {
  if(mqttClient.connected()) {
    mqttClient.disconnect();
  }
  return true;
}
bool MQTTClass::publish(const char *topic, const char *payload) {
  if(mqttClient.connected()) {
    char top[64];
    if(strlen(settings.MQTT.rootTopic) > 0)
      snprintf(top, sizeof(top), "%s/%s", settings.MQTT.rootTopic, topic);
    else
      strlcpy(top, topic, sizeof(top));
    mqttClient.publish(top, payload);
    return true;
  }
  return false;
}
bool MQTTClass::publish(const char *topic, uint32_t val) {
  snprintf(g_content, sizeof(g_content), "%u", val);
  return this->publish(topic, g_content);
}
bool MQTTClass::publish(const char *topic, JsonDocument &doc) {
  serializeJson(doc, g_content, sizeof(g_content));
  return this->publish(topic, g_content);
}
bool MQTTClass::publish(const char *topic, JsonArray &arr) {
  serializeJson(arr, g_content, sizeof(g_content));
  return this->publish(topic, g_content);
}
bool MQTTClass::publish(const char *topic, JsonObject &obj) {
  serializeJson(obj, g_content, sizeof(g_content));
  return this->publish(topic, g_content);
}
bool MQTTClass::publish(const char *topic, int8_t val) {
  snprintf(g_content, sizeof(g_content), "%d", val);
  return this->publish(topic, g_content);
}
bool MQTTClass::publish(const char *topic, uint8_t val) {
  snprintf(g_content, sizeof(g_content), "%u", val);
  return this->publish(topic, g_content);
}
bool MQTTClass::publish(const char *topic, uint16_t val) {
  snprintf(g_content, sizeof(g_content), "%u", val);
  return this->publish(topic, g_content);
}
bool MQTTClass::connected() {
  if(settings.MQTT.enabled) return mqttClient.connected();
  return false;
}
