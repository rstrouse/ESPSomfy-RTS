#include "ShadeChanges.h"

static bool flag(const shade_snapshot_t &shade, somfy_flags_t f) { return (shade.flags & static_cast<uint8_t>(f)) != 0; }

void publishShadeTopics(uint16_t changed, const shade_snapshot_t &shade, topic_sink_t sink, void *ctx) {
  // Where the shade is and is going are kept by the broker so a client that
  // connects later gets them; the rest are passing news.
  if(changed & SHADE_CHANGED_POSITION) sink("position", shade.position, true, ctx);
  if(changed & SHADE_CHANGED_DIRECTION) sink("direction", shade.direction, true, ctx);
  if(changed & SHADE_CHANGED_TARGET) sink("target", shade.target, true, ctx);
  if(changed & SHADE_CHANGED_ROLLING_CODE) sink("lastRollingCode", shade.lastRollingCode, false, ctx);
  if(changed & SHADE_CHANGED_MY_POS) sink("mypos", shade.myPos, true, ctx);
  if(changed & SHADE_CHANGED_MY_TILT_POS) sink("myTiltPos", shade.myTiltPos, true, ctx);
  if(changed & SHADE_CHANGED_TILT_DIRECTION) sink("tiltDirection", shade.tiltDirection, true, ctx);
  if(changed & SHADE_CHANGED_TILT_POSITION) sink("tiltPosition", shade.tiltPosition, true, ctx);
  if(changed & SHADE_CHANGED_TILT_TARGET) sink("tiltTarget", shade.tiltTarget, true, ctx);
  if(changed & SHADE_CHANGED_SUN_FLAG) sink("sunFlag", flag(shade, somfy_flags_t::SunFlag), false, ctx);
  if(changed & SHADE_CHANGED_SUNNY) sink("sunny", flag(shade, somfy_flags_t::Sunny), false, ctx);
  if(changed & SHADE_CHANGED_WINDY) sink("windy", flag(shade, somfy_flags_t::Windy), false, ctx);
}
uint16_t shadeStateTopics(const shade_snapshot_t &shade) {
  uint16_t topics = SHADE_CHANGED_POSITION | SHADE_CHANGED_DIRECTION | SHADE_CHANGED_TARGET | SHADE_CHANGED_ROLLING_CODE |
    SHADE_CHANGED_MY_POS | SHADE_CHANGED_MY_TILT_POS | SHADE_CHANGED_WINDY;
  if(shade.tiltType != static_cast<uint8_t>(tilt_types::none))
    topics |= SHADE_CHANGED_TILT_DIRECTION | SHADE_CHANGED_TILT_POSITION | SHADE_CHANGED_TILT_TARGET;
  if(shade.sunSensor) topics |= SHADE_CHANGED_SUN_FLAG | SHADE_CHANGED_SUNNY;
  return topics;
}
