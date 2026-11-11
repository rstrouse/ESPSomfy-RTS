#ifndef ITEM_SNAPSHOT_H
#define ITEM_SNAPSHOT_H
#include <stddef.h>
#include <stdint.h>
#include "ShadeTypes.h"
#include "JsonFormatter.h"

// A copy of one room, shade or group, taken on loop() or for a command reply,
// that the socket events, MQTT and the HTTP command replies write from.  Kept
// free of the shade model and the web server so it can be unit tested on the PC
// (pio test -e native).

// Positions are as clients see them: whole percentages, flipped for a shade
// with flipPosition, -1 when unset.  The snapshot functions (SomfySnapshot.h)
// set every field; anything else starts from {}.
struct room_snapshot_t {
  uint8_t roomId;
  char name[21];
  int8_t sortOrder;
};
struct linked_remote_snapshot_t {
  uint32_t remoteAddress;
  uint16_t lastRollingCode;
};
struct shade_snapshot_t {
  uint8_t shadeId;
  uint8_t roomId;
  char name[21];
  uint32_t remoteAddress;
  uint32_t upTime;
  uint32_t downTime;
  uint32_t tiltTime;
  uint16_t stepSize;
  bool paired;
  uint16_t lastRollingCode;
  int8_t position;
  int8_t target;
  int8_t myPos;
  int8_t direction;
  uint8_t tiltType;
  int8_t tiltPosition;
  int8_t tiltTarget;
  int8_t myTiltPos;
  int8_t tiltDirection;
  uint8_t shadeType;
  uint8_t bitLength;
  uint8_t proto;
  uint8_t flags;
  bool flipCommands;
  bool flipPosition;
  bool inGroup;
  bool sunSensor;
  bool light;
  bool simMy;
  uint8_t repeats;
  int8_t sortOrder;
  uint8_t gpioUp;
  uint8_t gpioDown;
  uint8_t gpioMy;
  bool gpioLLTrigger;
  uint8_t linkedRemoteCount;
  linked_remote_snapshot_t linkedRemotes[SOMFY_MAX_LINKED_REMOTES];
};
struct group_snapshot_t {
  uint8_t groupId;
  uint8_t roomId;
  char name[21];
  uint32_t remoteAddress;
  uint16_t lastRollingCode;
  uint8_t bitLength;
  uint8_t proto;
  bool sunSensor;
  bool flipCommands;
  uint8_t flags;          // its linked shades' flags together
  uint8_t repeats;
  int8_t sortOrder;
  uint8_t linkedShadeCount;
  uint8_t linkedShades[SOMFY_MAX_GROUPED_SHADES];
};
// The fields the HTTP command replies have always sent about their shade or
// group.
void writeShadeRef(JsonFormatter &json, const shade_snapshot_t &shade);
void writeGroupRef(JsonFormatter &json, const group_snapshot_t &group);
#endif
