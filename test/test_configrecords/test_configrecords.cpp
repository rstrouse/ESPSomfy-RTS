#include <unity.h>
#include <fstream>
#include <sstream>
#include <string>
#include "ConfigRecords.h"

void setUp() {}
void tearDown() {}

// A file in memory.
class TextWriter : public ConfigWriter {
  public:
    std::string text;
    bool writeBytes(const uint8_t *buf, size_t len) override { text.append(reinterpret_cast<const char *>(buf), len); return true; }
};
class TextReader : public ConfigReader {
  public:
    std::string text;
    size_t pos = 0;
    explicit TextReader(const std::string &t) : text(t) {}
    bool readByte(uint8_t &val) override {
      if(pos >= text.size()) return false;
      val = static_cast<uint8_t>(text[pos++]);
      return true;
    }
    uint32_t position() override { return static_cast<uint32_t>(pos); }
};

static config_header_t header(uint8_t version = SHADE_HDR_VER) {
  config_header_t h;
  h.version = version;
  return h;
}
static shade_record_t aShade() {
  shade_record_t rec;
  rec.shadeId = 7;
  rec.paired = true;
  rec.shadeType = shade_types::blind;
  rec.remoteAddress = 0x123456;
  strcpy(rec.name, "Kitchen");
  rec.tiltType = tilt_types::tiltmotor;
  rec.proto = radio_proto::GP_Remote;
  rec.bitLength = 80;
  rec.upTime = 21000;
  rec.downTime = 19500;
  rec.tiltTime = 3200;
  rec.stepSize = 250;
  for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) rec.linkedRemotes[j] = 0x200000 + j;
  rec.lastRollingCode = 65000;
  rec.flags = 0x21;
  rec.myPos = 42.5f;
  rec.myTiltPos = 12.25f;
  rec.currentPos = 66.0f;
  rec.currentTiltPos = 33.5f;
  rec.flipCommands = true;
  rec.flipPosition = true;
  rec.repeats = 4;
  rec.sortOrder = 3;
  rec.gpioUp = 25;
  rec.gpioDown = 26;
  rec.gpioMy = 27;
  rec.gpioFlags = 2;
  rec.roomId = 5;
  return rec;
}
static group_record_t aGroup() {
  group_record_t rec;
  rec.groupId = 3;
  rec.remoteAddress = 0x654321;
  strcpy(rec.name, "Upstairs");
  rec.proto = radio_proto::RTS;
  rec.bitLength = 80;
  rec.linkedShades[0] = 1;
  rec.linkedShades[1] = 7;
  rec.linkedShades[2] = 32;
  rec.repeats = 2;
  rec.sortOrder = 9;
  rec.flipCommands = true;
  rec.roomId = 4;
  rec.lastRollingCode = 1234;
  return rec;
}
static net_record_t aNet() {
  net_record_t rec;
  rec.connType = 2;
  rec.dhcp = false;
  strcpy(rec.ip, "192.168.100.200");
  strcpy(rec.gateway, "192.168.100.1");
  strcpy(rec.subnet, "255.255.255.0");
  strcpy(rec.dns1, "1.1.1.1");
  strcpy(rec.dns2, "8.8.8.8");
  strcpy(rec.mqttProtocol, "mqtts://");
  strcpy(rec.mqttHostname, "broker.local");
  rec.mqttPort = 8883;
  rec.mqttPubDisco = true;
  strcpy(rec.mqttRootTopic, "somfy");
  strcpy(rec.mqttDiscoTopic, "ha");
  rec.ethBoardType = 3;
  rec.ethPhyType = 1;
  rec.ethClkMode = 3;
  rec.ethPhyAddress = -1;
  rec.ethPWRPin = 12;
  rec.ethMDCPin = 23;
  rec.ethMDIOPin = 18;
  return rec;
}
static trans_record_t aTrans() {
  trans_record_t rec;
  rec.enabled = true;
  rec.type = 80;
  rec.SCKPin = 4;
  rec.CSNPin = 7;
  rec.MOSIPin = 6;
  rec.MISOPin = 5;
  rec.TXPin = 10;
  rec.RXPin = 3;
  rec.frequency = 433.25f;
  rec.rxBandwidth = 812.5f;
  rec.deviation = 1.58f;
  rec.txPower = -30;
  rec.noiseDetection = true;
  return rec;
}
static void assertShadesEqual(const shade_record_t &want, const shade_record_t &got) {
  TEST_ASSERT_EQUAL_UINT8(want.shadeId, got.shadeId);
  TEST_ASSERT_EQUAL(want.paired, got.paired);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(want.shadeType), static_cast<uint8_t>(got.shadeType));
  TEST_ASSERT_EQUAL_HEX32(want.remoteAddress, got.remoteAddress);
  TEST_ASSERT_EQUAL_STRING(want.name, got.name);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(want.tiltType), static_cast<uint8_t>(got.tiltType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(want.proto), static_cast<uint8_t>(got.proto));
  TEST_ASSERT_EQUAL_UINT8(want.bitLength, got.bitLength);
  TEST_ASSERT_EQUAL_UINT32(want.upTime, got.upTime);
  TEST_ASSERT_EQUAL_UINT32(want.downTime, got.downTime);
  TEST_ASSERT_EQUAL_UINT32(want.tiltTime, got.tiltTime);
  TEST_ASSERT_EQUAL_UINT16(want.stepSize, got.stepSize);
  TEST_ASSERT_EQUAL_HEX32_ARRAY(want.linkedRemotes, got.linkedRemotes, SOMFY_MAX_LINKED_REMOTES);
  TEST_ASSERT_EQUAL_UINT16(want.lastRollingCode, got.lastRollingCode);
  TEST_ASSERT_EQUAL_HEX8(want.flags, got.flags);
  TEST_ASSERT_EQUAL_FLOAT(want.myPos, got.myPos);
  TEST_ASSERT_EQUAL_FLOAT(want.myTiltPos, got.myTiltPos);
  TEST_ASSERT_EQUAL_FLOAT(want.currentPos, got.currentPos);
  TEST_ASSERT_EQUAL_FLOAT(want.currentTiltPos, got.currentTiltPos);
  TEST_ASSERT_EQUAL(want.flipCommands, got.flipCommands);
  TEST_ASSERT_EQUAL(want.flipPosition, got.flipPosition);
  TEST_ASSERT_EQUAL_UINT8(want.repeats, got.repeats);
  TEST_ASSERT_EQUAL_INT8(want.sortOrder, got.sortOrder);
  TEST_ASSERT_EQUAL_UINT8(want.gpioUp, got.gpioUp);
  TEST_ASSERT_EQUAL_UINT8(want.gpioDown, got.gpioDown);
  TEST_ASSERT_EQUAL_UINT8(want.gpioMy, got.gpioMy);
  TEST_ASSERT_EQUAL_UINT8(want.gpioFlags, got.gpioFlags);
  TEST_ASSERT_EQUAL_UINT8(want.roomId, got.roomId);
}

