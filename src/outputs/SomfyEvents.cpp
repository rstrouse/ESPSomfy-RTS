#include "Somfy.h"
#include "ProtoSocket.h"
#include "MqttConnection.h"

extern MqttConnection mqtt;

void SomfyShade::emitState() { this->changed |= SHADE_CHANGED_STATE; }
void SomfyShade::emitCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress) {
  protoSocket.shadeCommanded(*this, translateSomfyCommand(cmd).c_str(), source, sourceAddress);
  if(mqtt.connected()) {
    this->publish("cmdSource", source);
    this->publish("cmdAddress", sourceAddress);
    this->publish("cmd", translateSomfyCommand(cmd).c_str());
  }
}
void SomfyGroup::emitState() { this->changed |= GROUP_CHANGED_STATE; }
// The change fan-out.  Everything that changed about a shade or group in this
// loop() pass goes out once: its MQTT topics, then one state event.
void SomfyShadeController::sendChanges() {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade &shade = this->shades[i];
    const uint16_t changed = shade.changed;
    shade.changed = 0;
    if(!changed || shade.getShadeId() == 255) continue;
    if(changed & SHADE_CHANGED_TOPICS) shade.publishTopics(changed);
    if(changed & SHADE_CHANGED_CONFIG) protoSocket.shade(shade);
    else protoSocket.shadeState(shade);
  }
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup &group = this->groups[i];
    const uint8_t changed = group.changed;
    group.changed = 0;
    if(!changed || group.getGroupId() == 255) continue;
    if(changed & GROUP_CHANGED_DIRECTION) group.publish("direction", group.direction);
    group.publish();
    if(changed & GROUP_CHANGED_CONFIG) protoSocket.group(group);
    else protoSocket.groupState(group);
  }
}
