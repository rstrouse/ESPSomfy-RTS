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
  this->lastConnect = 0;
  this->connect();
  return true;
}
void MQTTClass::reset() {
  this->disconnect();
}
bool MQTTClass::loop() {
  if(settings.MQTT.enabled && !mqttClient.connected())
    this->connect();
  if(settings.MQTT.enabled) mqttClient.loop();
  return true;
}
void MQTTClass::receive(const char *topic, byte*payload, uint32_t length) {
  Serial.print("MQTT Topic:");
  Serial.print(topic);
  Serial.print(" payload:");
  for(uint32_t i=0; i<length; i++)
    Serial.print((char)payload[i]);
  Serial.println();

  // We need to start at the last slash in the data
  uint8_t len = strlen(topic);
  
  uint8_t slashes = 0;
  uint16_t ndx = strlen(topic) - 1;
  while(ndx > 0) {
    if(topic[ndx] == '/') slashes++;
    if(slashes == 4) break;
    ndx--;
  }
  char entityId[4];
  char command[32];
  char entityType[7];
  char value[10];
  memset(command, 0x00, sizeof(command));
  memset(entityId, 0x00, sizeof(entityId));
  memset(entityType, 0x00, sizeof(entityType));
  memset(value, 0x00, sizeof(value));
  uint8_t i = 0;
  while(topic[ndx] == '/' && ndx < len) ndx++;
  while(ndx < len) {
    if(topic[ndx] != '/' && i < sizeof(entityType))
      entityType[i++] = topic[ndx];
    ndx++;
    if(topic[ndx] == '/') break;
  }
  i = 0;
  while(topic[ndx] == '/' && ndx < len) ndx++;
  while(ndx < len) {
    if(topic[ndx] != '/' && i < sizeof(entityId))
      entityId[i++] = topic[ndx];
    ndx++;
    if(topic[ndx] == '/') break;
  }
  i = 0;
  while(topic[ndx] == '/' && ndx < len) ndx++;
  while(ndx < len) {
    if(topic[ndx] != '/' && i < sizeof(command))
      command[i++] = topic[ndx];
    ndx++;
    if(topic[ndx] == '/') break;
  }
  for(uint8_t j = 0; j < length && j < sizeof(value); j++)
    value[j] = payload[j];
  
  Serial.print("MQTT type:[");
  Serial.print(entityType);
  Serial.print("] command:[");
  Serial.print(command);
  Serial.print("] entityId:");
  Serial.print(entityId);
  Serial.print(" value:");
  Serial.println(value);
  if(strncmp(entityType, "shades", sizeof(entityType)) == 0) {
    SomfyShade* shade = somfy.getShadeById(atoi(entityId));
    if (shade) {
      int val = atoi(value);
      if(strncmp(command, "target", sizeof(command)) == 0) {
        if(val >= 0 && val <= 100)
          shade->moveToTarget(atoi(value));
      }
      if(strncmp(command, "tiltTarget", sizeof(command)) == 0) {
        if(val >= 0 && val <= 100)
          shade->moveToTiltTarget(atoi(value));
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
      else if(strncmp(command, "myTiltPos", sizeof(command)) == 0) {
        if(val >= 0 && val <= 100)
          shade->setMyPosition(shade->myPos, val);
      }
      else if(strncmp(command, "sunFlag", sizeof(command)) == 0) {
        if(val > 0) shade->sendCommand(somfy_commands::SunFlag);
        else shade->sendCommand(somfy_commands::Flag);
      }
    }
  }
  else if(strncmp(entityType, "groups", sizeof(entityType)) == 0) {
    SomfyGroup* group = somfy.getGroupById(atoi(entityId));
    if (group) {
      int val = atoi(value);
      if(strncmp(command, "direction", sizeof(command)) == 0) {
        if(val < 0)
          group->sendCommand(somfy_commands::Up);
        else if(val > 0)
          group->sendCommand(somfy_commands::Down);
        else
          group->sendCommand(somfy_commands::My);
      }
      else if(strncmp(command, "sunFlag", sizeof(command)) == 0) {
        if(val > 0)
          group->sendCommand(somfy_commands::Flag);
        else
          group->sendCommand(somfy_commands::SunFlag);
      }
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
      string statusTopic = settings.MQTT.rootTopic + "/status";
      mqttClient.setWill(statusTopic, "offline", true, 1);
      if(mqttClient.connect(this->clientId, settings.MQTT.username, settings.MQTT.password)) {
        Serial.print("Successfully connected MQTT client ");
        Serial.println(this->clientId);
        somfy.publish();
        this->subscribe("shades/+/target/set");
        this->subscribe("shades/+/tiltTarget/set");
        this->subscribe("shades/+/direction/set");
        this->subscribe("shades/+/mypos/set");
        this->subscribe("shades/+/myTiltPos/set");
        this->subscribe("shades/+/sunFlag/set");
        this->subscribe("groups/+/direction/set");
        mqttClient.setCallback(MQTTClass::receive);
        this->lastConnect = millis();
        this->publish(statusTopic, "online", true)
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
    this->unsubscribe("shades/+/tiltTarget/set");
    this->unsubscribe("shades/+/mypos/set");
    this->unsubscribe("shades/+/myTiltPos/set");
    this->unsubscribe("shades/+/sunFlag/set");
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
    Serial.print("MQTT Unsubscribed from:");
    Serial.println(top);
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
bool MQTTClass::publish(const char *topic, const char *payload, bool retain) {
  if(mqttClient.connected()) {
    char top[128];
    if(strlen(settings.MQTT.rootTopic) > 0)
      snprintf(top, sizeof(top), "%s/%s", settings.MQTT.rootTopic, topic);
    else
      strlcpy(top, topic, sizeof(top));
    mqttClient.publish(top, payload, retain);
    return true;
  }
  return false;
}
bool MQTTClass::publish(const char *topic, uint32_t val, bool retain) {
  snprintf(g_content, sizeof(g_content), "%u", val);
  return this->publish(topic, g_content, retain);
}
bool MQTTClass::publish(const char *topic, JsonDocument &doc, bool retain) {
  serializeJson(doc, g_content, sizeof(g_content));
  return this->publish(topic, g_content, retain);
}
bool MQTTClass::publish(const char *topic, JsonArray &arr, bool retain) {
  serializeJson(arr, g_content, sizeof(g_content));
  return this->publish(topic, g_content, retain);
}
bool MQTTClass::publish(const char *topic, JsonObject &obj, bool retain) {
  serializeJson(obj, g_content, sizeof(g_content));
  return this->publish(topic, g_content, retain);
}
bool MQTTClass::publish(const char *topic, int8_t val, bool retain) {
  snprintf(g_content, sizeof(g_content), "%d", val);
  return this->publish(topic, g_content, retain);
}
bool MQTTClass::publish(const char *topic, uint8_t val, bool retain) {
  snprintf(g_content, sizeof(g_content), "%u", val);
  return this->publish(topic, g_content, retain);
}
bool MQTTClass::publish(const char *topic, uint16_t val, bool retain) {
  snprintf(g_content, sizeof(g_content), "%u", val);
  return this->publish(topic, g_content, retain);
}
bool MQTTClass::publish(const char *topic, bool val, bool retain) {
  snprintf(g_content, sizeof(g_content), "%s", val ? "true" : "false");
  return this->publish(topic, g_content, retain);
}
bool MQTTClass::connected() {
  if(settings.MQTT.enabled) return mqttClient.connected();
  return false;
}