// Every record is the size the header says, or validate() refuses the file.
static void test_each_record_is_the_size_the_header_declares() {
  config_header_t h = header();
  h.length = SHADE_HDR_SIZE;
  strcpy(h.serverId, "a1b2c3d4");
  TextWriter w;
  writeConfigHeader(w, h);
  TEST_ASSERT_EQUAL(SHADE_HDR_SIZE, w.text.size());

  room_record_t room;
  room.roomId = 16;
  strcpy(room.name, "12345678901234567890");
  w.text.clear();
  writeRoomRecord(w, room);
  TEST_ASSERT_EQUAL(ROOM_REC_SIZE, w.text.size());

  w.text.clear();
  writeShadeRecord(w, aShade());
  TEST_ASSERT_EQUAL(SHADE_REC_SIZE, w.text.size());

  w.text.clear();
  writeGroupRecord(w, aGroup());
  TEST_ASSERT_EQUAL(GROUP_REC_SIZE, w.text.size());

  uint32_t repeaters[SOMFY_MAX_REPEATERS] = {0xFFFFFF, 1};
  w.text.clear();
  writeRepeaterRecord(w, repeaters);
  TEST_ASSERT_EQUAL(REPEATER_REC_SIZE, w.text.size());

  w.text.clear();
  writeTransRecord(w, aTrans());
  TEST_ASSERT_EQUAL(TRANS_REC_SIZE, w.text.size());
}
// backup() measures the records that vary in length with a ConfigCounter.
static void test_a_counter_measures_what_a_writer_writes() {
  TextWriter w;
  ConfigCounter c;
  writeNetRecord(w, aNet(), false);
  writeNetRecord(c, aNet(), false);
  TEST_ASSERT_EQUAL(w.text.size(), c.bytes);
}
static void test_a_deleted_shade_is_written_cleared() {
  shade_record_t rec = aShade();
  rec.shadeId = 255;
  TextWriter w;
  writeShadeRecord(w, rec);
  TEST_ASSERT_EQUAL(SHADE_REC_SIZE, w.text.size());
  TextReader r(w.text);
  shade_record_t got;
  readShadeRecord(r, header(), got);
  TEST_ASSERT_EQUAL_FLOAT(-1, got.myPos);
  TEST_ASSERT_EQUAL_FLOAT(0, got.currentPos);
  TEST_ASSERT_EQUAL_HEX8(0, got.flags);
}

static void test_the_header_reads_back() {
  config_header_t h = header();
  h.length = SHADE_HDR_SIZE;
  h.roomRecordSize = ROOM_REC_SIZE;
  h.roomRecords = 3;
  h.shadeRecordSize = SHADE_REC_SIZE;
  h.shadeRecords = 32;
  h.groupRecordSize = GROUP_REC_SIZE;
  h.groupRecords = 16;
  h.repeaterRecordSize = REPEATER_REC_SIZE;
  h.repeaterRecords = 1;
  h.settingsRecordSize = 94;
  h.netRecordSize = 166;
  h.transRecordSize = TRANS_REC_SIZE;
  strcpy(h.serverId, "a1b2c3d4");
  TextWriter w;
  writeConfigHeader(w, h);
  TextReader r(w.text);
  config_header_t got;
  readConfigHeader(r, got);
  TEST_ASSERT_EQUAL_UINT8(24, got.version);
  TEST_ASSERT_EQUAL_INT8(SHADE_HDR_SIZE, got.length);
  TEST_ASSERT_EQUAL_UINT8(3, got.roomRecords);
  TEST_ASSERT_EQUAL_UINT8(32, got.shadeRecords);
  TEST_ASSERT_EQUAL_UINT8(16, got.groupRecords);
  TEST_ASSERT_EQUAL_UINT8(REPEATER_REC_SIZE, got.repeaterRecordSize);
  TEST_ASSERT_EQUAL_UINT16(94, got.settingsRecordSize);
  TEST_ASSERT_EQUAL_UINT16(166, got.netRecordSize);
  TEST_ASSERT_EQUAL_UINT16(TRANS_REC_SIZE, got.transRecordSize);
  TEST_ASSERT_EQUAL_STRING("a1b2c3d4", got.serverId);
  TEST_ASSERT_EQUAL(SHADE_HDR_SIZE, r.position());
}
static void test_a_version_12_header_has_no_rooms_repeaters_or_settings() {
  // version, length, shade size and count (one byte each then), group size and count.
  TextReader r("12, 20,200,  2,150,  1\n");
  config_header_t got;
  readConfigHeader(r, got);
  TEST_ASSERT_EQUAL_UINT8(12, got.version);
  TEST_ASSERT_EQUAL_UINT16(200, got.shadeRecordSize);
  TEST_ASSERT_EQUAL_UINT8(2, got.shadeRecords);
  TEST_ASSERT_EQUAL_UINT16(150, got.groupRecordSize);
  TEST_ASSERT_EQUAL_UINT8(1, got.groupRecords);
  TEST_ASSERT_EQUAL_UINT8(0, got.roomRecords);
  TEST_ASSERT_EQUAL_UINT8(0, got.repeaterRecords);
  TEST_ASSERT_EQUAL_UINT16(0, got.settingsRecordSize);
}

