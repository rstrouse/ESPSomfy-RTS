#ifndef PROTO_SCHEMA_H
#define PROTO_SCHEMA_H
#include <stdint.h>
#include <type_traits>
#include "JsonFormatter.h"

// Socket protocol 2 (docs/PROTOCOL.md, docs/adr/0005-one-websocket-protocol.md):
// the one table of message types, fields, error codes and feeds.  The device
// sends it to clients as the schema, so it is the only copy anywhere.  Writers
// name fields by proto_field_t, so a field that isn't declared here can't be
// sent.  Kept free of the shade model and the web server so it can be unit
// tested on the PC (pio test -e native).

#define PROTO_VERSION 2

// Message types: 1-15 control, 16-127 client ops, 128-255 device events.  A
// number is never reused; a retired type stays reserved.
// TYPE(name, number, sender, op): sender is 'c' client, 'd' device, 'b' both;
// op is what becomes of a client op (proto_op_t), '-' for the rest.
#define PROTO_TYPES(TYPE) \
  TYPE(hello, 1, 'd', '-') \
  TYPE(reply, 2, 'd', '-') \
  TYPE(next, 3, 'c', '-') \
  TYPE(sync, 4, 'b', '-') \
  TYPE(syncEnd, 5, 'd', '-') \
  TYPE(subscribe, 6, 'c', '-') \
  TYPE(unsubscribe, 7, 'c', '-') \
  TYPE(shadeCommand, 16, 'c', 'q') \
  TYPE(groupCommand, 17, 'c', 'q') \
  TYPE(tiltCommand, 18, 'c', 'q') \
  TYPE(repeatCommand, 19, 'c', 'q') \
  TYPE(setPositions, 20, 'c', 'q') \
  TYPE(setSensor, 21, 'c', 'q') \
  TYPE(setMyPosition, 22, 'c', 'q') \
  TYPE(unpairShade, 23, 'c', 'q') \
  TYPE(setRollingCode, 24, 'c', 'q') \
  TYPE(sendRemoteCommand, 25, 'c', 'f') \
  TYPE(addRoom, 26, 'c', 'e') \
  TYPE(addShade, 27, 'c', 'e') \
  TYPE(addGroup, 28, 'c', 'e') \
  TYPE(saveRoom, 29, 'c', 'e') \
  TYPE(saveShade, 30, 'c', 'e') \
  TYPE(saveGroup, 31, 'c', 'e') \
  TYPE(deleteRoom, 32, 'c', 'e') \
  TYPE(deleteShade, 33, 'c', 'e') \
  TYPE(deleteGroup, 34, 'c', 'e') \
  TYPE(linkRemote, 35, 'c', 'e') \
  TYPE(unlinkRemote, 36, 'c', 'e') \
  TYPE(linkToGroup, 37, 'c', 'e') \
  TYPE(unlinkFromGroup, 38, 'c', 'e') \
  TYPE(linkRepeater, 39, 'c', 'e') \
  TYPE(unlinkRepeater, 40, 'c', 'e') \
  TYPE(setPaired, 41, 'c', 'e') \
  TYPE(roomSortOrder, 42, 'c', 'e') \
  TYPE(shadeSortOrder, 43, 'c', 'e') \
  TYPE(groupSortOrder, 44, 'c', 'e') \
  TYPE(getNextRoom, 45, 'c', 'r') \
  TYPE(getNextShade, 46, 'c', 'r') \
  TYPE(getNextGroup, 47, 'c', 'r') \
  TYPE(groupOptions, 48, 'c', 'r') \
  TYPE(getSettings, 49, 'c', 'd') \
  TYPE(saveSettings, 50, 'c', 'd') \
  TYPE(scanWifi, 51, 'c', 'd') \
  TYPE(beginFrequencyScan, 52, 'c', 'd') \
  TYPE(endFrequencyScan, 53, 'c', 'd') \
  TYPE(reboot, 54, 'c', 'd') \
  TYPE(shadeState, 128, 'd', '-') \
  TYPE(shade, 129, 'd', '-') \
  TYPE(shadeRemoved, 130, 'd', '-') \
  TYPE(group, 131, 'd', '-') \
  TYPE(groupState, 132, 'd', '-') \
  TYPE(groupRemoved, 133, 'd', '-') \
  TYPE(room, 134, 'd', '-') \
  TYPE(roomRemoved, 135, 'd', '-') \
  TYPE(roomOrder, 136, 'd', '-') \
  TYPE(shadeOrder, 137, 'd', '-') \
  TYPE(groupOrder, 138, 'd', '-') \
  TYPE(shadeCommanded, 139, 'd', '-') \
  TYPE(memory, 140, 'd', '-') \
  TYPE(wifi, 141, 'd', '-') \
  TYPE(ethernet, 142, 'd', '-') \
  TYPE(frame, 143, 'd', '-') \
  TYPE(frequencyScan, 144, 'd', '-') \
  TYPE(repeaters, 145, 'd', '-') \
  TYPE(settingsChanged, 146, 'd', '-') \
  TYPE(wifiNetworks, 147, 'd', '-')

