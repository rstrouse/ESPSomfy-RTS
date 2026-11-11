#include "MqttDisco.h"

bool isDiscoSwitch(shade_types type) { return type == shade_types::drycontact || type == shade_types::drycontact2; }
void shadeDiscoTopic(const char *discoTopic, uint8_t shadeId, bool isSwitch, char *topic, size_t len) {
  snprintf(topic, len, "%s/%s/%d/config", discoTopic, isSwitch ? "switch" : "cover", shadeId);
}
static const char *deviceClass(shade_types type) {
  switch(type) {
    case shade_types::blind:
      return "blind";
    case shade_types::lgate:
    case shade_types::cgate:
    case shade_types::rgate:
    case shade_types::lgate1:
    case shade_types::cgate1:
    case shade_types::rgate1:
    case shade_types::ldrapery:
    case shade_types::rdrapery:
    case shade_types::cdrapery:
      return "curtain";
    case shade_types::garage1:
    case shade_types::garage3:
      return "garage";
    case shade_types::awning:
      return "awning";
    case shade_types::shutter:
      return "shutter";
    default:
      return "shade";
  }
}
void shadeDisco(const shade_disco_t &shade, JsonObject obj, char *topic, size_t len) {
  snprintf(topic, len, "%s/shades/%d", shade.rootTopic, shade.shadeId);
  obj["~"] = topic;
  JsonObject dobj = obj["device"].to<JsonObject>();
  dobj["hw_version"] = shade.fwVersion;
  dobj["name"] = shade.hostname;
  dobj["mf"] = "cjkas";
  JsonArray arrids = dobj["identifiers"].to<JsonArray>();
  snprintf(topic, len, "mqtt_espsomfyrts_%s", shade.serverId);
  arrids.add(topic);
  dobj["model"] = "ESPSomfy-RTS MQTT";
  snprintf(topic, len, "%s/status", shade.rootTopic);
  obj["availability_topic"] = topic;
  obj["payload_available"] = "online";
  obj["payload_not_available"] = "offline";
  obj["name"] = shade.name;
  snprintf(topic, len, "mqtt_%s_shade%d", shade.serverId, shade.shadeId);
  obj["unique_id"] = topic;
  const bool isSwitch = isDiscoSwitch(shade.shadeType);
  if(!isSwitch) {
    obj["device_class"] = deviceClass(shade.shadeType);
    // Positions count down from open: 0 is open and 100 closed, so an
    // upward move (-1) opens.  An awning, which opens downward, and a
    // flipped position turn that around.
    const bool reversed = shade.flipPosition != (shade.shadeType == shade_types::awning);
    obj["payload_close"] = reversed ? "-1" : "1";
    obj["payload_open"] = reversed ? "1" : "-1";
    obj["position_open"] = reversed ? 100 : 0;
    obj["position_closed"] = reversed ? 0 : 100;
    obj["state_closing"] = reversed ? "-1" : "1";
    obj["state_opening"] = reversed ? "1" : "-1";
    if(shade.tiltType != tilt_types::tiltonly) {
      obj["command_topic"] = "~/direction/set";
      obj["position_topic"] = "~/position";
      obj["set_position_topic"] = "~/target/set";
      obj["state_topic"] = "~/direction";
      obj["payload_stop"] = "0";
      obj["state_stopped"] = "0";
    }
    else {
      obj["payload_close"] = nullptr;
      obj["payload_open"] = nullptr;
      obj["payload_stop"] = nullptr;
    }
    if(shade.tiltType != tilt_types::none) {
      obj["tilt_command_topic"] = "~/tiltTarget/set";
      obj["tilt_status_topic"] = "~/tiltPosition";
    }
  }
  else {
    obj["payload_on"] = 100;
    obj["payload_off"] = 0;
    obj["state_off"] = 0;
    obj["state_on"] = 100;
    obj["state_topic"] = "~/position";
    obj["command_topic"] = "~/target/set";
  }
  obj["enabled_by_default"] = true;
  shadeDiscoTopic(shade.discoTopic, shade.shadeId, isSwitch, topic, len);
}
