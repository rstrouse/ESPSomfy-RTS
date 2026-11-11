#ifndef MQTT_TOPICS_H
#define MQTT_TOPICS_H
#include <stddef.h>

// The topics the broker keeps for a shade (shades/<id>/) and a group
// (groups/<id>/), which deleting one clears.  Kept free of MQTT so the lists
// can be checked against what is published in the unit tests on the PC
// (pio test -e native).
inline constexpr const char *SHADE_TOPICS[] = {
  "shadeId", "name", "remoteAddress", "shadeType", "tiltType", "flags", "flipCommands", "flipPosition",
  "position", "direction", "target", "lastRollingCode", "mypos", "myTiltPos",
  "tiltDirection", "tiltPosition", "tiltTarget", "windy", "sunny"
};
inline constexpr const char *GROUP_TOPICS[] = {
  "groupId", "name", "remoteAddress", "groupType", "direction", "lastRollingCode", "flags", "sunSensor", "flipCommands"
};
inline constexpr size_t SHADE_TOPIC_COUNT = sizeof(SHADE_TOPICS) / sizeof(SHADE_TOPICS[0]);
inline constexpr size_t GROUP_TOPIC_COUNT = sizeof(GROUP_TOPICS) / sizeof(GROUP_TOPICS[0]);
#endif