static void test_a_shade_reads_back() {
  TextWriter w;
  writeShadeRecord(w, aShade());
  TextReader r(w.text);
  shade_record_t got;
  readShadeRecord(r, header(), got);
  assertShadesEqual(aShade(), got);
  TEST_ASSERT_EQUAL(w.text.size(), r.position());
}
static void test_a_room_reads_back() {
  room_record_t room;
  room.roomId = 2;
  strcpy(room.name, "Living room");
  room.sortOrder = 5;
  TextWriter w;
  writeRoomRecord(w, room);
  TextReader r(w.text);
  room_record_t got;
  readRoomRecord(r, got);
  TEST_ASSERT_EQUAL_UINT8(2, got.roomId);
  TEST_ASSERT_EQUAL_STRING("Living room", got.name);
  TEST_ASSERT_EQUAL_INT8(5, got.sortOrder);
}
static void test_a_group_reads_back() {
  TextWriter w;
  writeGroupRecord(w, aGroup());
  TextReader r(w.text);
  group_record_t got;
  readGroupRecord(r, header(), got);
  const group_record_t want = aGroup();
  TEST_ASSERT_EQUAL_UINT8(want.groupId, got.groupId);
  TEST_ASSERT_EQUAL_HEX32(want.remoteAddress, got.remoteAddress);
  TEST_ASSERT_EQUAL_STRING(want.name, got.name);
  TEST_ASSERT_EQUAL_UINT8(want.bitLength, got.bitLength);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want.linkedShades, got.linkedShades, SOMFY_MAX_GROUPED_SHADES);
  TEST_ASSERT_EQUAL_UINT8(want.repeats, got.repeats);
  TEST_ASSERT_EQUAL_INT8(want.sortOrder, got.sortOrder);
  TEST_ASSERT_TRUE(got.flipCommands);
  TEST_ASSERT_EQUAL_UINT8(want.roomId, got.roomId);
  TEST_ASSERT_EQUAL_UINT16(want.lastRollingCode, got.lastRollingCode);
}
static void test_a_group_reads_its_shades_without_gaps() {
  group_record_t rec = aGroup();
  memset(rec.linkedShades, 0, sizeof(rec.linkedShades));
  rec.linkedShades[2] = 4;
  rec.linkedShades[10] = 9;
  TextWriter w;
  writeGroupRecord(w, rec);
  TextReader r(w.text);
  group_record_t got;
  readGroupRecord(r, header(), got);
  TEST_ASSERT_EQUAL_UINT8(4, got.linkedShades[0]);
  TEST_ASSERT_EQUAL_UINT8(9, got.linkedShades[1]);
  TEST_ASSERT_EQUAL_UINT8(0, got.linkedShades[2]);
}
static void test_the_settings_read_back() {
  settings_record_t rec;
  strcpy(rec.fwVersion, "v4.0.0");
  strcpy(rec.hostname, "somfy");
  strcpy(rec.ntpServer, "time.google.com");
  strcpy(rec.posixZone, "CET-1CEST,M3.5.0,M10.5.0/3");
  rec.ssdpBroadcast = true;
  rec.checkForUpdate = false;
  TextWriter w;
  writeSettingsRecord(w, rec);
  TextReader r(w.text);
  settings_record_t got;
  readSettingsRecord(r, header(), got);
  TEST_ASSERT_EQUAL_STRING("v4.0.0", got.fwVersion);
  TEST_ASSERT_EQUAL_STRING("somfy", got.hostname);
  TEST_ASSERT_EQUAL_STRING("time.google.com", got.ntpServer);
  // The commas are inside the quotes.
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", got.posixZone);
  TEST_ASSERT_TRUE(got.ssdpBroadcast);
  TEST_ASSERT_FALSE(got.checkForUpdate);
}
static void test_the_network_reads_back_with_ethernet() {
  TextWriter w;
  writeNetRecord(w, aNet(), true);
  TextReader r(w.text);
  net_record_t got;
  readNetRecord(r, header(), got, true);
  TEST_ASSERT_EQUAL_UINT8(2, got.connType);
  TEST_ASSERT_FALSE(got.dhcp);
  TEST_ASSERT_EQUAL_STRING("192.168.100.200", got.ip);
  TEST_ASSERT_EQUAL_STRING("192.168.100.1", got.gateway);
  TEST_ASSERT_EQUAL_STRING("255.255.255.0", got.subnet);
  TEST_ASSERT_EQUAL_STRING("1.1.1.1", got.dns1);
  TEST_ASSERT_EQUAL_STRING("8.8.8.8", got.dns2);
  TEST_ASSERT_EQUAL_STRING("mqtts://", got.mqttProtocol);
  TEST_ASSERT_EQUAL_STRING("broker.local", got.mqttHostname);
  TEST_ASSERT_EQUAL_UINT16(8883, got.mqttPort);
  TEST_ASSERT_TRUE(got.mqttPubDisco);
  TEST_ASSERT_EQUAL_STRING("somfy", got.mqttRootTopic);
  TEST_ASSERT_EQUAL_STRING("ha", got.mqttDiscoTopic);
  TEST_ASSERT_EQUAL_UINT8(3, got.ethBoardType);
  TEST_ASSERT_EQUAL_UINT8(1, got.ethPhyType);
  TEST_ASSERT_EQUAL_UINT8(3, got.ethClkMode);
  TEST_ASSERT_EQUAL_INT8(-1, got.ethPhyAddress);
  TEST_ASSERT_EQUAL_INT8(12, got.ethPWRPin);
  TEST_ASSERT_EQUAL_INT8(23, got.ethMDCPin);
  TEST_ASSERT_EQUAL_INT8(18, got.ethMDIOPin);
  TEST_ASSERT_EQUAL(w.text.size(), r.position());
}
static void test_the_radio_reads_back() {
  TextWriter w;
  writeTransRecord(w, aTrans());
  TextReader r(w.text);
  trans_record_t got;
  readTransRecord(r, got);
  TEST_ASSERT_TRUE(got.enabled);
  TEST_ASSERT_EQUAL_UINT8(80, got.type);
  TEST_ASSERT_EQUAL_UINT8(4, got.SCKPin);
  TEST_ASSERT_EQUAL_UINT8(7, got.CSNPin);
  TEST_ASSERT_EQUAL_UINT8(6, got.MOSIPin);
  TEST_ASSERT_EQUAL_UINT8(5, got.MISOPin);
  TEST_ASSERT_EQUAL_UINT8(10, got.TXPin);
  TEST_ASSERT_EQUAL_UINT8(3, got.RXPin);
  TEST_ASSERT_EQUAL_FLOAT(433.25f, got.frequency);
  TEST_ASSERT_EQUAL_FLOAT(812.5f, got.rxBandwidth);
  TEST_ASSERT_EQUAL_FLOAT(1.58f, got.deviation);
  TEST_ASSERT_EQUAL_INT8(-30, got.txPower);
  TEST_ASSERT_TRUE(got.noiseDetection);
}

