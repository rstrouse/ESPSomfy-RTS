#include "SomfyShadeOps.h"
#include "Somfy.h"
#include "ShadeChanges.h"

extern SomfyShadeController somfy;

// ShadeRequests only calls these for a shade or group it has just found.
bool SomfyShadeOps::findShade(uint8_t shadeId, shade_info_t &info) {
  SomfyShade *shade = somfy.getShadeById(shadeId);
  if(!shade) return false;
  info.flipPosition = shade->flipPosition;
  info.shadeType = shade->shadeType;
  info.tiltType = shade->tiltType;
  info.repeats = shade->repeats;
  info.bitLength = shade->bitLength;
  info.currentPos = shade->state().currentPos;
  info.myPos = shade->state().myPos;
  info.myTiltPos = shade->state().myTiltPos;
  info.idle = shade->isIdle();
  return true;
}
bool SomfyShadeOps::findGroup(uint8_t groupId, uint8_t &repeats) {
  SomfyGroup *group = somfy.getGroupById(groupId);
  if(!group) return false;
  repeats = group->repeats;
  return true;
}
bool SomfyShadeOps::moveToTarget(uint8_t shadeId, float pos) { return somfy.getShadeById(shadeId)->moveToTarget(pos); }
bool SomfyShadeOps::moveToTiltTarget(uint8_t shadeId, float tilt) { return somfy.getShadeById(shadeId)->moveToTiltTarget(tilt); }
bool SomfyShadeOps::sendCommand(uint8_t shadeId, somfy_commands cmd, uint8_t repeat, uint8_t stepSize) {
  return somfy.getShadeById(shadeId)->sendCommand(cmd, repeat, stepSize);
}
bool SomfyShadeOps::sendTiltCommand(uint8_t shadeId, somfy_commands cmd) { return somfy.getShadeById(shadeId)->sendTiltCommand(cmd); }
bool SomfyShadeOps::isLastCommand(uint8_t shadeId, somfy_commands cmd) { return somfy.getShadeById(shadeId)->isLastCommand(cmd); }
bool SomfyShadeOps::repeatFrame(uint8_t shadeId, uint8_t repeat) { return somfy.getShadeById(shadeId)->repeatFrame(repeat); }
bool SomfyShadeOps::sendSensorCommand(uint8_t shadeId, int8_t windy, int8_t sunny, uint8_t repeat) {
  SomfyShade *shade = somfy.getShadeById(shadeId);
  const bool sent = shade->sendSensorCommand(windy, sunny, repeat);
  shade->emitState();
  return sent;
}
bool SomfyShadeOps::setMyPosition(uint8_t shadeId, int8_t pos, int8_t tilt) { return somfy.getShadeById(shadeId)->setMyPosition(pos, tilt); }
void SomfyShadeOps::setPositions(uint8_t shadeId, float pos, float tilt) {
  SomfyShade *shade = somfy.getShadeById(shadeId);
  shade->setPosition(pos, tilt);
  shade->emitState();
}
void SomfyShadeOps::setUnpaired(uint8_t shadeId) {
  SomfyShade *shade = somfy.getShadeById(shadeId);
  shade->paired = false;
  shade->save();
  shade->configChanged();
}
void SomfyShadeOps::setRollingCode(uint8_t shadeId, uint16_t code) { somfy.getShadeById(shadeId)->setRollingCode(code); }
bool SomfyShadeOps::sendGroupCommand(uint8_t groupId, somfy_commands cmd, uint8_t repeat, uint8_t stepSize) {
  return somfy.getGroupById(groupId)->sendCommand(cmd, repeat, stepSize);
}
bool SomfyShadeOps::isLastGroupCommand(uint8_t groupId, somfy_commands cmd) { return somfy.getGroupById(groupId)->isLastCommand(cmd); }
bool SomfyShadeOps::repeatGroupFrame(uint8_t groupId, uint8_t repeat) { return somfy.getGroupById(groupId)->repeatFrame(repeat); }
bool SomfyShadeOps::sendGroupSensorCommand(uint8_t groupId, int8_t windy, int8_t sunny, uint8_t repeat) {
  SomfyGroup *group = somfy.getGroupById(groupId);
  const bool sent = group->sendSensorCommand(windy, sunny, repeat);
  group->emitState();
  return sent;
}
