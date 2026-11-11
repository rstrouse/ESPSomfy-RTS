#ifndef SHADE_CHANGES_H
#define SHADE_CHANGES_H
#include <stdint.h>
#include "ItemSnapshot.h"

// What changed about a shade since the change fan-out last sent it out, as
// bits.  The shade's setters and emitState() set them; once per loop() pass the
// fan-out sends one shadeState for the shade, publishes the MQTT topics whose
// bits are set, and clears them.  Kept free of the shade model and MQTT so it
// can be unit tested on the PC (pio test -e native).
enum : uint16_t {
  SHADE_CHANGED_POSITION       = 0x0001,
  SHADE_CHANGED_TILT_POSITION  = 0x0002,
  SHADE_CHANGED_DIRECTION      = 0x0004,
  SHADE_CHANGED_TILT_DIRECTION = 0x0008,
  SHADE_CHANGED_TARGET         = 0x0010,
  SHADE_CHANGED_TILT_TARGET    = 0x0020,
  SHADE_CHANGED_MY_POS         = 0x0040,
  SHADE_CHANGED_MY_TILT_POS    = 0x0080,
  SHADE_CHANGED_ROLLING_CODE   = 0x0100,
  SHADE_CHANGED_SUN_FLAG       = 0x0200,
  SHADE_CHANGED_SUNNY          = 0x0400,
  SHADE_CHANGED_WINDY          = 0x0800,
  // Something only the socket's shadeState carries changed.
  SHADE_CHANGED_STATE          = 0x1000,
  // A setting changed (an edit, pairing, its room).  Protocol 2 clients get the
  // whole shade rather than its state.
  SHADE_CHANGED_CONFIG         = 0x2000
};
// The bits that have an MQTT topic.
#define SHADE_CHANGED_TOPICS 0x0FFF
// The same for a group: its direction topic, and groupState for the socket.
enum : uint8_t {
  GROUP_CHANGED_DIRECTION = 0x01,
  GROUP_CHANGED_STATE     = 0x02,
  // A setting or its shades changed: protocol 2 clients get the whole group.
  GROUP_CHANGED_CONFIG    = 0x04
};

// Publishes one MQTT topic: its name under shades/<id>/, its value and whether
// the broker keeps it.
typedef void (*topic_sink_t)(const char *topic, int32_t value, bool retain, void *ctx);
// Publishes the topics of the changed bits with the shade's values, positions
// as clients see them.
void publishShadeTopics(uint16_t changed, const shade_snapshot_t &shade, topic_sink_t sink, void *ctx);
// The topics a shade publishes in full when MQTT connects: the tilt ones only
// with a tilt, the sun ones only with a sun sensor.
uint16_t shadeStateTopics(const shade_snapshot_t &shade);
#endif