// The web UI takes any name, and the record would shift by a value at the
// comma.
static void test_a_name_with_a_comma_keeps_the_record_in_line() {
  shade_record_t rec = aShade();
  strcpy(rec.name, "Left, by the door");
  TextWriter w;
  writeShadeRecord(w, rec);
  TEST_ASSERT_EQUAL(SHADE_REC_SIZE, w.text.size());
  TextReader r(w.text);
  shade_record_t got;
  readShadeRecord(r, header(), got);
  assertShadesEqual(rec, got);
}
static void test_the_longest_name_reads_back() {
  room_record_t room;
  room.roomId = 1;
  strcpy(room.name, "12345678901234567890");
  room.sortOrder = 7;
  TextWriter w;
  writeRoomRecord(w, room);
  TextReader r(w.text);
  room_record_t got;
  readRoomRecord(r, got);
  TEST_ASSERT_EQUAL_STRING("12345678901234567890", got.name);
  TEST_ASSERT_EQUAL_INT8(7, got.sortOrder);
}
// A quoted value as long as its field, with its quotes and comma, is longer
// than the field.  Reading stopped at the field's size and left the rest to
// the next value.
static void test_the_longest_settings_read_back() {
  settings_record_t rec;
  strcpy(rec.fwVersion, "v4.0.0");
  memset(rec.hostname, 'h', sizeof(rec.hostname) - 1);
  memset(rec.ntpServer, 'n', sizeof(rec.ntpServer) - 1);
  memset(rec.posixZone, 'z', sizeof(rec.posixZone) - 1);
  rec.ssdpBroadcast = true;
  rec.checkForUpdate = false;
  TextWriter w;
  writeSettingsRecord(w, rec);
  TextReader r(w.text);
  settings_record_t got;
  readSettingsRecord(r, header(), got);
  TEST_ASSERT_EQUAL_STRING(rec.hostname, got.hostname);
  TEST_ASSERT_EQUAL_STRING(rec.ntpServer, got.ntpServer);
  TEST_ASSERT_EQUAL_STRING(rec.posixZone, got.posixZone);
  TEST_ASSERT_TRUE(got.ssdpBroadcast);
  TEST_ASSERT_FALSE(got.checkForUpdate);
}
static void test_the_longest_mqtt_settings_read_back() {
  net_record_t rec = aNet();
  memset(rec.mqttHostname, 'm', sizeof(rec.mqttHostname) - 1);
  memset(rec.mqttRootTopic, 'r', sizeof(rec.mqttRootTopic) - 1);
  TextWriter w;
  writeNetRecord(w, rec, false);
  TextReader r(w.text);
  net_record_t got;
  readNetRecord(r, header(), got, false);
  TEST_ASSERT_EQUAL_STRING(rec.mqttHostname, got.mqttHostname);
  TEST_ASSERT_EQUAL_UINT16(8883, got.mqttPort);
  TEST_ASSERT_EQUAL_STRING(rec.mqttRootTopic, got.mqttRootTopic);
  TEST_ASSERT_EQUAL_STRING("ha", got.mqttDiscoTopic);
}
// A value too long for its field is cut, and the next value still reads.
static void test_a_value_too_long_is_cut() {
  TextReader r("\"0123456789012345678901234567890123456789\",\"next\"\n");
  char ip[CFG_IP_LEN];
  char next[CFG_IP_LEN];
  r.beginRecord();
  r.readVarString(ip, sizeof(ip));
  r.readVarString(next, sizeof(next));
  TEST_ASSERT_EQUAL_STRING("01234567890123456789012", ip);
  TEST_ASSERT_EQUAL_STRING("next", next);
}

