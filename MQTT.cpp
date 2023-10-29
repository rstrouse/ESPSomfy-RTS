#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "MQTT.h"
#include "ConfigSettings.h"
#include "Somfy.h"
#include "Network.h"

WiFiClient tcpClient;
PubSubClient mqttClient(tcpClient);

#define MQTT_MAX_RESPONSE 2048
static char g_content[MQTT_MAX_RESPONSE];

extern ConfigSettings settings;
extern SomfyShadeController somfy;
extern Network net;


bool MQTTClass::begin() {
  this->suspended = false;
  return true;
}
bool MQTTClass::end() {
  this->suspended = true;
  this->disconnect();
  return true;
}
void MQTTClass::reset() {
  this->disconnect();
  this->lastConnect = 0;
  this->connect();
}
bool MQTTClass::loop() {
  if(settings.MQTT.enabled && !this->suspended && !mqttClient.connected())
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
          shade->moveToTarget(shade->transformPosition(atoi(value)));
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
      else if(strncmp(command, "position", sizeof(command)) == 0) {
        if(val >= 0 && val <= 100) {
          shade->target = shade->currentPos = shade->transformPosition((float)val);
          shade->emitState();
        }
      }
      else if(strncmp(command, "tiltPosition", sizeof(command)) == 0) {
        if(val >= 0 && val <= 100) {
          shade->tiltTarget = shade->currentTiltPos = (float)val;
          shade->emitState();
        }
      }
      else if(strncmp(command, "sunny", sizeof(command)) == 0) {
        if(val >= 0) shade->sendSensorCommand(-1, val, shade->repeats);
      }
      else if(strncmp(command, "windy", sizeof(command)) == 0) {
        if(val >= 0) shade->sendSensorCommand(val, -1, shade->repeats);
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
      else if(strncmp(command, "sunny", sizeof(command)) == 0) {
        if(val >= 0) group->sendSensorCommand(-1, val, group->repeats);
      }
      else if(strncmp(command, "windy", sizeof(command)) == 0) {
        if(val >= 0) group->sendSensorCommand(val, -1, group->repeats);
      }
    }
  }
}
bool MQTTClass::connect() {
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
      if(mqttClient.connect(this->clientId, settings.MQTT.username, settings.MQTT.password, lwtTopic, 0, true, "offline")) {
        Serial.print("Successfully connected MQTT client ");
        Serial.println(this->clientId);
        this->publish("status", "online", true);
        this->publish("ipAddress", settings.IP.ip.toString().c_str(), true);
        this->publish("host", settings.hostname, true);
        this->publish("firmware", settings.fwVersion.name, true);
        this->publish("serverId", settings.serverId, true);
        this->publish("mac", net.mac.c_str());
        somfy.publish();
        this->subscribe("shades/+/target/set");
        this->subscribe("shades/+/tiltTarget/set");
        this->subscribe("shades/+/direction/set");
        this->subscribe("shades/+/mypos/set");
        this->subscribe("shades/+/myTiltPos/set");
        this->subscribe("shades/+/sunFlag/set");
        this->subscribe("shades/+/sunny/set");
        this->subscribe("shades/+/windy/set");
        this->subscribe("shades/+/position/set");
        this->subscribe("shades/+/tiltPosition/set");
        this->subscribe("groups/+/direction/set");
        this->subscribe("groups/+/sunFlag/set");
        this->subscribe("groups/+/sunny/set");
        this->subscribe("groups/+/windy/set");

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
    this->unsubscribe("shades/+/tiltTarget/set");
    this->unsubscribe("shades/+/mypos/set");
    this->unsubscribe("shades/+/myTiltPos/set");
    this->unsubscribe("shades/+/sunFlag/set");
    this->unsubscribe("groups/+/direction/set");
    this->unsubscribe("shades/+/sunny/set");
    this->unsubscribe("shades/+/windy/set");
    this->unsubscribe("shades/+/position/set");
    this->unsubscribe("shades/+/tiltPosition/set");
    this->unsubscribe("groups/+/direction/set");
    this->unsubscribe("groups/+/sunFlag/set");
    this->unsubscribe("groups/+/sunny/set");
    this->unsubscribe("groups/+/windy/set");
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
bool MQTTClass::publishBuffer(const char *topic, uint8_t *data, uint16_t len, bool retain) {
  size_t res;
  uint16_t offset = 0;
  uint16_t to_write = len;
  uint16_t buff_len;
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
bool MQTTClass::publishDisco(const char *topic, JsonObject &obj, bool retain) {
  serializeJson(obj, g_content, sizeof(g_content));
  this->publishBuffer(topic, (uint8_t *)g_content, strlen(g_content), retain);
  return true;
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
