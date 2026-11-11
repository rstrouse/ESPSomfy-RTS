#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ProtoSettings.h"
#include "ProtoParse.h"
#include "EthPhy.h"

static const char *const SECTIONS[] = {"general", "network", "mqtt", "security", "radio"};
const char *settingsSectionName(settings_section_t section) { return SECTIONS[static_cast<uint8_t>(section)]; }

static settings_section_t sectionOf(settings_part_t part) {
  switch(part) {
    case settings_part_t::general:
    case settings_part_t::ntp: return settings_section_t::general;
    case settings_part_t::conn:
    case settings_part_t::wifi:
    case settings_part_t::ethernet:
    case settings_part_t::ip: return settings_section_t::network;
    case settings_part_t::mqtt: return settings_section_t::mqtt;
    case settings_part_t::security: return settings_section_t::security;
    default: return settings_section_t::radio;
  }
}

// What a save may set.  lo..hi is a whole number's range, a number's in
// hundredths, and hi a string's longest length: what the setting holds, so the
// route's fromJSON neither cuts it short nor wraps it.  The has* fields and
// radioInit are only ever sent.
struct settings_field_t {
  settings_part_t part;
  proto_field_t field;
  const char *route;  // the name the route reads; nullptr when it is the field's
  int32_t lo;
  int32_t hi;
};
static const settings_field_t FIELDS[] = {
  {settings_part_t::general, proto_field_t::hostname, nullptr, 0, 31},
  {settings_part_t::general, proto_field_t::ssdpBroadcast, nullptr, 0, 0},
  {settings_part_t::ntp, proto_field_t::ntpServer, nullptr, 0, 64},
  {settings_part_t::ntp, proto_field_t::posixZone, nullptr, 0, 63},
  // wifi, ethernet and ethernet with WiFi as the fallback (conn_types_t).
  {settings_part_t::conn, proto_field_t::connMode, "connType", 1, 3},
  {settings_part_t::wifi, proto_field_t::ssid, nullptr, 0, 64},
  {settings_part_t::wifi, proto_field_t::passphrase, nullptr, 0, 64},
  {settings_part_t::wifi, proto_field_t::roaming, nullptr, 0, 0},
  {settings_part_t::wifi, proto_field_t::hidden, nullptr, 0, 0},
  {settings_part_t::ethernet, proto_field_t::boardType, nullptr, 0, 255},
  {settings_part_t::ethernet, proto_field_t::phyType, nullptr, 0, 255},
  {settings_part_t::ethernet, proto_field_t::CLKMode, nullptr, 0, ETH_CLOCK_ID_MAX},
  {settings_part_t::ethernet, proto_field_t::phyAddress, nullptr, -1, 31},
  // Pins run to GPIO 54, the P4's last.
  {settings_part_t::ethernet, proto_field_t::PWRPin, nullptr, -1, 54},
  {settings_part_t::ethernet, proto_field_t::MDCPin, nullptr, 0, 54},
  {settings_part_t::ethernet, proto_field_t::MDIOPin, nullptr, 0, 54},
  {settings_part_t::ip, proto_field_t::dhcp, nullptr, 0, 0},
  {settings_part_t::ip, proto_field_t::ip, nullptr, 0, 15},
  {settings_part_t::ip, proto_field_t::gateway, nullptr, 0, 15},
  {settings_part_t::ip, proto_field_t::subnet, nullptr, 0, 15},
  {settings_part_t::ip, proto_field_t::dns1, nullptr, 0, 15},
  {settings_part_t::ip, proto_field_t::dns2, nullptr, 0, 15},
  {settings_part_t::mqtt, proto_field_t::enabled, nullptr, 0, 0},
  {settings_part_t::mqtt, proto_field_t::pubDisco, nullptr, 0, 0},
  {settings_part_t::mqtt, proto_field_t::protocol, nullptr, 0, 9},
  {settings_part_t::mqtt, proto_field_t::hostname, nullptr, 0, 64},
  {settings_part_t::mqtt, proto_field_t::port, nullptr, 1, 65535},
  {settings_part_t::mqtt, proto_field_t::username, nullptr, 0, 32},
  {settings_part_t::mqtt, proto_field_t::password, nullptr, 0, 32},
  {settings_part_t::mqtt, proto_field_t::rootTopic, nullptr, 0, 64},
  {settings_part_t::mqtt, proto_field_t::discoTopic, nullptr, 0, 64},
  // None, PIN or password (security_types).
  {settings_part_t::security, proto_field_t::securityType, "type", 0, 2},
  {settings_part_t::security, proto_field_t::username, nullptr, 0, 32},
  {settings_part_t::security, proto_field_t::password, nullptr, 0, 32},
  {settings_part_t::security, proto_field_t::pin, nullptr, 0, 4},
  // 56 or 80, checked on its own.
  {settings_part_t::radio, proto_field_t::bitLength, "type", 56, 80},
  // RTS, RTW, RTV, GP Relay or GP Remote (radio_proto), checked on its own.
  {settings_part_t::radio, proto_field_t::proto, nullptr, 0, 9},
  {settings_part_t::radio, proto_field_t::enabled, nullptr, 0, 0},
  {settings_part_t::radio, proto_field_t::TXPin, nullptr, 0, 54},
  {settings_part_t::radio, proto_field_t::RXPin, nullptr, 0, 54},
  {settings_part_t::radio, proto_field_t::SCKPin, nullptr, 0, 54},
  {settings_part_t::radio, proto_field_t::MOSIPin, nullptr, 0, 54},
  {settings_part_t::radio, proto_field_t::MISOPin, nullptr, 0, 54},
  {settings_part_t::radio, proto_field_t::CSNPin, nullptr, 0, 54},
  // What the CC1101 can be set to, in MHz and kHz.
  {settings_part_t::radio, proto_field_t::frequency, nullptr, 30000, 92800},
  {settings_part_t::radio, proto_field_t::rxBandwidth, nullptr, 5803, 81250},
  {settings_part_t::radio, proto_field_t::deviation, nullptr, 158, 38085},
  {settings_part_t::radio, proto_field_t::txPower, nullptr, -30, 12},
  {settings_part_t::radio, proto_field_t::noiseDetection, nullptr, 0, 0}
};
static const char *routeName(const settings_field_t &s) { return s.route ? s.route : protoField(s.field).name; }
static const settings_field_t *saved(settings_section_t section, proto_field_t field) {
  for(const settings_field_t &s : FIELDS)
    if(s.field == field && sectionOf(s.part) == section) return &s;
  return nullptr;
}