// The layout of a restore past the shades: the network record of a board
// without Ethernet, then the radio record.
static std::string backupTail(const net_record_t &net, const trans_record_t &trans) {
  TextWriter w;
  writeNetRecord(w, net, false);
  writeTransRecord(w, trans);
  return w.text;
}
// 4.0 boards without Ethernet declared the network record 28 bytes longer
// than they wrote, and a restore that skipped it by its size read the radio
// from the middle of its record.
static void test_skipping_the_network_of_a_board_without_ethernet_finds_the_radio() {
  TextReader r(backupTail(aNet(), aTrans()));
  skipRecords(r, 1);
  trans_record_t got;
  readTransRecord(r, got);
  TEST_ASSERT_EQUAL_UINT8(4, got.SCKPin);
  TEST_ASSERT_EQUAL_UINT8(3, got.RXPin);
  TEST_ASSERT_EQUAL_FLOAT(433.25f, got.frequency);
  TEST_ASSERT_TRUE(got.noiseDetection);
}
// The board that wrote a backup reads its Ethernet settings back; a board
// with Ethernet restoring a backup from one without finds none.
static void test_a_network_record_without_ethernet_keeps_the_ethernet_defaults() {
  TextReader r(backupTail(aNet(), aTrans()));
  net_record_t got;
  readNetRecord(r, header(), got, true);
  TEST_ASSERT_EQUAL_UINT8(0, got.ethBoardType);
  TEST_ASSERT_EQUAL_INT8(1, got.ethPhyAddress);
  TEST_ASSERT_EQUAL_INT8(23, got.ethMDIOPin);
  trans_record_t trans;
  readTransRecord(r, trans);
  TEST_ASSERT_EQUAL_UINT8(4, trans.SCKPin);
}
// Before 4.0 the radio record was declared 74 bytes and is 80.
static void test_the_radio_reads_whole_whatever_its_declared_size() {
  TextWriter w;
  writeTransRecord(w, aTrans());
  TEST_ASSERT_NOT_EQUAL(74, w.text.size());
  TextReader r(w.text);
  trans_record_t got;
  readTransRecord(r, got);
  TEST_ASSERT_TRUE(got.noiseDetection);
  TEST_ASSERT_EQUAL(w.text.size(), r.position());
}
// A record from a newer version has values this one doesn't know.
static void test_values_a_newer_version_added_are_skipped() {
  TextWriter w;
  writeRoomRecord(w, room_record_t{1, "Hall", 0});
  w.text.insert(w.text.size() - 1, ",  9,  9");
  writeRoomRecord(w, room_record_t{2, "Attic", 1});
  TextReader r(w.text);
  room_record_t got;
  readRoomRecord(r, got);
  readRoomRecord(r, got);
  TEST_ASSERT_EQUAL_UINT8(2, got.roomId);
  TEST_ASSERT_EQUAL_STRING("Attic", got.name);
}
// A record shorter than its version says reads defaults for the rest and
// leaves the next record alone.
static void test_a_short_record_never_reads_the_next() {
  TextWriter w;
  writeRoomRecord(w, room_record_t{1, "Hall", 0});
  // Ends after the name.
  std::string text = w.text.substr(0, 4 + CFG_NAME_LEN - 1) + CFG_REC_END;
  w.text.clear();
  writeRoomRecord(w, room_record_t{2, "Attic", 1});
  TextReader r(text + w.text);
  room_record_t got;
  readRoomRecord(r, got);
  TEST_ASSERT_EQUAL_UINT8(1, got.roomId);
  TEST_ASSERT_EQUAL_STRING("Hall", got.name);
  TEST_ASSERT_EQUAL_INT8(0, got.sortOrder);
  readRoomRecord(r, got);
  TEST_ASSERT_EQUAL_UINT8(2, got.roomId);
  TEST_ASSERT_EQUAL_INT8(1, got.sortOrder);
}
static void test_skipping_records_passes_their_newlines() {
  TextWriter w;
  writeShadeRecord(w, aShade());
  writeGroupRecord(w, aGroup());
  writeRoomRecord(w, room_record_t{4, "Last", 2});
  TextReader r(w.text);
  TEST_ASSERT_TRUE(skipRecords(r, 2));
  room_record_t got;
  readRoomRecord(r, got);
  TEST_ASSERT_EQUAL_STRING("Last", got.name);
  TEST_ASSERT_FALSE(skipRecords(r, 1));
}

