#ifndef SHADE_SETTINGS_H
#define SHADE_SETTINGS_H
#include <Arduino.h>
#include "ShadeTypes.h"

// The settings a shade's behaviour reads and never changes.  The user sets
// them through JSON, the config file and ModelEditor.  Kept free of the shade
// model so they can be used in the unit tests on the PC (pio test -e native).

// What shades and groups share: how their frames go on air.
struct RemoteSettings {
  bool flipCommands = false;
  uint8_t bitLength = 0;
  uint8_t repeats = 1;
};
// What only a shade has.
struct ShadeSettings {
  shade_types shadeType = shade_types::roller;
  tilt_types tiltType = tilt_types::none;
  uint32_t upTime = 10000;
  uint32_t downTime = 10000;
  uint32_t tiltTime = 7000;
  uint16_t stepSize = 100;
  bool flipPosition = false;
  // Reaches the favorite by moving to it rather than sending My.
  bool simMy = false;
  bool sunSensor = false;
};

// The bits of a shade's flags byte that are settings.  The rest are state the
// shade changes (sun flag, sunny, windy, demo mode) or unused (lighted).
static const uint8_t SHADE_SETTING_FLAGS = static_cast<uint8_t>(somfy_flags_t::SunSensor) |
  static_cast<uint8_t>(somfy_flags_t::Light) | static_cast<uint8_t>(somfy_flags_t::SimMy);

// The flags byte as it is saved and sent, put together from where its bits live.
inline uint8_t joinShadeFlags(const ShadeSettings &settings, bool light, uint8_t stateFlags) {
  uint8_t flags = stateFlags & ~SHADE_SETTING_FLAGS;
  if(settings.sunSensor) flags |= static_cast<uint8_t>(somfy_flags_t::SunSensor);
  if(light) flags |= static_cast<uint8_t>(somfy_flags_t::Light);
  if(settings.simMy) flags |= static_cast<uint8_t>(somfy_flags_t::SimMy);
  return flags;
}
// Splits a saved or sent flags byte back to where its bits live.
inline void splitShadeFlags(uint8_t flags, ShadeSettings &settings, bool &light, uint8_t &stateFlags) {
  settings.sunSensor = flags & static_cast<uint8_t>(somfy_flags_t::SunSensor);
  light = flags & static_cast<uint8_t>(somfy_flags_t::Light);
  settings.simMy = flags & static_cast<uint8_t>(somfy_flags_t::SimMy);
  stateFlags = flags & ~SHADE_SETTING_FLAGS;
}
#endif
