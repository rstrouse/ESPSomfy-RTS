#ifndef SHADE_REQUESTS_H
#define SHADE_REQUESTS_H
#include <stdint.h>
#include "SomfyFrame.h"
#include "ShadeTypes.h"

// Shade requests from the entry points (HTTP and MQTT) and the domain module
// that carries them out on loop().  Kept free of the shade model so it can be
// unit tested on the PC (pio test -e native).

enum class shade_request_type_t : uint8_t {
  shade_command,    // move the shade to target, or send command
  group_command,    // send command to the group
  tilt_command,     // move the tilt to target, or send command to the tilt
  shade_repeat,     // send command, or repeat it when it was the last one sent
  group_repeat,     // the same for a group
  set_positions,    // correct where the shade is thought to be; sends nothing
  shade_sensor,     // send a sensor frame as the shade
  group_sensor,     // send a sensor frame as the group
  set_favorite,     // set or clear the shade's favorite
  unpair,           // send Prog and clear paired
  set_rolling_code  // set the shade's rolling code; sends nothing
};

// Positions are as the client sees them: 0-100, counted from the other end on
// a shade with flipPosition, and -1 when not given.
struct shade_request_t {
  shade_request_type_t type = shade_request_type_t::shade_command;
  uint8_t id = 255;                         // shade id, or group id for group_*
  int8_t target = -1;                       // shade/tilt_command: -1 sends command instead
  somfy_commands command = somfy_commands::My;
  int8_t repeat = -1;                       // -1 for the shade's or group's own repeats
  uint8_t stepSize = 0;
  int8_t position = -1;                     // set_positions, set_favorite
  int8_t tiltPosition = -1;                 // set_positions, set_favorite
  int8_t sunny = -1;                        // *_sensor: -1 leaves it as it is
  int8_t windy = -1;
  uint16_t rollingCode = 0;                 // set_rolling_code
};

// Whether the request sends anything on air.
bool shadeRequestUsesRadio(shade_request_type_t type);
// Whether the request's id is a group id rather than a shade id.
bool shadeRequestIsForGroup(shade_request_type_t type);
// A repeat count from a client: negative for the default, capped at what fits.
int8_t requestRepeat(int32_t repeat);
// A target from a client: -1 unless it is 0-100.
int8_t requestTarget(int32_t target);

// What ShadeRequests needs to know about a shade.  Positions are stored ones.
struct shade_info_t {
  bool flipPosition = false;
  shade_types shadeType = shade_types::roller;
  tilt_types tiltType = tilt_types::none;
  uint8_t repeats = 1;
  uint8_t bitLength = 56;
  float currentPos = 0.0f;
  float myPos = -1.0f;
  float myTiltPos = -1.0f;
  bool idle = true;  // not moving, and at its target
};

// The seam between ShadeRequests and the shades and groups it acts on.  On the
// device it is the shade model (SomfyShadeOps); the tests record the calls.
// Positions are stored ones, so already turned round for a flipped shade.
class ShadeOps {
  public:
    virtual ~ShadeOps() {}
    virtual bool findShade(uint8_t shadeId, shade_info_t &info) = 0;
    // repeats is the group's own repeat count.
    virtual bool findGroup(uint8_t groupId, uint8_t &repeats) = 0;
    // The bool ones return false when the radio refused a command they sent.
    virtual bool moveToTarget(uint8_t shadeId, float pos) = 0;
    virtual bool moveToTiltTarget(uint8_t shadeId, float tilt) = 0;
    virtual bool sendCommand(uint8_t shadeId, somfy_commands cmd, uint8_t repeat, uint8_t stepSize) = 0;
    virtual bool sendTiltCommand(uint8_t shadeId, somfy_commands cmd) = 0;
    virtual bool isLastCommand(uint8_t shadeId, somfy_commands cmd) = 0;
    virtual bool repeatFrame(uint8_t shadeId, uint8_t repeat) = 0;
    virtual bool sendSensorCommand(uint8_t shadeId, int8_t windy, int8_t sunny, uint8_t repeat) = 0;
    virtual bool setMyPosition(uint8_t shadeId, int8_t pos, int8_t tilt) = 0;
    // A negative position leaves that one as it is.
    virtual void setPositions(uint8_t shadeId, float pos, float tilt) = 0;
    virtual void setUnpaired(uint8_t shadeId) = 0;
    virtual void setRollingCode(uint8_t shadeId, uint16_t code) = 0;
    virtual bool sendGroupCommand(uint8_t groupId, somfy_commands cmd, uint8_t repeat, uint8_t stepSize) = 0;
    virtual bool isLastGroupCommand(uint8_t groupId, somfy_commands cmd) = 0;
    virtual bool repeatGroupFrame(uint8_t groupId, uint8_t repeat) = 0;
    virtual bool sendGroupSensorCommand(uint8_t groupId, int8_t windy, int8_t sunny, uint8_t repeat) = 0;
};

// How a request went.  One with nothing in range to do is done.
enum class shade_request_result_t : uint8_t {
  done,
  not_found,      // no shade or group with that id; nothing was done
  radio_refused,  // the radio didn't take a command it sent
  shade_moving,   // a favorite is only set on a shade at rest; nothing was done
  no_tilt         // only a tilt was given, for a shade without one; nothing was done
};

// Carries out shade requests, whichever entry point they came from: finds
// the shade or group, turns client positions into stored ones, checks
// ranges and fills in default repeats.  Runs on loop() only.
class ShadeRequests {
  public:
    explicit ShadeRequests(ShadeOps &ops) : ops(ops) {}
    shade_request_result_t run(const shade_request_t &req);
  private:
    ShadeOps &ops;
};
#endif
