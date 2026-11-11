#ifndef SHADE_TYPES_H
#define SHADE_TYPES_H
#include <Arduino.h>

// The kinds of shade, tilt and group, and how many of each the shade model
// holds.  Kept free of the shade model so the shade maths can use them in the
// unit tests on the PC (pio test -e native).

#define SOMFY_MAX_SHADES 32
#define SOMFY_MAX_GROUPS 16
#define SOMFY_MAX_LINKED_REMOTES 7
#define SOMFY_MAX_GROUPED_SHADES 32
#define SOMFY_MAX_ROOMS 16
#define SOMFY_MAX_REPEATERS 7

enum class group_types : byte {
  channel = 0x00
};
enum class shade_types : byte {
  roller = 0x00,
  blind = 0x01,
  ldrapery = 0x02,
  awning = 0x03,
  shutter = 0x04,
  garage1 = 0x05,
  garage3 = 0x06,
  rdrapery = 0x07,
  cdrapery = 0x08,
  drycontact = 0x09,
  drycontact2 = 0x0A,
  lgate = 0x0B,
  cgate = 0x0C,
  rgate = 0x0D,
  lgate1 = 0x0E,
  cgate1 = 0x0F,
  rgate1 = 0x10
};
// A garage door or gate worked with one button: each press moves, stops or
// reverses it, so it can't be sent to a position.
inline bool isToggleShade(shade_types type) {
  switch(type) {
    case shade_types::garage1:
    case shade_types::lgate1:
    case shade_types::cgate1:
    case shade_types::rgate1:
      return true;
    default:
      return false;
  }
}
// The bits of a shade's or group's flags.
enum class somfy_flags_t : byte {
    SunFlag = 0x01,
    SunSensor = 0x02,
    DemoMode = 0x04,
    Light = 0x08,
    Windy = 0x10,
    Sunny = 0x20,
    Lighted = 0x40,
    SimMy = 0x80
};
enum class tilt_types : byte {
  none = 0x00,
  tiltmotor = 0x01,
  integrated = 0x02,
  tiltonly = 0x03,
  euromode = 0x04
};
#endif
