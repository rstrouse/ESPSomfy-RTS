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
void MQTTClass::receive(const char *topic, byte*payload, uint32_t length) {
  //Serial.print("MQTT Topic:");
  //Serial.print(topic);
  //Serial.print(" payload:");
  //for(uint32_t i=0; i<length; i++)
  //  Serial.print((char)payload[i]);
  //Serial.println();

  // We need to start at the last slash in the data
  uint16_t ndx = strlen(topic) - 1;
  // ------------------+
  // shades/1/target/set
  while(ndx >= 0 && topic[ndx] != '/') ndx--; // Back off the set command
  uint16_t end_command = --ndx;
  // --------------+----
  // shades/1/target/set
  while(ndx >= 0 && topic[ndx] != '/') ndx--; // Get the start of the leaf.
  // --------+----------
  // shades/1/target/set
  uint16_t start_command = ndx + 1;
  uint16_t id_end = --ndx;
  while(ndx >= 0 && topic[ndx] != '/') ndx--;
  // ------+------------
  // shades/1/target/set
  uint16_t id_start = ndx + 1;
  char shadeId[4];
  char command[32];
  memset(command, 0x00, sizeof(command));
  memset(shadeId, 0x00, sizeof(shadeId));
  for(uint16_t i = 0;id_start <= id_end; i++)
    shadeId[i] = topic[id_start++];
  for(uint16_t i = 0;start_command <= end_command; i++)
    command[i] = topic[start_command++];

  char value[10];
  memset(value, 0x00, sizeof(value));
  for(uint8_t i = 0; i < length; i++)
    value[i] = payload[i];
  
  Serial.print("MQTT Command:[");
  Serial.print(command);
  Serial.print("] shadeId:");
  Serial.print(shadeId);
  Serial.print(" value:");
  Serial.println(value);
  SomfyShade* shade = somfy.getShadeById(atoi(shadeId));
  if (shade) {
    int val = atoi(value);
    if(strncmp(command, "target", sizeof(command)) == 0) {
      if(val >= 0 && val <= 100)
        shade->moveToTarget(atoi(value));
    }
    else if(strncmp(command, "direction", sizeof(command)) == 0) {
      if(val < 0)
        shade->sendCommand(somfy_commands::Up);
      else if(val > 0)
        shade->sendCommand(somfy_commands::Down);
      else
        shade->sendCommand(somfy_commands::My);
    }
    else if(strncmp(command, "mypos", sizeof(command)) == 0) {
      if(val >= 0 && val <= 100)
        shade->setMyPosition(val);
    }
  }
}
bool MQTTClass::connect() {
  if(mqttClient.connected()) {
    if(!settings.MQTT.enabled)
      return this->disconnect();
    else
      return true;
  }
  if(settings.MQTT.enabled) {
    if(this->lastConnect + 10000 > millis()) return false;    
    uint64_t mac = ESP.getEfuseMac();
    snprintf(this->clientId, sizeof(this->clientId), "client-%08x%08x", (uint32_t)((mac >> 32) & 0xFFFFFFFF), (uint32_t)(mac & 0xFFFFFFFF));
    if(strlen(settings.MQTT.protocol) > 0 && strlen(settings.MQTT.hostname) > 0) {
      mqttClient.setServer(settings.MQTT.hostname, settings.MQTT.port);
      if(mqttClient.connect(this->clientId, settings.MQTT.username, settings.MQTT.password)) {
        Serial.print("Successfully connected MQTT client ");
        Serial.println(this->clientId);
        somfy.publish();
        this->subscribe("shades/+/target/set");
        this->subscribe("shades/+/direction/set");
        this->subscribe("shades/+/mypos/set");
        mqttClient.setCallback(MQTTClass::receive);
        this->lastConnect = millis();
        return true;
      }
      else {
        Serial.print("MQTT Connection failed for: ");
        Serial.println(mqttClient.state());
        this->lastConnect = millis();
        return false;
      }
    }
    else
      return true;
  }
  return true;
}
bool MQTTClass::disconnect() {
  if(mqttClient.connected()) {
    this->unsubscribe("shades/+/target/set");
    this->unsubscribe("shades/+/direction/set");
    mqttClient.disconnect();
  }
  return true;
}
bool MQTTClass::unsubscribe(const char *topic) {
  if(mqttClient.connected()) {
    char top[128];
    if(strlen(settings.MQTT.rootTopic) > 0)
      snprintf(top, sizeof(top), "%s/%s", settings.MQTT.rootTopic, topic);
    else
      strlcpy(top, topic, sizeof(top));
    return mqttClient.unsubscribe(top);
  }
  return true;
}
bool MQTTClass::subscribe(const char *topic) {
  if(mqttClient.connected()) {
    char top[128];
    if(strlen(settings.MQTT.rootTopic) > 0)
      snprintf(top, sizeof(top), "%s/%s", settings.MQTT.rootTopic, topic);
    else
      strlcpy(top, topic, sizeof(top));
    Serial.print("MQTT Subscribed to:");
    Serial.println(top);
    return mqttClient.subscribe(top);
  }
  return true;
}
bool MQTTClass::publish(const char *topic, const char *payload) {
  if(mqttClient.connected()) {
    char top[128];
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
