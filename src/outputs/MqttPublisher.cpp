#include "Utils.h"
#include "ConfigSettings.h"
#include "Somfy.h"
#include "SomfySnapshot.h"
#include "MqttConnection.h"
#include "MqttDisco.h"
#include "MqttTopics.h"

extern SomfyShadeController somfy;
extern ConfigSettings settings;
extern MqttConnection mqtt;

void SomfyRoom::publish() {
  if(mqtt.connected()) {
    char topic[64];
    sprintf(topic, "rooms/%d/roomId", this->roomId);
    mqtt.publish(topic, this->roomId, true);
    sprintf(topic, "rooms/%d/name", this->roomId);
    mqtt.publish(topic, this->name, true);
    sprintf(topic, "rooms/%d/sortOrder", this->roomId);
    mqtt.publish(topic, this->sortOrder, true);
  }
}
void SomfyRoom::unpublish() {
  if(mqtt.connected()) {
    char topic[64];
    sprintf(topic, "rooms/%d/roomId", this->roomId);
    mqtt.unpublish(topic);
    sprintf(topic, "rooms/%d/name", this->roomId);
    mqtt.unpublish(topic);
    sprintf(topic, "rooms/%d/sortOrder", this->roomId);
    mqtt.unpublish(topic);
  }
}
// Publishes one topic of a shade: shades/<id>/<topic>.
static void publishShadeTopic(const char *topic, int32_t value, bool retain, void *ctx) {
  SomfyShade *shade = static_cast<SomfyShade *>(ctx);
  if(value < 0) shade->publish(topic, static_cast<int8_t>(value), retain);
  else shade->publish(topic, static_cast<uint32_t>(value), retain);
}
void SomfyShade::publishTopics(uint16_t changed) {
  if(!mqtt.connected()) return;
  shade_snapshot_t s;
  snapshotShade(*this, s);
  publishShadeTopics(changed, s, publishShadeTopic, this);
}
void SomfyShade::publishState() {
  if(!mqtt.connected()) return;
  shade_snapshot_t s;
  snapshotShade(*this, s);
  publishShadeTopics(shadeStateTopics(s), s, publishShadeTopic, this);
}
// The shade's config for Home Assistant's MQTT discovery.
void SomfyShade::publishDisco() {
  if(!mqtt.connected() || !settings.MQTT.pubDisco) return;
  shade_disco_t disco;
  disco.shadeId = this->shadeId;
  disco.name = this->name;
  disco.shadeType = this->shadeType;
  disco.tiltType = this->tiltType;
  disco.flipPosition = this->flipPosition;
  disco.rootTopic = settings.MQTT.rootTopic;
  disco.discoTopic = settings.MQTT.discoTopic;
  disco.serverId = settings.serverId;
  disco.hostname = settings.hostname;
  disco.fwVersion = settings.fwVersion.name;
  char topic[128] = "";
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  shadeDisco(disco, obj, topic, sizeof(topic));
  mqtt.publishDisco(topic, obj, true);
}
void SomfyShade::unpublishDisco() {
  if(!mqtt.connected() || !settings.MQTT.pubDisco) return;
  char topic[128] = "";
  shadeDiscoTopic(settings.MQTT.discoTopic, this->shadeId, isDiscoSwitch(this->shadeType), topic, sizeof(topic));
  mqtt.unpublish(topic);
}
void SomfyShade::publish() {
  if(mqtt.connected()) {
    this->publish("shadeId", this->shadeId, true);
    this->publish("name", this->name, true);
    this->publish("remoteAddress", this->getRemoteAddress(), true);
    this->publish("shadeType", static_cast<uint8_t>(this->shadeType), true);
    this->publish("tiltType", static_cast<uint8_t>(this->tiltType), true);
    this->publish("flags", this->getFlags(), true);
    this->publish("flipCommands", this->flipCommands, true);
    this->publish("flipPosition", this->flipPosition, true);
    this->publishState();
    this->publishDisco();
  }
}
void SomfyGroup::publishState() {
  if(mqtt.connected()) {
    this->publish("direction", this->direction, true);
    this->publish("lastRollingCode", this->lastRollingCode, true);
    this->publish("flipCommands", this->flipCommands, true);
    const uint8_t sunFlag = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::SunFlag));
    const uint8_t isSunny = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny));
    const uint8_t isWindy = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::Windy));
    this->publish("sunFlag", sunFlag);
    this->publish("sunny", isSunny);
    this->publish("windy", isWindy);    
  }  
}
void SomfyGroup::publish() {
  if(mqtt.connected()) {
    this->publish("groupId", this->groupId, true);
    this->publish("name", this->name, true);
    this->publish("remoteAddress", this->getRemoteAddress(), true);
    this->publish("groupType", static_cast<uint8_t>(this->groupType), true);
    this->publish("flags", this->flags, true);
    this->publish("sunSensor", this->hasSunSensor(), true);
    this->publishState();
  }
}
char mqttTopicBuffer[55];
void SomfyGroup::unpublish() { SomfyGroup::unpublish(this->groupId); }
void SomfyShade::unpublish() { SomfyShade::unpublish(this->shadeId); }
void SomfyShade::unpublish(uint8_t id) {
  if(mqtt.connected()) {
    for(const char *topic : SHADE_TOPICS) SomfyShade::unpublish(id, topic);
    if(settings.MQTT.pubDisco) {
      char topic[128] = "";
      shadeDiscoTopic(settings.MQTT.discoTopic, id, false, topic, sizeof(topic));
      mqtt.unpublish(topic);
      shadeDiscoTopic(settings.MQTT.discoTopic, id, true, topic, sizeof(topic));
      mqtt.unpublish(topic);
    }
  }
}
void SomfyGroup::unpublish(uint8_t id) {
  if(mqtt.connected()) {
    for(const char *topic : GROUP_TOPICS) SomfyGroup::unpublish(id, topic);
  }
}
void SomfyGroup::unpublish(uint8_t id, const char *topic) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", id, topic);
    mqtt.unpublish(mqttTopicBuffer);
  }
}
void SomfyShade::unpublish(uint8_t id, const char *topic) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", id, topic);
    mqtt.unpublish(mqttTopicBuffer);
  }
}
bool SomfyShade::publish(const char *topic, int8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyShade::publish(const char *topic, const char *val, bool retain) { 
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyShade::publish(const char *topic, uint8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyShade::publish(const char *topic, uint32_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyShade::publish(const char *topic, uint16_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyShade::publish(const char *topic, bool val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyGroup::publish(const char *topic, int8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyGroup::publish(const char *topic, uint8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyGroup::publish(const char *topic, uint32_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyGroup::publish(const char *topic, uint16_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyGroup::publish(const char *topic, bool val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
void SomfyShadeController::publish() {
  this->updateGroupFlags();
  char arrIds[128] = "[";
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() == 255) continue;
    if(strlen(arrIds) > 1) strcat(arrIds, ",");
    itoa(shade->getShadeId(), &arrIds[strlen(arrIds)], 10);
    shade->publish();
  }
  strcat(arrIds, "]");
  mqtt.publish("shades", arrIds, true);
  for(uint8_t i = 1; i <= SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = this->getShadeById(i);
    if(shade) continue;
    else {
      SomfyShade::unpublish(i);
    }
  }
  strcpy(arrIds, "[");
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &this->groups[i];
    if(group->getGroupId() == 255) continue;
    if(strlen(arrIds) > 1) strcat(arrIds, ",");
    itoa(group->getGroupId(), &arrIds[strlen(arrIds)], 10);
    group->publish();
  }
  strcat(arrIds, "]");
  mqtt.publish("groups", arrIds, true);
  for(uint8_t i = 1; i <= SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = this->getGroupById(i);
    if(group) continue;
    else SomfyGroup::unpublish(i);
  }
}
