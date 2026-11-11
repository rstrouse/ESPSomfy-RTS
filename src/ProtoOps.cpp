#include <stdio.h>
#include <strings.h>
#include "ProtoOps.h"

// An op reads only the fields that were sent: unlike what the device sends, a
// left-out field is not its schema default but not given, so a client can
// leave a shade's tilt or its sun flag as they are.
class OpReader {
  public:
    explicit OpReader(JsonObjectConst d) : d(d) {}
    bool has(proto_field_t field) const { return !this->d[protoField(field).key].isNull(); }
    // The field when it was sent, as an integer in lo..hi.  False when it
    // wasn't; error is set when it was but isn't one.
    bool get(proto_field_t field, int32_t lo, int32_t hi, int32_t &out) {
      JsonVariantConst v = this->d[protoField(field).key];
      if(v.isNull()) return false;
      if(!v.is<int32_t>() || v.as<int32_t>() < lo || v.as<int32_t>() > hi) {
        this->fail("%s must be %ld-%ld", protoField(field).name, (long)lo, (long)hi);
        return false;
      }
      out = v.as<int32_t>();
      return true;
    }
    // The same for a field the op can't do without.
    bool need(proto_field_t field, int32_t lo, int32_t hi, int32_t &out) {
      if(!this->has(field)) {
        this->fail("%s is missing", protoField(field).name);
        return false;
      }
      return this->get(field, lo, hi, out);
    }
    bool command(somfy_commands &cmd) {
      if(!this->has(proto_field_t::command)) {
        this->fail("command is missing");
        return false;
      }
      if(!protoCommand(protoString(proto_field_t::command), cmd)) {
        this->fail("Not a command");
        return false;
      }
      return true;
    }
    // Exactly one of the two.
    bool either(proto_field_t a, proto_field_t b) {
      if(this->has(a) == this->has(b)) {
        this->fail("Give %s or %s", protoField(a).name, protoField(b).name);
        return false;
      }
      return true;
    }
    template<typename... Args>
    void fail(const char *fmt, Args... args) {
      // Only the first problem is reported.
      if(this->error) return;
      snprintf(this->why, sizeof(this->why), fmt, args...);
      this->error = this->why;
    }
    const char *error = nullptr;
  private:
    JsonObjectConst d;
    char why[64];
    const char *protoString(proto_field_t field) const {
      JsonVariantConst v = this->d[protoField(field).key];
      return v.is<const char *>() ? v.as<const char *>() : nullptr;
    }
};
// The reader's problem, kept once the reader has gone so the reply can send
// it.  One buffer for every parse: they all run on async_tcp, one at a time.
static char lastError[64];
static const char *errorOf(OpReader &r) {
  if(!r.error) return nullptr;
  snprintf(lastError, sizeof(lastError), "%s", r.error);
  return lastError;
}

struct command_name_t {
  const char *name;
  somfy_commands cmd;
};
// The names, and the spellings shadeCommanded reports, so they read back.
static const command_name_t COMMANDS[] = {
  {"My", somfy_commands::My}, {"Up", somfy_commands::Up}, {"Down", somfy_commands::Down},
  {"MyUp", somfy_commands::MyUp}, {"My+Up", somfy_commands::MyUp},
  {"MyDown", somfy_commands::MyDown}, {"My+Down", somfy_commands::MyDown},
  {"UpDown", somfy_commands::UpDown}, {"Up+Down", somfy_commands::UpDown},
  {"MyUpDown", somfy_commands::MyUpDown}, {"My+Up+Down", somfy_commands::MyUpDown},
  {"Prog", somfy_commands::Prog},
  {"SunFlag", somfy_commands::SunFlag}, {"Sun Flag", somfy_commands::SunFlag},
  {"Flag", somfy_commands::Flag},
  {"StepUp", somfy_commands::StepUp}, {"Step Up", somfy_commands::StepUp},
  {"StepDown", somfy_commands::StepDown}, {"Step Down", somfy_commands::StepDown},
  {"Toggle", somfy_commands::Toggle}, {"Sensor", somfy_commands::Sensor},
  {"Favorite", somfy_commands::Favorite}, {"Stop", somfy_commands::Stop}
};
bool protoCommand(const char *name, somfy_commands &cmd) {
  if(!name) return false;
  for(const command_name_t &c : COMMANDS) {
    if(strcasecmp(c.name, name) == 0) {
      cmd = c.cmd;
      return true;
    }
  }
  return false;
}