// Fields, in one dictionary for every type.  FIELD(name, key, kind, default):
// kind is b bool (sent as 1/0), i integer, n number with a fraction, s string,
// l list, o object.  A field at its default is left out; a string's default is
// "" and a list's is empty.  hello's and the schema's own keys (fw, sh, T, F,
// E, Q) are fixed, as a client needs them before it has the schema.
#define PROTO_FIELDS(FIELD) \
  /* the schema and the sync */ \
  FIELD(types, "T", 'l', 0) \
  FIELD(fields, "F", 'l', 0) \
  FIELD(errors, "E", 'l', 0) \
  FIELD(feeds, "Q", 'l', 0) \
  FIELD(header, "h", 'o', 0) \
  FIELD(rooms, "R", 'l', 0) \
  FIELD(shades, "S", 'l', 0) \
  FIELD(groups, "G", 'l', 0) \
  FIELD(repeaters, "X", 'l', 0) \
  FIELD(status, "Z", 'o', 0) \
  FIELD(fw, "fw", 's', 0) \
  FIELD(schemaHash, "sh", 's', 0) \
  /* replies and control */ \
  FIELD(ok, "k", 'b', 0) \
  FIELD(code, "e", 'i', 0) \
  FIELD(desc, "x", 's', 0) \
  FIELD(feed, "fe", 'i', 0) \
  FIELD(ids, "is", 'l', 0) \
  /* rooms, shades and groups */ \
  FIELD(shadeId, "s", 'i', 0) \
  FIELD(groupId, "g", 'i', 0) \
  FIELD(roomId, "r", 'i', 0) \
  FIELD(name, "n", 's', 0) \
  FIELD(remoteAddress, "a", 'i', 0) \
  FIELD(lastRollingCode, "c", 'i', 0) \
  FIELD(sortOrder, "o", 'i', 0) \
  FIELD(flags, "f", 'i', 0) \
  FIELD(position, "p", 'i', 0) \
  FIELD(target, "t", 'i', 0) \
  FIELD(direction, "d", 'i', 0) \
  FIELD(myPos, "m", 'i', -1) \
  FIELD(tiltType, "y", 'i', 0) \
  FIELD(tiltPosition, "q", 'i', 0) \
  FIELD(tiltTarget, "u", 'i', 0) \
  FIELD(tiltDirection, "w", 'i', 0) \
  FIELD(myTiltPos, "M", 'i', -1) \
  FIELD(upTime, "U", 'i', 0) \
  FIELD(downTime, "D", 'i', 0) \
  FIELD(tiltTime, "Y", 'i', 0) \
  FIELD(stepSize, "z", 'i', 100) \
  FIELD(paired, "i", 'b', 0) \
  FIELD(shadeType, "st", 'i', 0) \
  FIELD(bitLength, "bl", 'i', 56) \
  FIELD(proto, "pr", 'i', 0) \
  FIELD(flipCommands, "fc", 'b', 0) \
  FIELD(flipPosition, "fp", 'b', 0) \
  FIELD(inGroup, "ig", 'b', 0) \
  FIELD(sunSensor, "ss", 'b', 0) \
  FIELD(light, "li", 'b', 0) \
  FIELD(simMy, "sm", 'b', 0) \
  FIELD(repeats, "rp", 'i', 1) \
  FIELD(gpioUp, "gu", 'i', 0) \
  FIELD(gpioDown, "gd", 'i', 0) \
  FIELD(gpioMy, "gm", 'i', 0) \
  FIELD(gpioLLTrigger, "gl", 'b', 0) \
  FIELD(linkedRemotes, "lk", 'l', 0) \
  FIELD(linkedShades, "ls", 'l', 0) \
  /* the command ops and shadeCommanded */ \
  FIELD(command, "cm", 's', 0) \
  FIELD(source, "so", 's', 0) \
  FIELD(sourceAddress, "sr", 'i', 0) \
  FIELD(repeat, "re", 'i', 0) \
  FIELD(sunny, "sn", 'b', 0) \
  FIELD(windy, "wn", 'b', 0) \
  /* the edit ops */ \
  FIELD(validation, "vc", 'i', 0) \
  /* the header */ \
  FIELD(maxRooms, "xr", 'i', 0) \
  FIELD(maxShades, "xs", 'i', 0) \
  FIELD(maxGroups, "xg", 'i', 0) \
  FIELD(maxGroupedShades, "xm", 'i', 0) \
  FIELD(maxLinkedRemotes, "xl", 'i', 0) \
  FIELD(startingAddress, "sa", 'i', 0) \
  FIELD(serverId, "sv", 's', 0) \
  FIELD(hostname, "hn", 's', 0) \
  FIELD(model, "md", 's', 0) \
  FIELD(chipModel, "cp", 's', 0) \
  FIELD(hasOtaSlot, "os", 'b', 1) \
  FIELD(connType, "ct", 's', 0) \
  /* memory, wifi and ethernet */ \
  FIELD(freeHeap, "fr", 'i', 0) \
  FIELD(maxHeap, "mx", 'i', 0) \
  FIELD(minHeap, "mn", 'i', 0) \
  FIELD(totalHeap, "to", 'i', 0) \
  FIELD(uptime, "up", 'i', 0) \
  FIELD(loopRate, "lp", 'i', 0) \
  FIELD(loopMaxUs, "lx", 'i', 0) \
  FIELD(ssid, "sd", 's', 0) \
  FIELD(strength, "sg", 'i', 0) \
  FIELD(channel, "ch", 'i', 0) \
  FIELD(connected, "co", 'b', 0) \
  FIELD(speed, "sp", 'i', 0) \
  FIELD(fullDuplex, "fd", 'b', 0) \
  /* the frame and frequency scan feeds */ \
  FIELD(encKey, "ek", 'i', 0) \
  FIELD(rawCmd, "rw", 'i', 0) \
  FIELD(rssi, "rs", 'i', 0) \
  FIELD(bits, "bt", 'i', 0) \
  FIELD(valid, "va", 'b', 0) \
  FIELD(hwsync, "hs", 'i', 0) \
  FIELD(pulses, "pu", 'l', 0) \
  FIELD(scanning, "sc", 'b', 0) \
  FIELD(testFreq, "tf", 'n', 0) \
  FIELD(testRssi, "tr", 'i', 0) \
  FIELD(frequency, "fq", 'n', 0) \
  /* the settings ops: which section, and what a save did */ \
  FIELD(section, "se", 's', 0) \
  FIELD(apiKey, "ak", 's', 0) \
  FIELD(rebooting, "rb", 'b', 0) \
  /* general */ \
  FIELD(ssdpBroadcast, "sb", 'b', 0) \
  FIELD(ntpServer, "ns", 's', 0) \
  FIELD(posixZone, "pz", 's', 0) \
  /* network: the connection the settings ask for (connType is the one in \
     use), WiFi, Ethernet and the IP address */ \
  FIELD(connMode, "cn", 'i', 0) \
  FIELD(hasEthernet, "he", 'b', 0) \
  FIELD(passphrase, "pp", 's', 0) \
  FIELD(hasPassphrase, "hq", 'b', 0) \
  FIELD(roaming, "ro", 'b', 0) \
  FIELD(hidden, "hd", 'b', 0) \
  FIELD(boardType, "bo", 'i', 0) \
  FIELD(phyType, "ph", 'i', 0) \
  FIELD(CLKMode, "ck", 'i', 0) \
  FIELD(phyAddress, "pa", 'i', 0) \
  FIELD(PWRPin, "pw", 'i', 0) \
  FIELD(MDCPin, "mc", 'i', 0) \
  FIELD(MDIOPin, "mi", 'i', 0) \
  FIELD(dhcp, "dh", 'b', 0) \
  FIELD(ip, "ip", 's', 0) \
  FIELD(gateway, "gw", 's', 0) \
  FIELD(subnet, "su", 's', 0) \
  FIELD(dns1, "d1", 's', 0) \
  FIELD(dns2, "d2", 's', 0) \
  /* MQTT and security; hostname is the broker's in MQTT */ \
  FIELD(enabled, "en", 'b', 0) \
  FIELD(pubDisco, "pd", 'b', 0) \
  FIELD(protocol, "pt", 's', 0) \
  FIELD(port, "po", 'i', 0) \
  FIELD(username, "un", 's', 0) \
  FIELD(password, "ps", 's', 0) \
  FIELD(hasPassword, "hw", 'b', 0) \
  FIELD(rootTopic, "rt", 's', 0) \
  FIELD(discoTopic, "dt", 's', 0) \
  FIELD(securityType, "sy", 'i', 0) \
  FIELD(pin, "pn", 's', 0) \
  FIELD(hasPin, "hi", 'b', 0) \
  /* the radio: bitLength, proto, frequency and enabled as above */ \
  FIELD(TXPin, "tx", 'i', 0) \
  FIELD(RXPin, "rx", 'i', 0) \
  FIELD(SCKPin, "sk", 'i', 0) \
  FIELD(MOSIPin, "mo", 'i', 0) \
  FIELD(MISOPin, "ms", 'i', 0) \
  FIELD(CSNPin, "cs", 'i', 0) \
  FIELD(rxBandwidth, "bw", 'n', 0) \
  FIELD(deviation, "dv", 'n', 0) \
  FIELD(txPower, "tp", 'i', 0) \
  FIELD(noiseDetection, "nd", 'b', 0) \
  FIELD(radioInit, "ri", 'b', 0) \
  /* wifiNetworks: each network has ssid, channel and strength as above */ \
  FIELD(networks, "nw", 'l', 0) \
  FIELD(macAddress, "ma", 's', 0)

