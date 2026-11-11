#ifndef MQTT_DISCO_H
#define MQTT_DISCO_H
#include <ArduinoJson.h>
#include "ShadeTypes.h"

// What Home Assistant's MQTT discovery needs to know about a shade.  Kept
// free of the shade model and MQTT so the config it gets can be unit tested
// on the PC (pio test -e native).
struct shade_disco_t {
  uint8_t shadeId = 0;
  const char *name = "";
  shade_types shadeType = shade_types::roller;
  tilt_types tiltType = tilt_types::none;
  bool flipPosition = false;
  // From the MQTT and general settings.
  const char *rootTopic = "";
  const char *discoTopic = "";
  const char *serverId = "";
  const char *hostname = "";
  const char *fwVersion = "";
};
// Dry contacts are switches, the rest covers.
bool isDiscoSwitch(shade_types type);
// Where the shade's config goes: <discoTopic>/cover/<id>/config or
// <discoTopic>/switch/<id>/config.
void shadeDiscoTopic(const char *discoTopic, uint8_t shadeId, bool isSwitch, char *topic, size_t len);
// Fills obj with the shade's config and topic with where it goes.
void shadeDisco(const shade_disco_t &shade, JsonObject obj, char *topic, size_t len);
#endif