// The repeat count and step size a command may carry.
static void readRepeat(OpReader &r, shade_request_t &req) {
  int32_t n;
  if(r.get(proto_field_t::repeat, 0, INT8_MAX, n)) req.repeat = static_cast<int8_t>(n);
}
static void readStep(OpReader &r, shade_request_t &req) {
  int32_t n;
  // Steps are 7 bits on air.
  if(r.get(proto_field_t::stepSize, 0, 127, n)) req.stepSize = static_cast<uint8_t>(n);
}
static void readId(OpReader &r, proto_field_t field, shade_request_t &req) {
  int32_t n;
  if(r.need(field, 0, 255, n)) req.id = static_cast<uint8_t>(n);
}
static void readShade(OpReader &r, shade_request_t &req) { readId(r, proto_field_t::shadeId, req); }
// A shade or a group; req.type becomes the one that was given.
static void readShadeOrGroup(OpReader &r, shade_request_t &req, shade_request_type_t shade, shade_request_type_t group) {
  if(!r.either(proto_field_t::shadeId, proto_field_t::groupId)) return;
  const bool isShade = r.has(proto_field_t::shadeId);
  req.type = isShade ? shade : group;
  readId(r, isShade ? proto_field_t::shadeId : proto_field_t::groupId, req);
}
// A command, or a position 0-100 to move to.
static void readCommandOrTarget(OpReader &r, shade_request_t &req) {
  if(!r.either(proto_field_t::command, proto_field_t::target)) return;
  int32_t n;
  if(r.has(proto_field_t::command)) r.command(req.command);
  else if(r.get(proto_field_t::target, 0, 100, n)) req.target = static_cast<int8_t>(n);
}
// Two values in lo..hi of which at least one is given; the other is left as
// it was.
static void readOneOrBoth(OpReader &r, proto_field_t a, proto_field_t b, int32_t lo, int32_t hi, int8_t &aOut, int8_t &bOut) {
  if(!r.has(a) && !r.has(b)) {
    r.fail("Give %s, %s or both", protoField(a).name, protoField(b).name);
    return;
  }
  int32_t n;
  if(r.get(a, lo, hi, n)) aOut = static_cast<int8_t>(n);
  if(r.get(b, lo, hi, n)) bOut = static_cast<int8_t>(n);
}

const char *parseShadeOp(uint8_t type, JsonObjectConst d, shade_request_t &req) {
  req = shade_request_t();
  OpReader r(d);
  int32_t n;
  switch(static_cast<proto_type_t>(type)) {
    case proto_type_t::shadeCommand:
      req.type = shade_request_type_t::shade_command;
      readShade(r, req);
      readCommandOrTarget(r, req);
      readRepeat(r, req);
      readStep(r, req);
      break;
    case proto_type_t::tiltCommand:
      req.type = shade_request_type_t::tilt_command;
      readShade(r, req);
      readCommandOrTarget(r, req);
      break;
    case proto_type_t::groupCommand:
      req.type = shade_request_type_t::group_command;
      readId(r, proto_field_t::groupId, req);
      r.command(req.command);
      readRepeat(r, req);
      readStep(r, req);
      break;
    case proto_type_t::repeatCommand:
      readShadeOrGroup(r, req, shade_request_type_t::shade_repeat, shade_request_type_t::group_repeat);
      r.command(req.command);
      readRepeat(r, req);
      readStep(r, req);
      break;
    case proto_type_t::setPositions:
      req.type = shade_request_type_t::set_positions;
      readShade(r, req);
      readOneOrBoth(r, proto_field_t::position, proto_field_t::tiltPosition, 0, 100, req.position, req.tiltPosition);
      break;
    case proto_type_t::setSensor:
      readShadeOrGroup(r, req, shade_request_type_t::shade_sensor, shade_request_type_t::group_sensor);
      readOneOrBoth(r, proto_field_t::sunny, proto_field_t::windy, 0, 1, req.sunny, req.windy);
      readRepeat(r, req);
      break;
    case proto_type_t::setMyPosition:
      req.type = shade_request_type_t::set_favorite;
      readShade(r, req);
      readOneOrBoth(r, proto_field_t::myPos, proto_field_t::myTiltPos, 0, 100, req.position, req.tiltPosition);
      break;
    case proto_type_t::unpairShade:
      req.type = shade_request_type_t::unpair;
      readShade(r, req);
      break;
    case proto_type_t::setRollingCode:
      req.type = shade_request_type_t::set_rolling_code;
      readShade(r, req);
      if(r.need(proto_field_t::lastRollingCode, 0, UINT16_MAX, n)) req.rollingCode = static_cast<uint16_t>(n);
      break;
    default:
      r.fail("Not a shade op");
      break;
  }
  return errorOf(r);
}

const char *parseRemoteCommandOp(JsonObjectConst d, somfy_frame_t &frame, uint8_t &repeats) {
  frame = somfy_frame_t();
  repeats = 0;
  OpReader r(d);
  int32_t n;
  // A remote address is 24 bits, and 0 is no remote.
  if(r.need(proto_field_t::remoteAddress, 1, 0xFFFFFF, n)) frame.remoteAddress = static_cast<uint32_t>(n);
  r.command(frame.cmd);
  if(r.need(proto_field_t::lastRollingCode, 1, UINT16_MAX, n)) frame.rollingCode = static_cast<uint16_t>(n);
  if(r.get(proto_field_t::encKey, 0, UINT8_MAX, n)) frame.encKey = static_cast<uint8_t>(n);
  if(r.get(proto_field_t::repeats, 0, UINT8_MAX, n)) repeats = static_cast<uint8_t>(n);
  return errorOf(r);
}

proto_error_t protoRequestError(shade_request_result_t result) {
  switch(result) {
    case shade_request_result_t::not_found: return proto_error_t::notFound;
    case shade_request_result_t::radio_refused: return proto_error_t::radioUnavailable;
    // At rest it would take it: the client may try again.
    case shade_request_result_t::shade_moving: return proto_error_t::busy;
    case shade_request_result_t::no_tilt: return proto_error_t::badRequest;
    default: return proto_error_t::none;
  }
}
const char *protoRequestErrorDesc(const shade_request_t &req, shade_request_result_t result) {
  switch(result) {
    case shade_request_result_t::not_found: return shadeRequestIsForGroup(req.type) ? "No group with that id" : "No shade with that id";
    case shade_request_result_t::radio_refused: return PROTO_RADIO_REFUSED;
    case shade_request_result_t::shade_moving: return "The shade is moving";
    case shade_request_result_t::no_tilt: return "The shade has no tilt";
    default: return nullptr;
  }
}