// Error codes in a reply.  ERR(name, number)
#define PROTO_ERRORS(ERR) \
  ERR(badRequest, 1) \
  ERR(unknownOp, 2) \
  ERR(unauthorized, 3) \
  ERR(notFound, 4) \
  ERR(busy, 5) \
  ERR(radioUnavailable, 6) \
  ERR(internal, 7)

// Feeds a client can subscribe to.  FEED(name, number)
#define PROTO_FEEDS(FEED) \
  FEED(frames, 1) \
  FEED(frequencyScan, 2)

#define PROTO_ENUM_ENTRY(name, number, ...) name = number,
enum class proto_type_t : uint8_t { PROTO_TYPES(PROTO_ENUM_ENTRY) };
enum class proto_error_t : uint8_t { none = 0, PROTO_ERRORS(PROTO_ENUM_ENTRY) };
enum class proto_feed_t : uint8_t { PROTO_FEEDS(PROTO_ENUM_ENTRY) };
#undef PROTO_ENUM_ENTRY
#define PROTO_FIELD_ENTRY(name, ...) name,
enum class proto_field_t : uint8_t { PROTO_FIELDS(PROTO_FIELD_ENTRY) count };
#undef PROTO_FIELD_ENTRY

struct proto_field_info_t {
  const char *name;
  const char *key;
  char kind;
  int32_t def;
};
struct proto_named_t {
  uint8_t number;
  const char *name;
  char sender;  // types only
  char op;      // types only
};
// What becomes of a client op, from the TYPE table.
enum class proto_op_t : char {
  none = '-',          // not an op
  shadeRequest = 'q',  // a command: a shade request loop() runs
  remoteFrame = 'f',   // sendRemoteCommand: a frame that goes to the radio at once
  edit = 'e',          // an edit loop() applies
  read = 'r',          // a read loop() answers from the shade model
  device = 'd'         // a settings, scan or reboot op loop() runs
};
const proto_field_info_t &protoField(proto_field_t field);
const proto_field_info_t *protoFieldByKey(const char *key);
// The field with that key; false when there is none.
bool protoFieldOfKey(const char *key, proto_field_t &out);
// The tables as the schema lists them, in declaration order.
uint8_t protoTypeCount();
const proto_named_t &protoTypeAt(uint8_t index);
uint8_t protoErrorCount();
const proto_named_t &protoErrorAt(uint8_t index);
uint8_t protoFeedCount();
const proto_named_t &protoFeedAt(uint8_t index);
const char *protoTypeName(uint8_t number);
// What a client may send: control messages it sends and the ops.
bool protoClientMaySend(uint8_t type);
proto_op_t protoOpKind(uint8_t type);
// Hex FNV-1a of every row of the schema: 8 characters and a terminator.
const char *protoSchemaHash();

