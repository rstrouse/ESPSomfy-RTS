#ifndef PROTO_SETTINGS_H
#define PROTO_SETTINGS_H
#include <stdint.h>
#include <ArduinoJson.h>
#include "ProtoSchema.h"

// Socket protocol 2's settings ops (docs/PROTOCOL.md): the sections that
// getSettings and saveSettings name, and what a save's payload becomes.  A
// save is the settings route's request: its payload, in full names, is what
// loop() hands to the settings' own fromJSON(), so the rules stay in one
// place.  Kept free of the settings and the web server so it can be unit
// tested on the PC (pio test -e native).

enum class settings_section_t : uint8_t {
  general,   // hostname, SSDP and the clock
  network,   // the connection, WiFi, Ethernet and the IP address
  mqtt,
  security,
  radio
};
// The settings each part of a section belongs to, which loop() saves only
// when a field of it was sent.
enum class settings_part_t : uint8_t {
  general,   // ConfigSettings: hostname, ssdpBroadcast
  ntp,       // NTPSettings
  conn,      // ConfigSettings: connType
  wifi,      // WifiSettings
  ethernet,  // EthernetSettings
  ip,        // IPSettings
  mqtt,      // MQTTSettings
  security,  // SecuritySettings
  radio      // transceiver_config_t
};
const char *settingsSectionName(settings_section_t section);
// The section that payload d names in section.  nullptr when it is good;
// otherwise what is wrong with it, for a bad request reply.
const char *parseSettingsSection(JsonObjectConst d, settings_section_t &section);
// saveSettings: the section d names, and in doc its fields under the names
// the route reads.  Each is checked against its kind and its range or length,
// so nothing is cut short or wraps.  An address may be "" to clear it.  As
// with every op, only what was sent is in it; a field the section doesn't
// save, such as hasPassword, is refused.  Without hasEthernet the Ethernet
// fields are refused too.  nullptr when it is good; otherwise what is wrong.
const char *expandSettingsPayload(JsonObjectConst d, bool hasEthernet, settings_section_t &section, JsonDocument &doc);
// The parts that a save's request (from expandSettingsPayload) sets, as bits
// 1 << settings_part_t.  A route name such as hostname means another setting
// in another section, hence the section.
uint16_t settingsParts(settings_section_t section, JsonObjectConst request);
inline bool settingsSets(uint16_t parts, settings_part_t part) { return parts & (1u << static_cast<uint8_t>(part)); }
#endif