// Older versions, written field by field in the layout they had.
static void writeShadeStart(TextWriter &w, uint8_t id) {
  w.writeUInt8(id);
  w.writeBool(true);
  w.writeUInt8(static_cast<uint8_t>(shade_types::roller));
  w.writeUInt32(0x111111);
  w.writeString("Old", CFG_NAME_LEN);
}
static std::string version4Shade() {
  TextWriter w;
  writeShadeStart(w, 2);
  w.writeUInt8(0);        // tiltType
  w.writeUInt8(56);       // bitLength
  w.writeUInt32(15000);   // upTime
  w.writeUInt32(14000);   // downTime
  w.writeUInt32(0);       // tiltTime
  for(uint8_t j = 0; j < 5; j++) w.writeUInt32(j == 0 ? 0x222222 : 0);
  w.writeUInt16(300);     // lastRollingCode
  w.writeFloat(50, 5);    // myPos
  w.writeFloat(-1, 5);    // myTiltPos
  w.writeFloat(25, 5);    // currentPos
  w.writeFloat(0, 5, CFG_REC_END);
  return w.text;
}
static void test_a_version_4_shade() {
  const std::string text = version4Shade();
  TextReader r(text);
  shade_record_t got;
  readShadeRecord(r, header(4), got);
  TEST_ASSERT_EQUAL_UINT8(2, got.shadeId);
  TEST_ASSERT_TRUE(got.paired);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(shade_types::roller), static_cast<uint8_t>(got.shadeType));
  TEST_ASSERT_EQUAL_HEX32(0x111111, got.remoteAddress);
  TEST_ASSERT_EQUAL_STRING("Old", got.name);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tilt_types::none), static_cast<uint8_t>(got.tiltType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(radio_proto::RTS), static_cast<uint8_t>(got.proto));
  TEST_ASSERT_EQUAL_UINT8(56, got.bitLength);
  TEST_ASSERT_EQUAL_UINT32(15000, got.upTime);
  TEST_ASSERT_EQUAL_UINT32(14000, got.downTime);
  TEST_ASSERT_EQUAL_UINT32(0, got.tiltTime);
  TEST_ASSERT_EQUAL_UINT16(100, got.stepSize);
  TEST_ASSERT_EQUAL_HEX32(0x222222, got.linkedRemotes[0]);
  TEST_ASSERT_EQUAL_HEX32(0, got.linkedRemotes[4]);
  TEST_ASSERT_EQUAL_HEX32(0, got.linkedRemotes[5]);
  TEST_ASSERT_EQUAL_UINT16(300, got.lastRollingCode);
  TEST_ASSERT_EQUAL_HEX8(0, got.flags);
  TEST_ASSERT_EQUAL_FLOAT(50, got.myPos);
  TEST_ASSERT_EQUAL_FLOAT(-1, got.myTiltPos);
  TEST_ASSERT_EQUAL_FLOAT(25, got.currentPos);
  TEST_ASSERT_EQUAL_FLOAT(0, got.currentTiltPos);
  TEST_ASSERT_FALSE(got.flipCommands);
  TEST_ASSERT_FALSE(got.flipPosition);
  TEST_ASSERT_EQUAL_UINT8(1, got.repeats);
  TEST_ASSERT_EQUAL_INT8(1, got.sortOrder);
  TEST_ASSERT_EQUAL_UINT8(0, got.gpioUp);
  TEST_ASSERT_EQUAL_UINT8(0, got.roomId);
  TEST_ASSERT_EQUAL(text.size(), r.position());
}
// A restore reads into a record that starts as the shade it replaces, and
// what an older version lacks stays as it was.
static void test_what_an_older_version_lacks_stays_as_it_was() {
  const std::string text = version4Shade();
  TextReader r(text);
  shade_record_t got = aShade();
  readShadeRecord(r, header(4), got);
  TEST_ASSERT_EQUAL_UINT32(15000, got.upTime);
  TEST_ASSERT_EQUAL_HEX32(0x222222, got.linkedRemotes[0]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(radio_proto::GP_Remote), static_cast<uint8_t>(got.proto));
  TEST_ASSERT_EQUAL_UINT16(250, got.stepSize);
  TEST_ASSERT_EQUAL_HEX32(0x200005, got.linkedRemotes[5]);
  TEST_ASSERT_EQUAL_HEX8(0x21, got.flags);
  TEST_ASSERT_TRUE(got.flipCommands);
  TEST_ASSERT_TRUE(got.flipPosition);
  TEST_ASSERT_EQUAL_UINT8(4, got.repeats);
  TEST_ASSERT_EQUAL_UINT8(25, got.gpioUp);
  TEST_ASSERT_EQUAL_UINT8(27, got.gpioMy);
  TEST_ASSERT_EQUAL_UINT8(2, got.gpioFlags);
  TEST_ASSERT_EQUAL_UINT8(5, got.roomId);
}
// Version 2 saved positions as fractions and the favorite as a byte.
static void test_a_version_2_shade_has_fractions() {
  TextWriter w;
  writeShadeStart(w, 1);
  w.writeBool(false);     // tilt
  w.writeUInt8(56);
  w.writeUInt32(10000);
  w.writeUInt32(10000);
  w.writeUInt32(0);
  for(uint8_t j = 0; j < 5; j++) w.writeUInt32(0);
  w.writeUInt16(12);
  w.writeUInt8(40);       // myPos
  w.writeFloat(0.5f, 5);  // currentPos
  w.writeFloat(0, 5, CFG_REC_END);
  TextReader r(w.text);
  shade_record_t got;
  readShadeRecord(r, header(2), got);
  TEST_ASSERT_EQUAL_FLOAT(40, got.myPos);
  TEST_ASSERT_EQUAL_FLOAT(50, got.currentPos);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tilt_types::none), static_cast<uint8_t>(got.tiltType));
  TEST_ASSERT_EQUAL(w.text.size(), r.position());
}
// Version 13 had the sort order and no GPIOs or room.
static void test_a_version_13_shade() {
  TextWriter w;
  writeShadeStart(w, 5);
  w.writeUInt8(0);
  w.writeUInt8(static_cast<uint8_t>(radio_proto::RTS));
  w.writeUInt8(80);
  w.writeUInt32(10000);
  w.writeUInt32(10000);
  w.writeUInt32(0);
  w.writeUInt16(200);
  for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) w.writeUInt32(0);
  w.writeUInt16(77);
  w.writeUInt8(0x01);     // flags
  w.writeFloat(-1, 5);
  w.writeFloat(-1, 5);
  w.writeFloat(100, 5);
  w.writeFloat(0, 5);
  w.writeBool(true);      // flipCommands
  w.writeBool(false);     // flipPosition
  w.writeUInt8(3);        // repeats
  w.writeUInt8(8, CFG_REC_END);
  TextReader r(w.text);
  shade_record_t got;
  readShadeRecord(r, header(13), got);
  TEST_ASSERT_EQUAL_UINT8(80, got.bitLength);
  TEST_ASSERT_EQUAL_UINT16(200, got.stepSize);
  TEST_ASSERT_EQUAL_UINT16(77, got.lastRollingCode);
  TEST_ASSERT_EQUAL_HEX8(0x01, got.flags);
  TEST_ASSERT_EQUAL_FLOAT(100, got.currentPos);
  TEST_ASSERT_TRUE(got.flipCommands);
  TEST_ASSERT_EQUAL_UINT8(3, got.repeats);
  TEST_ASSERT_EQUAL_INT8(8, got.sortOrder);
  TEST_ASSERT_EQUAL_UINT8(0, got.gpioUp);
  TEST_ASSERT_EQUAL_UINT8(0, got.roomId);
  TEST_ASSERT_EQUAL(w.text.size(), r.position());
}
static void writeGroupStart(TextWriter &w) {
  w.writeUInt8(2);
  w.writeUInt8(0);
  w.writeUInt32(0x333333);
  w.writeString("Group", CFG_NAME_LEN);
  w.writeUInt8(0);
  w.writeUInt8(56);
}
// Version 23 put the group's rolling code before its shades.
static void test_a_version_23_group() {
  TextWriter w;
  writeGroupStart(w);
  w.writeUInt16(4321);
  for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) w.writeUInt8(j < 2 ? j + 1 : 0);
  w.writeUInt8(1);
  w.writeUInt8(1);
  w.writeBool(false);
  w.writeUInt8(6, CFG_REC_END);
  TextReader r(w.text);
  group_record_t got;
  readGroupRecord(r, header(23), got);
  TEST_ASSERT_EQUAL_UINT16(4321, got.lastRollingCode);
  TEST_ASSERT_TRUE(got.hasRollingCode);
  TEST_ASSERT_EQUAL_UINT8(2, got.linkedShades[1]);
  TEST_ASSERT_EQUAL_UINT8(6, got.roomId);
  TEST_ASSERT_EQUAL(w.text.size(), r.position());
}
static void test_a_version_11_group() {
  TextWriter w;
  writeGroupStart(w);
  for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) w.writeUInt8(j == 0 ? 4 : 0, j == SOMFY_MAX_GROUPED_SHADES - 1 ? CFG_REC_END : CFG_VALUE_SEP);
  TextReader r(w.text);
  group_record_t got;
  readGroupRecord(r, header(11), got);
  TEST_ASSERT_EQUAL_UINT8(4, got.linkedShades[0]);
  TEST_ASSERT_EQUAL_UINT8(1, got.repeats);
  TEST_ASSERT_EQUAL_INT8(1, got.sortOrder);
  TEST_ASSERT_FALSE(got.flipCommands);
  TEST_ASSERT_EQUAL_UINT16(0, got.lastRollingCode);
}
// Before version 23 a group had no rolling code, so a restore leaves the one
// NVS holds.
static void test_a_group_before_version_23_has_no_rolling_code() {
  TextWriter w;
  writeGroupStart(w);
  for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) w.writeUInt8(0, j == SOMFY_MAX_GROUPED_SHADES - 1 ? CFG_REC_END : CFG_VALUE_SEP);
  TextReader r(w.text);
  group_record_t got = aGroup();
  readGroupRecord(r, header(11), got);
  TEST_ASSERT_FALSE(got.hasRollingCode);
  TEST_ASSERT_EQUAL_UINT16(1234, got.lastRollingCode);
  TEST_ASSERT_TRUE(got.flipCommands);
  TEST_ASSERT_EQUAL_UINT8(4, got.roomId);
  TextWriter v24;
  writeGroupRecord(v24, aGroup());
  TextReader r24(v24.text);
  readGroupRecord(r24, header(), got);
  TEST_ASSERT_TRUE(got.hasRollingCode);
}
// Before version 20 the settings had no update check, which stays as it was.
static void test_version_19_settings_leave_the_update_check() {
  TextReader r("\"v2.0.0\",\"host\",\"pool.ntp.org\",\"\",true \n");
  settings_record_t got;
  got.checkForUpdate = false;
  readSettingsRecord(r, header(19), got);
  TEST_ASSERT_EQUAL_STRING("host", got.hostname);
  TEST_ASSERT_TRUE(got.ssdpBroadcast);
  TEST_ASSERT_FALSE(got.checkForUpdate);
}
// Before version 22 the network record had no MQTT settings.
static void test_a_version_21_network_has_no_mqtt() {
  TextReader r("  1,true ,\"\",\"\",\"\",\"\",\"\",  1,  0,  0,   0,  -1,  23,  18\n  9\n");
  net_record_t got;
  readNetRecord(r, header(21), got, true);
  TEST_ASSERT_EQUAL_UINT8(1, got.connType);
  TEST_ASSERT_EQUAL_STRING("", got.mqttHostname);
  TEST_ASSERT_EQUAL_UINT16(1883, got.mqttPort);
  TEST_ASSERT_EQUAL_INT8(-1, got.ethPWRPin);
  r.beginRecord();
  TEST_ASSERT_EQUAL_UINT8(9, r.readUInt8(0));
}

