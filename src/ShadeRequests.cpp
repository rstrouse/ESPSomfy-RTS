#include "ShadeRequests.h"
#include "ShadeMotion.h"

bool shadeRequestUsesRadio(shade_request_type_t type) {
  switch(type) {
    case shade_request_type_t::set_positions:
    case shade_request_type_t::set_rolling_code:
      return false;
    default:
      return true;
  }
}
bool shadeRequestIsForGroup(shade_request_type_t type) {
  switch(type) {
    case shade_request_type_t::group_command:
    case shade_request_type_t::group_repeat:
    case shade_request_type_t::group_sensor:
      return true;
    default:
      return false;
  }
}
int8_t requestRepeat(int32_t repeat) {
  if(repeat < 0) return -1;
  return repeat > INT8_MAX ? INT8_MAX : static_cast<int8_t>(repeat);
}
int8_t requestTarget(int32_t target) { return target >= 0 && target <= 100 ? static_cast<int8_t>(target) : -1; }

static bool inRange(int8_t pos) { return pos >= 0 && pos <= 100; }
// Turning a client position round is the same sum as turning a stored one.
static int8_t storedPosition(int8_t pos, bool flip) { return displayPosition(static_cast<float>(pos), flip); }
static uint8_t repeatsOr(int8_t repeat, uint8_t own) { return repeat >= 0 ? static_cast<uint8_t>(repeat) : own; }

static shade_request_result_t sent(bool taken) { return taken ? shade_request_result_t::done : shade_request_result_t::radio_refused; }

shade_request_result_t ShadeRequests::run(const shade_request_t &req) {
  shade_info_t shade;
  uint8_t groupRepeats = 0;
  if(shadeRequestIsForGroup(req.type)) {
    if(!this->ops.findGroup(req.id, groupRepeats)) return shade_request_result_t::not_found;
  }
  else if(!this->ops.findShade(req.id, shade)) return shade_request_result_t::not_found;
  const bool flip = shade.flipPosition;
  switch(req.type) {
    case shade_request_type_t::shade_command:
      if(inRange(req.target)) return sent(this->ops.moveToTarget(req.id, storedPosition(req.target, flip)));
      return sent(this->ops.sendCommand(req.id, req.command, repeatsOr(req.repeat, shade.repeats), req.stepSize));
    case shade_request_type_t::group_command:
      return sent(this->ops.sendGroupCommand(req.id, req.command, repeatsOr(req.repeat, groupRepeats), req.stepSize));
    case shade_request_type_t::tilt_command:
      if(inRange(req.target)) return sent(this->ops.moveToTiltTarget(req.id, storedPosition(req.target, flip)));
      return sent(this->ops.sendTiltCommand(req.id, req.command));
    case shade_request_type_t::shade_repeat: {
      // A single button garage door has no Prog, its button is Toggle.
      somfy_commands cmd = req.command;
      if(shade.shadeType == shade_types::garage1 && cmd == somfy_commands::Prog) cmd = somfy_commands::Toggle;
      const uint8_t repeat = repeatsOr(req.repeat, shade.repeats);
      if(this->ops.isLastCommand(req.id, cmd)) return sent(this->ops.repeatFrame(req.id, repeat));
      return sent(this->ops.sendCommand(req.id, cmd, repeat, req.stepSize));
    }
    case shade_request_type_t::group_repeat: {
      const uint8_t repeat = repeatsOr(req.repeat, groupRepeats);
      if(this->ops.isLastGroupCommand(req.id, req.command)) return sent(this->ops.repeatGroupFrame(req.id, repeat));
      return sent(this->ops.sendGroupCommand(req.id, req.command, repeat, req.stepSize));
    }
    case shade_request_type_t::set_positions: {
      const float pos = inRange(req.position) ? storedPosition(req.position, flip) : -1.0f;
      const float tilt = inRange(req.tiltPosition) && shade.tiltType != tilt_types::none ? storedPosition(req.tiltPosition, flip) : -1.0f;
      // A tilt on its own for a shade without one has nothing to set.
      if(pos < 0.0f && inRange(req.tiltPosition) && shade.tiltType == tilt_types::none) return shade_request_result_t::no_tilt;
      if(pos >= 0.0f || tilt >= 0.0f) this->ops.setPositions(req.id, pos, tilt);
      return shade_request_result_t::done;
    }
    case shade_request_type_t::shade_sensor:
      return sent(this->ops.sendSensorCommand(req.id, req.windy, req.sunny, repeatsOr(req.repeat, shade.repeats)));
    case shade_request_type_t::group_sensor:
      return sent(this->ops.sendGroupSensorCommand(req.id, req.windy, req.sunny, repeatsOr(req.repeat, groupRepeats)));
    case shade_request_type_t::set_favorite: {
      // The shade only takes a favorite at rest, and would ignore it.
      if(!shade.idle) return shade_request_result_t::shade_moving;
      const int8_t tilt = requestedFavoriteTilt(req.tiltPosition, shade.tiltType, shade.myTiltPos, flip);
      // Without a lift position only the tilt changes: the lift favorite stays,
      // or is set where the shade is when it has none.
      int8_t pos = req.position;
      if(!inRange(pos)) pos = displayPosition(shade.myPos >= 0.0f ? shade.myPos : shade.currentPos, flip);
      return sent(this->ops.setMyPosition(req.id, storedPosition(pos, flip), storedPosition(tilt, flip)));
    }
    case shade_request_type_t::unpair: {
      // A 56-bit motor needs a longer Prog press than an 80-bit one.
      const bool taken = this->ops.sendCommand(req.id, somfy_commands::Prog, shade.bitLength == 56 ? 7 : 1, 0);
      if(taken) this->ops.setUnpaired(req.id);
      return sent(taken);
    }
    case shade_request_type_t::set_rolling_code:
      this->ops.setRollingCode(req.id, req.rollingCode);
      return shade_request_result_t::done;
  }
  return shade_request_result_t::done;
}