const char *parseSettingsSection(JsonObjectConst d, settings_section_t &section) {
  const char *name = protoString(d, proto_field_t::section);
  if(!name) return "section is missing";
  for(uint8_t i = 0; i < sizeof(SECTIONS) / sizeof(SECTIONS[0]); i++) {
    if(strcmp(SECTIONS[i], name) != 0) continue;
    section = static_cast<settings_section_t>(i);
    return nullptr;
  }
  return "Unknown section";
}

// Four numbers 0-255 between dots.
static bool isAddress(const char *s) {
  for(int part = 0; part < 4; part++) {
    if(part > 0 && *s++ != '.') return false;
    if(*s < '0' || *s > '9') return false;
    char *end;
    const long n = strtol(s, &end, 10);
    if(n > 255 || end - s > 3) return false;
    s = end;
  }
  return *s == '\0';
}

// What the reply's desc points at; every parse runs on async_tcp, one at a
// time.
static char why[64];
// A value already of its field's kind: nullptr when it is in range.
static const char *checkValue(const settings_field_t &s, const proto_field_info_t &f, JsonVariant value) {
  if(f.kind == 'i') {
    const bool inRange = value.is<int64_t>() && value.as<int64_t>() >= s.lo && value.as<int64_t>() <= s.hi;
    if(s.field == proto_field_t::bitLength) {
      if(inRange && (value.as<int>() == 56 || value.as<int>() == 80)) return nullptr;
      snprintf(why, sizeof(why), "%s must be 56 or 80", f.name);
      return why;
    }
    if(s.field == proto_field_t::proto) {
      if(inRange && (value.as<int>() <= 2 || value.as<int>() >= 8)) return nullptr;
      snprintf(why, sizeof(why), "%s must be 0, 1, 2, 8 or 9", f.name);
      return why;
    }
    if(inRange) return nullptr;
    if(s.lo == s.hi) snprintf(why, sizeof(why), "%s must be %ld", f.name, (long)s.lo);
    else snprintf(why, sizeof(why), "%s must be %ld-%ld", f.name, (long)s.lo, (long)s.hi);
    return why;
  }
  if(f.kind == 'n') {
    // Half a hundredth either way, as 58.03 is a little under it in a float.
    const double hundredths = value.as<double>() * 100;
    if(hundredths > s.lo - 0.5 && hundredths < s.hi + 0.5) return nullptr;
    snprintf(why, sizeof(why), "%s must be %ld.%02ld-%ld.%02ld", f.name, (long)(s.lo / 100), (long)(s.lo % 100),
      (long)(s.hi / 100), (long)(s.hi % 100));
    return why;
  }
  if(f.kind != 's') return nullptr;
  const char *text = value.as<const char *>();
  if(strlen(text) > static_cast<size_t>(s.hi)) {
    snprintf(why, sizeof(why), "%s is longer than %ld characters", f.name, (long)s.hi);
    return why;
  }
  if(s.part != settings_part_t::ip) return nullptr;
  // The settings send 0.0.0.0 as "", so "" sets it back.
  if(!text[0]) value.set("0.0.0.0");
  else if(!isAddress(text)) {
    snprintf(why, sizeof(why), "%s is not an IP address", f.name);
    return why;
  }
  return nullptr;
}