// A real backup, which isn't in the repo: it holds the remotes' addresses.
// CONFIG_BACKUP=<path> pio test -e native -f test_configrecords
static void test_a_real_backup_reads_to_its_end() {
  const char *path = getenv("CONFIG_BACKUP");
  if(!path) TEST_IGNORE_MESSAGE("set CONFIG_BACKUP to a backup file");
  std::ifstream in(path, std::ios::binary);
  TEST_ASSERT_TRUE_MESSAGE(in.good(), "can't open CONFIG_BACKUP");
  std::stringstream ss;
  ss << in.rdbuf();
  TextReader r(ss.str());
  config_header_t h;
  readConfigHeader(r, h);
  TEST_ASSERT_EQUAL(static_cast<uint32_t>(h.length), r.position());
  for(uint8_t i = 0; i < h.roomRecords; i++) {
    room_record_t rec;
    readRoomRecord(r, rec);
    TEST_ASSERT_NOT_EQUAL(0, rec.roomId);
  }
  for(uint8_t i = 0; i < h.shadeRecords; i++) {
    shade_record_t rec;
    readShadeRecord(r, h, rec);
    TEST_ASSERT_TRUE(rec.shadeId >= 1 && rec.shadeId <= SOMFY_MAX_SHADES);
    TEST_ASSERT_TRUE(rec.currentPos >= 0 && rec.currentPos <= 100);
    // Written back, it is the record it was read from.
    TextWriter w;
    writeShadeRecord(w, rec);
    TEST_ASSERT_EQUAL(h.shadeRecordSize, w.text.size());
  }
  for(uint8_t i = 0; i < h.groupRecords; i++) {
    group_record_t rec;
    readGroupRecord(r, h, rec);
    TEST_ASSERT_TRUE(rec.groupId >= 1 && rec.groupId <= SOMFY_MAX_GROUPS);
  }
  for(uint8_t i = 0; i < h.repeaterRecords; i++) {
    uint32_t repeaters[SOMFY_MAX_REPEATERS];
    readRepeaterRecord(r, repeaters);
  }
  settings_record_t settings;
  readSettingsRecord(r, h, settings);
  TEST_ASSERT_EQUAL_UINT8('v', settings.fwVersion[0]);
  net_record_t net;
  readNetRecord(r, h, net, true);
  trans_record_t trans;
  readTransRecord(r, trans);
  TEST_ASSERT_TRUE(trans.frequency > 300 && trans.frequency < 950);
  TEST_ASSERT_EQUAL(ss.str().size(), r.position());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_each_record_is_the_size_the_header_declares);
  RUN_TEST(test_a_counter_measures_what_a_writer_writes);
  RUN_TEST(test_a_deleted_shade_is_written_cleared);
  RUN_TEST(test_the_header_reads_back);
  RUN_TEST(test_a_version_12_header_has_no_rooms_repeaters_or_settings);
  RUN_TEST(test_a_shade_reads_back);
  RUN_TEST(test_a_room_reads_back);
  RUN_TEST(test_a_group_reads_back);
  RUN_TEST(test_a_group_reads_its_shades_without_gaps);
  RUN_TEST(test_the_settings_read_back);
  RUN_TEST(test_the_network_reads_back_with_ethernet);
  RUN_TEST(test_the_radio_reads_back);
  RUN_TEST(test_a_name_with_a_comma_keeps_the_record_in_line);
  RUN_TEST(test_the_longest_name_reads_back);
  RUN_TEST(test_the_longest_settings_read_back);
  RUN_TEST(test_the_longest_mqtt_settings_read_back);
  RUN_TEST(test_a_value_too_long_is_cut);
  RUN_TEST(test_skipping_the_network_of_a_board_without_ethernet_finds_the_radio);
  RUN_TEST(test_a_network_record_without_ethernet_keeps_the_ethernet_defaults);
  RUN_TEST(test_the_radio_reads_whole_whatever_its_declared_size);
  RUN_TEST(test_values_a_newer_version_added_are_skipped);
  RUN_TEST(test_a_short_record_never_reads_the_next);
  RUN_TEST(test_skipping_records_passes_their_newlines);
  RUN_TEST(test_a_version_4_shade);
  RUN_TEST(test_what_an_older_version_lacks_stays_as_it_was);
  RUN_TEST(test_a_version_2_shade_has_fractions);
  RUN_TEST(test_a_version_13_shade);
  RUN_TEST(test_a_version_23_group);
  RUN_TEST(test_a_version_11_group);
  RUN_TEST(test_a_group_before_version_23_has_no_rolling_code);
  RUN_TEST(test_version_19_settings_leave_the_update_check);
  RUN_TEST(test_a_version_21_network_has_no_mqtt);
  RUN_TEST(test_a_real_backup_reads_to_its_end);
  return UNITY_END();
}
