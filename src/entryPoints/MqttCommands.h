#ifndef MQTT_COMMANDS_H
#define MQTT_COMMANDS_H
#include <stddef.h>
#include <stdint.h>
#include "ShadeRequests.h"

// The MQTT command topics, <root>/shades/<id>/<command>/set and
// <root>/groups/<id>/<command>/set.  Kept free of the MQTT client so it can be
// unit tested on the PC (pio test -e native).

// Turns a message on a command topic into a shade request.  False for a topic
// or value it doesn't take, which is dropped.
bool parseMqttCommand(const char *topic, const uint8_t *payload, size_t length, shade_request_t &req);

// The command topics under the root topic, + standing for any id.  The MQTT
// connection subscribes to these.
#define MQTT_COMMAND_TOPIC_COUNT 14
extern const char *const MQTT_COMMAND_TOPICS[MQTT_COMMAND_TOPIC_COUNT];
#endif
