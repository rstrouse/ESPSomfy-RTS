#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "TaskWdt.h"
#include "esp_log.h"
#include "ConfigSettings.h"
#include "MqttConnection.h"
#include "entryPoints/MqttCommands.h"
#include "Somfy.h"
#include "LoopQueue.h"
#include "ESPNetwork.h"
#include "Utils.h"

static const char *TAG = "MQTT";

WiFiClient tcpClient;
PubSubClient mqttClient(tcpClient);

#define MQTT_MAX_RESPONSE 2048
static char g_content[MQTT_MAX_RESPONSE];

extern ConfigSettings settings;
extern SomfyShadeController somfy;
extern ESPNetwork net;
extern rebootDelay_t rebootDelay;
extern LoopQueue loopQueue;


bool MqttConnection::begin() {
  this->suspended = false;
  return true;
}
bool MqttConnection::end() {
  this->suspended = true;
  this->disconnect();
  return true;
}
void MqttConnection::reset() {
  this->disconnect();
  this->lastConnect = 0;
  this->connect();
}
bool MqttConnection::loop() {
  if(settings.MQTT.enabled && !rebootDelay.reboot && !this->suspended && !mqttClient.connected()) {
    resetTaskWdt();
    if(!this->connected() && net.connected()) this->connect();
  }
  resetTaskWdt();
  if(settings.MQTT.enabled) mqttClient.loop();
  return true;
}
void MqttConnection::receive(const char *topic, byte*payload, uint32_t length) {
  resetTaskWdt(); // Make sure we do not reboot here.
  ESP_LOGD(TAG, "MQTT Topic:%s payload:%.*s", topic, (int)length, (const char *)payload);
  // This already runs on loop(), but going through the loop queue keeps MQTT
  // and web requests in order and holds them while the radio is full.
  loop_item_t item;
  if(!parseMqttCommand(topic, payload, length, item.request)) {
    ESP_LOGW(TAG, "MQTT command not understood: %s", topic);
    return;
  }
  loopQueue.submit(item);
}
bool MqttConnection::connect() {
  resetTaskWdt(); // Make sure we do not reboot here.
  if(mqttClient.connected()) {
    if(!settings.MQTT.enabled || this->suspended)
      return this->disconnect();
    else
      return true;
  }
  if(settings.MQTT.enabled && !this->suspended) {
    if(this->lastConnect + 10000 > millis()) return false;    
    uint64_t mac = ESP.getEfuseMac();
    snprintf(this->clientId, sizeof(this->clientId), "client-%08x%08x", (uint32_t)((mac >> 32) & 0xFFFFFFFF), (uint32_t)(mac & 0xFFFFFFFF));
    if(strlen(settings.MQTT.protocol) > 0 && strlen(settings.MQTT.hostname) > 0) {
      mqttClient.setServer(settings.MQTT.hostname, settings.MQTT.port);
      char lwtTopic[128] = "status";
      if(strlen(settings.MQTT.rootTopic) > 0)
        snprintf(lwtTopic, sizeof(lwtTopic), "%s/status", settings.MQTT.rootTopic);
      resetTaskWdt();
      if(mqttClient.connect(this->clientId, settings.MQTT.username, settings.MQTT.password, lwtTopic, 0, true, "offline")) {
        ESP_LOGI(TAG, "Successfully connected MQTT client %s", this->clientId);
        this->publish("status", "online", true);
        this->publish("ipAddress", settings.IP.ip.toString().c_str(), true);
        this->publish("host", settings.hostname, true);
        this->publish("firmware", settings.fwVersion.name, true);
        this->publish("serverId", settings.serverId, true);
        this->publish("mac", net.mac.c_str());
        somfy.publish();
        for(const char *topic : MQTT_COMMAND_TOPICS) this->subscribe(topic);
        mqttClient.setCallback(MqttConnection::receive);
        ESP_LOGI(TAG, "MQTT Startup Completed");
        resetTaskWdt();
        this->lastConnect = millis();
        return true;
      }
      else {
        ESP_LOGE(TAG, "MQTT Connection failed for: %d", mqttClient.state());
        this->lastConnect = millis();
        return false;
      }
    }
    else
      return true;
  }
  return true;
}
bool MqttConnection::disconnect() {
  if(mqttClient.connected()) {
    for(const char *topic : MQTT_COMMAND_TOPICS) this->unsubscribe(topic);
    mqttClient.disconnect();
  }
  return true;
}
bool MqttConnection::unsubscribe(const char *topic) {
  if(mqttClient.connected()) {
    char top[128];
    if(strlen(settings.MQTT.rootTopic) > 0)
      snprintf(top, sizeof(top), "%s/%s", settings.MQTT.rootTopic, topic);
    else
      strlcpy(top, topic, sizeof(top));
    ESP_LOGD(TAG, "MQTT Unsubscribed from:%s", top);
    return mqttClient.unsubscribe(top);
  }
  return true;
}
bool MqttConnection::subscribe(const char *topic) {
  if(mqttClient.connected()) {
    resetTaskWdt(); // Make sure we do not reboot here.
    char top[128];
    if(strlen(settings.MQTT.rootTopic) > 0)
      snprintf(top, sizeof(top), "%s/%s", settings.MQTT.rootTopic, topic);
    else
      strlcpy(top, topic, sizeof(top));
    ESP_LOGD(TAG, "MQTT Subscribed to:%s", top);
    return mqttClient.subscribe(top);
  }
  return true;
}
bool MqttConnection::publish(const char *topic, const char *payload, bool retain) {
  if(mqttClient.connected()) {
    char top[128];
    if(strlen(settings.MQTT.rootTopic) > 0)
      snprintf(top, sizeof(top), "%s/%s", settings.MQTT.rootTopic, topic);
    else
      strlcpy(top, topic, sizeof(top));
    resetTaskWdt(); // Make sure we do not reboot here.
    mqttClient.publish(top, payload, retain);
    return true;
  }
  return false;
}
bool MqttConnection::publish(const char *topic, uint32_t val, bool retain) {
  snprintf(g_content, sizeof(g_content), "%u", val);
  return this->publish(topic, g_content, retain);
}
bool MqttConnection::unpublish(const char *topic) {
  if(mqttClient.connected()) {
    char top[128];
    if(strlen(settings.MQTT.rootTopic) > 0)
      snprintf(top, sizeof(top), "%s/%s", settings.MQTT.rootTopic, topic);
    else
      strlcpy(top, topic, sizeof(top));
    resetTaskWdt(); // Make sure we do not reboot here.
    mqttClient.publish(top, (const uint8_t *)"", 0, true);
    return true;
  }
  return false;

//  mqttClient.beginPublish(topic, 0, true);
//  mqttClient.endPublish();
}