const char *expandSettingsPayload(JsonObjectConst d, bool hasEthernet, settings_section_t &section, JsonDocument &doc) {
  doc.clear();
  const char *bad = parseSettingsSection(d, section);
  if(bad) return bad;
  JsonObject obj = doc.to<JsonObject>();
  for(JsonPairConst kv : d) {
    proto_field_t field;
    if(!protoFieldOfKey(kv.key().c_str(), field)) {
      snprintf(why, sizeof(why), "Unknown field %s", kv.key().c_str());
      return why;
    }
    if(field == proto_field_t::section) continue;
    const proto_field_info_t &f = protoField(field);
    const settings_field_t *s = saved(section, field);
    if(!s) {
      snprintf(why, sizeof(why), "%s can't be saved in %s", f.name, settingsSectionName(section));
      return why;
    }
    if(s->part == settings_part_t::ethernet && !hasEthernet) return "This device has no Ethernet";
    JsonVariant value = obj[routeName(*s)].to<JsonVariant>();
    if(!protoRouteValue(f, kv.value(), value)) {
      snprintf(why, sizeof(why), "%s is not a %s", f.name, protoKindName(f.kind));
      return why;
    }
    bad = checkValue(*s, f, value);
    if(bad) return bad;
  }
  if(obj.size() == 0) return "Nothing to save";
  return doc.overflowed() ? "Too large" : nullptr;
}

uint16_t settingsParts(settings_section_t section, JsonObjectConst request) {
  uint16_t parts = 0;
  for(JsonPairConst kv : request)
    for(const settings_field_t &s : FIELDS)
      if(sectionOf(s.part) == section && strcmp(routeName(s), kv.key().c_str()) == 0)
        parts |= 1u << static_cast<uint8_t>(s.part);
  return parts;
}