// Writes fields by their short keys into the open object of json: booleans as
// 1/0, and nothing at all for a value at the field's default.
class ProtoWriter {
  public:
    explicit ProtoWriter(JsonFormatter &json) : json(json) {}
    template<typename T> void add(proto_field_t field, T value) {
      const proto_field_info_t &f = protoField(field);
      if constexpr(std::is_same<T, bool>::value) {
        if((value ? 1 : 0) == f.def) return;
        this->json.addElem(f.key, (uint8_t)(value ? 1 : 0));
      }
      else if constexpr(std::is_floating_point<T>::value) {
        if(value == (T)f.def) return;
        this->json.addElem(f.key, (float)value);
      }
      else if constexpr(std::is_integral<T>::value) {
        this->addInt(f, (int64_t)value);
      }
      else {
        const char *s = value;
        if(!s || !s[0]) return;
        this->json.addElem(f.key, s);
      }
    }
    void beginList(proto_field_t field) { this->json.beginArray(protoField(field).key); }
    void endList() { this->json.endArray(); }
    void beginObject(proto_field_t field) { this->json.beginObject(protoField(field).key); }
    void endObject() { this->json.endObject(); }
    // An object or a value inside a list.
    void beginItem() { this->json.beginObject(); }
    void endItem() { this->json.endObject(); }
    void item(int64_t value);
    JsonFormatter &json;
  private:
    void addInt(const proto_field_info_t &f, int64_t value);
};
#endif