bool MqttConnection::publishBuffer(const char *topic, uint8_t *data, uint16_t len, bool retain) {
  size_t res;
  uint16_t offset = 0;
  uint16_t to_write = len;
  uint16_t buff_len;
  resetTaskWdt(); // Make sure we do not reboot here.
  mqttClient.beginPublish(topic, len, retain);
  do { 
    buff_len = to_write;
    if(buff_len > 128) buff_len = 128;
    res = mqttClient.write(data+offset, buff_len);
    offset += buff_len;
    to_write -= buff_len;
  } while(res == buff_len && to_write > 0);
  mqttClient.endPublish();
  return true;
}
bool MqttConnection::publishDisco(const char *topic, JsonObject &obj, bool retain) {
  serializeJson(obj, g_content, sizeof(g_content));
  this->publishBuffer(topic, (uint8_t *)g_content, strlen(g_content), retain);
  return true;
}
bool MqttConnection::publish(const char *topic, int8_t val, bool retain) {
  snprintf(g_content, sizeof(g_content), "%d", val);
  return this->publish(topic, g_content, retain);
}
bool MqttConnection::publish(const char *topic, uint8_t val, bool retain) {
  snprintf(g_content, sizeof(g_content), "%u", val);
  return this->publish(topic, g_content, retain);
}
bool MqttConnection::publish(const char *topic, uint16_t val, bool retain) {
  snprintf(g_content, sizeof(g_content), "%u", val);
  return this->publish(topic, g_content, retain);
}
bool MqttConnection::publish(const char *topic, bool val, bool retain) {
  snprintf(g_content, sizeof(g_content), "%s", val ? "true" : "false");
  return this->publish(topic, g_content, retain);
}
bool MqttConnection::connected() {
  if(settings.MQTT.enabled) return mqttClient.connected();
  return false;
}
