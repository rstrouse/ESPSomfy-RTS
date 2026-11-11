#include "ItemSnapshot.h"

void writeShadeRef(JsonFormatter &json, const shade_snapshot_t &shade) {
  json.addElem("shadeId", shade.shadeId);
  json.addElem("roomId", shade.roomId);
  json.addElem("name", shade.name);
  json.addElem("remoteAddress", shade.remoteAddress);
  json.addElem("paired", shade.paired);
  json.addElem("shadeType", shade.shadeType);
  json.addElem("flipCommands", shade.flipCommands);
  json.addElem("flipPosition", shade.flipPosition);
  json.addElem("bitLength", shade.bitLength);
  json.addElem("proto", shade.proto);
  json.addElem("flags", shade.flags);
  json.addElem("sunSensor", shade.sunSensor);
  json.addElem("hasLight", shade.light);
  json.addElem("repeats", shade.repeats);
}
void writeGroupRef(JsonFormatter &json, const group_snapshot_t &group) {
  json.addElem("groupId", group.groupId);
  json.addElem("roomId", group.roomId);
  json.addElem("name", group.name);
  json.addElem("remoteAddress", group.remoteAddress);
  json.addElem("lastRollingCode", static_cast<uint32_t>(group.lastRollingCode));
  json.addElem("bitLength", group.bitLength);
  json.addElem("proto", group.proto);
  json.addElem("sunSensor", group.sunSensor);
  json.addElem("flipCommands", group.flipCommands);
  json.addElem("flags", group.flags);
  json.addElem("repeats", group.repeats);
  json.addElem("sortOrder", group.sortOrder);
}
