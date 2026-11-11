#include <stdlib.h>
#include <string.h>
#include "MqttCommands.h"

const char *const MQTT_COMMAND_TOPICS[MQTT_COMMAND_TOPIC_COUNT] = {
  "shades/+/target/set",
  "shades/+/tiltTarget/set",
  "shades/+/direction/set",
  "shades/+/mypos/set",
  "shades/+/myTiltPos/set",
  "shades/+/sunFlag/set",
  "shades/+/sunny/set",
  "shades/+/windy/set",
  "shades/+/position/set",
  "shades/+/tiltPosition/set",
  "groups/+/direction/set",
  "groups/+/sunFlag/set",
  "groups/+/sunny/set",
  "groups/+/windy/set"
};

namespace {
struct segment_t {
  const char *s = "";
  size_t len = 0;
  bool is(const char *word) const { return this->len == strlen(word) && strncmp(this->s, word, this->len) == 0; }
};
bool inRange(int val) { return val >= 0 && val <= 100; }
somfy_commands directionCommand(int val) {
  if(val < 0) return somfy_commands::Up;
  if(val > 0) return somfy_commands::Down;
  return somfy_commands::My;
}
}

bool parseMqttCommand(const char *topic, const uint8_t *payload, size_t length, shade_request_t &req) {
  // Only the last four segments count, so the root topic may hold slashes or
  // be empty: type, id, command and "set".
  segment_t seg[4];
  size_t end = strlen(topic);
  for(int i = 3; i >= 0; i--) {
    size_t start = end;
    while(start > 0 && topic[start - 1] != '/') start--;
    seg[i].s = topic + start;
    seg[i].len = end - start;
    if(i > 0) {
      if(start == 0) return false;
      end = start - 1;
    }
  }
  if(!seg[3].is("set")) return false;
  if(seg[1].len == 0 || seg[1].len > 3) return false;
  int id = 0;
  for(size_t i = 0; i < seg[1].len; i++) {
    if(seg[1].s[i] < '0' || seg[1].s[i] > '9') return false;
    id = id * 10 + (seg[1].s[i] - '0');
  }
  if(id > 255) return false;
  char value[16];
  const size_t n = length < sizeof(value) - 1 ? length : sizeof(value) - 1;
  memcpy(value, payload, n);
  value[n] = '\0';
  const int val = atoi(value);
  const segment_t &cmd = seg[2];

  shade_request_t r;
  r.id = static_cast<uint8_t>(id);
  if(seg[0].is("shades")) {
    if(cmd.is("target")) {
      if(!inRange(val)) return false;
      r.type = shade_request_type_t::shade_command;
      r.target = static_cast<int8_t>(val);
    }
    else if(cmd.is("tiltTarget")) {
      if(!inRange(val)) return false;
      r.type = shade_request_type_t::tilt_command;
      r.target = static_cast<int8_t>(val);
    }
    else if(cmd.is("direction")) {
      r.type = shade_request_type_t::shade_command;
      r.command = directionCommand(val);
    }
    else if(cmd.is("mypos")) {
      if(!inRange(val)) return false;
      r.type = shade_request_type_t::set_favorite;
      r.position = static_cast<int8_t>(val);
    }
    else if(cmd.is("myTiltPos")) {
      if(!inRange(val)) return false;
      r.type = shade_request_type_t::set_favorite;
      r.tiltPosition = static_cast<int8_t>(val);
    }
    else if(cmd.is("sunFlag")) {
      r.type = shade_request_type_t::shade_command;
      r.command = sunFlagCommand(val > 0);
    }
    else if(cmd.is("sunny")) {
      if(val < 0) return false;
      r.type = shade_request_type_t::shade_sensor;
      r.sunny = val > 0 ? 1 : 0;
    }
    else if(cmd.is("windy")) {
      if(val < 0) return false;
      r.type = shade_request_type_t::shade_sensor;
      r.windy = val > 0 ? 1 : 0;
    }
    else if(cmd.is("position")) {
      if(!inRange(val)) return false;
      r.type = shade_request_type_t::set_positions;
      r.position = static_cast<int8_t>(val);
    }
    else if(cmd.is("tiltPosition")) {
      if(!inRange(val)) return false;
      r.type = shade_request_type_t::set_positions;
      r.tiltPosition = static_cast<int8_t>(val);
    }
    else return false;
  }
  else if(seg[0].is("groups")) {
    if(cmd.is("direction")) {
      r.type = shade_request_type_t::group_command;
      r.command = directionCommand(val);
    }
    else if(cmd.is("sunFlag")) {
      r.type = shade_request_type_t::group_command;
      r.command = sunFlagCommand(val > 0);
    }
    else if(cmd.is("sunny")) {
      if(val < 0) return false;
      r.type = shade_request_type_t::group_sensor;
      r.sunny = val > 0 ? 1 : 0;
    }
    else if(cmd.is("windy")) {
      if(val < 0) return false;
      r.type = shade_request_type_t::group_sensor;
      r.windy = val > 0 ? 1 : 0;
    }
    else return false;
  }
  else return false;
  req = r;
  return true;
}
