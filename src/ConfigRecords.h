#ifndef CONFIG_RECORDS_H
#define CONFIG_RECORDS_H
#include <Arduino.h>
#include "ConfigHeader.h"
#include "ShadeTypes.h"
#include "SomfyFrame.h"

// The records of shades.cfg and of a backup, and how they are written and
// read.  The file is text: each value is padded to a fixed width and followed
// by a comma, strings that vary in length are quoted, and each record ends
// with a newline.  Older versions of the file lack the values added since,
// and read as their defaults.  Kept free of the file system and the shade
// model so every version can be unit tested on the PC (pio test -e native).

#define CFG_VALUE_SEP ','
#define CFG_REC_END '\n'
#define CFG_TOK_NONE 0x00
#define CFG_TOK_QUOTE '"'

#define SHADE_HDR_VER 24
#define SHADE_HDR_SIZE 76
#define SHADE_REC_SIZE 276
#define GROUP_REC_SIZE 200
#define ROOM_REC_SIZE 29
#define REPEATER_REC_SIZE 77
// Files written before 4.0 declare the transceiver record 74 bytes: it was
// left there when noiseDetection was added.
#define TRANS_REC_SIZE 80

// The sizes of the strings, which are those of the fields they are read into.
#define CFG_NAME_LEN 21
#define CFG_VERSION_LEN 24
#define CFG_HOSTNAME_LEN 32
#define CFG_NTP_SERVER_LEN 65
#define CFG_POSIX_ZONE_LEN 64
#define CFG_IP_LEN 24
#define CFG_MQTT_PROTOCOL_LEN 10
#define CFG_MQTT_HOSTNAME_LEN 65
#define CFG_MQTT_TOPIC_LEN 65

// Reads the values of a file.  A record's values past its end read as their
// defaults, so a record shorter than expected never reads into the next one.
class ConfigReader {
  private:
    uint8_t _last = 0;
    bool next(uint8_t &val);
  public:
    virtual ~ConfigReader() {}
    // The next byte of the file; false at its end.
    virtual bool readByte(uint8_t &val) = 0;
    virtual uint32_t position() = 0;
    void beginRecord() { _last = 0; }
    // Moves past the rest of the record: the values a newer version added.
    void endRecord();
    bool atRecordEnd() const { return _last == CFG_REC_END; }
    bool seekChar(const char val);
    // A value up to the next separator, cut to fit buff.
    bool readString(char *buff, size_t len);
    // A value padded to len - 1 bytes, read whole so it may hold commas: a
    // name.
    bool readFixedString(char *buff, size_t len);
    // A quoted value of any length, cut to fit buff.
    bool readVarString(char *buff, size_t len);
    int8_t readInt8(const int8_t defVal = 0);
    uint8_t readUInt8(const uint8_t defVal = 0);
    uint16_t readUInt16(const uint16_t defVal = 0);
    uint32_t readUInt32(const uint32_t defVal = 0);
    bool readBool(const bool defVal = false);
    float readFloat(const float defVal = 0.00);
};
class ConfigWriter {
  public:
    virtual ~ConfigWriter() {}
    virtual bool writeBytes(const uint8_t *buf, size_t len) = 0;
    bool writeChar(const char val);
    bool writeString(const char *val, size_t len, const char tok = CFG_VALUE_SEP);
    bool writeVarString(const char *val, const char tok = CFG_VALUE_SEP);
    bool writeInt8(const int8_t val, const char tok = CFG_VALUE_SEP);
    bool writeUInt8(const uint8_t val, const char tok = CFG_VALUE_SEP);
    bool writeUInt16(const uint16_t val, const char tok = CFG_VALUE_SEP);
    bool writeUInt32(const uint32_t val, const char tok = CFG_VALUE_SEP);
    bool writeBool(const bool val, const char tok = CFG_VALUE_SEP);
    bool writeFloat(const float val, const uint8_t prec, const char tok = CFG_VALUE_SEP);
};
// Counts what would be written: the size of a record that varies in length.
class ConfigCounter : public ConfigWriter {
  public:
    uint32_t bytes = 0;
    bool writeBytes(const uint8_t *buf, size_t len) override { (void)buf; bytes += len; return true; }
};

struct room_record_t {
  uint8_t roomId = 0;
  char name[CFG_NAME_LEN] = "";
  int8_t sortOrder = 0;
};
struct shade_record_t {
  uint8_t shadeId = 255;
  bool paired = false;
  shade_types shadeType = shade_types::roller;
  uint32_t remoteAddress = 0;
  char name[CFG_NAME_LEN] = "";
  tilt_types tiltType = tilt_types::none;
  radio_proto proto = radio_proto::RTS;
  uint8_t bitLength = 56;
  uint32_t upTime = 10000;
  uint32_t downTime = 10000;
  uint32_t tiltTime = 7000;
  uint16_t stepSize = 100;
  uint32_t linkedRemotes[SOMFY_MAX_LINKED_REMOTES] = {};
  uint16_t lastRollingCode = 0;
  uint8_t flags = 0;
  float myPos = -1;
  float myTiltPos = -1;
  float currentPos = 0;
  float currentTiltPos = 0;
  bool flipCommands = false;
  bool flipPosition = false;
  uint8_t repeats = 1;
  int8_t sortOrder = 0;
  uint8_t gpioUp = 0;
  uint8_t gpioDown = 0;
  uint8_t gpioMy = 0;
  uint8_t gpioFlags = 0;
  uint8_t roomId = 0;
};
struct group_record_t {
  uint8_t groupId = 255;
  group_types groupType = group_types::channel;
  uint32_t remoteAddress = 0;
  char name[CFG_NAME_LEN] = "";
  radio_proto proto = radio_proto::RTS;
  uint8_t bitLength = 56;
  // Read without the gaps the file may have.
  uint8_t linkedShades[SOMFY_MAX_GROUPED_SHADES] = {};
  uint8_t repeats = 1;
  int8_t sortOrder = 0;
  bool flipCommands = false;
  uint8_t roomId = 0;
  uint16_t lastRollingCode = 0;
  // Versions before 23 have none.
  bool hasRollingCode = false;
};
struct settings_record_t {
  // The firmware that wrote the backup.  Not restored.
  char fwVersion[CFG_VERSION_LEN] = "";
  char hostname[CFG_HOSTNAME_LEN] = "";
  char ntpServer[CFG_NTP_SERVER_LEN] = "";
  char posixZone[CFG_POSIX_ZONE_LEN] = "";
  bool ssdpBroadcast = false;
  bool checkForUpdate = true;
};
struct net_record_t {
  uint8_t connType = 0;
  bool dhcp = true;
  char ip[CFG_IP_LEN] = "";
  char gateway[CFG_IP_LEN] = "";
  char subnet[CFG_IP_LEN] = "";
  char dns1[CFG_IP_LEN] = "";
  char dns2[CFG_IP_LEN] = "";
  char mqttProtocol[CFG_MQTT_PROTOCOL_LEN] = "";
  char mqttHostname[CFG_MQTT_HOSTNAME_LEN] = "";
  uint16_t mqttPort = 1883;
  bool mqttPubDisco = false;
  char mqttRootTopic[CFG_MQTT_TOPIC_LEN] = "";
  char mqttDiscoTopic[CFG_MQTT_TOPIC_LEN] = "";
  // Only boards with Ethernet write these, and only the board that wrote
  // them reads them back.
  uint8_t ethBoardType = 1;
  uint8_t ethPhyType = 0;
  uint8_t ethClkMode = 0;
  int8_t ethPhyAddress = 1;
  int8_t ethPWRPin = 1;
  int8_t ethMDCPin = 16;
  int8_t ethMDIOPin = 23;
};
struct trans_record_t {
  bool enabled = false;
  radio_proto proto = radio_proto::RTS;
  uint8_t type = 56;
  uint8_t SCKPin = 18;
  uint8_t CSNPin = 5;
  uint8_t MOSIPin = 23;
  uint8_t MISOPin = 19;
  uint8_t TXPin = 13;
  uint8_t RXPin = 35;
  float frequency = 433.42;
  float rxBandwidth = 99.97;
  float deviation = 47.60;
  int8_t txPower = 10;
  bool noiseDetection = false;
};

// The readers leave alone what the record's version lacks, so a record that
// starts as the shade, group or settings it is read into keeps their values.
bool writeConfigHeader(ConfigWriter &w, const config_header_t &header);
bool readConfigHeader(ConfigReader &r, config_header_t &header);
bool writeRoomRecord(ConfigWriter &w, const room_record_t &rec);
bool readRoomRecord(ConfigReader &r, room_record_t &rec);
bool writeShadeRecord(ConfigWriter &w, const shade_record_t &rec);
bool readShadeRecord(ConfigReader &r, const config_header_t &header, shade_record_t &rec);
bool writeGroupRecord(ConfigWriter &w, const group_record_t &rec);
bool readGroupRecord(ConfigReader &r, const config_header_t &header, group_record_t &rec);
bool writeRepeaterRecord(ConfigWriter &w, const uint32_t (&repeaters)[SOMFY_MAX_REPEATERS]);
bool readRepeaterRecord(ConfigReader &r, uint32_t (&repeaters)[SOMFY_MAX_REPEATERS]);
bool writeSettingsRecord(ConfigWriter &w, const settings_record_t &rec);
bool readSettingsRecord(ConfigReader &r, const config_header_t &header, settings_record_t &rec);
// withEthernet: the Ethernet settings, which a board without Ethernet leaves out.
bool writeNetRecord(ConfigWriter &w, const net_record_t &rec, bool withEthernet);
bool readNetRecord(ConfigReader &r, const config_header_t &header, net_record_t &rec, bool withEthernet);
bool writeTransRecord(ConfigWriter &w, const trans_record_t &rec);
bool readTransRecord(ConfigReader &r, trans_record_t &rec);
// Moves past count records without reading them.
bool skipRecords(ConfigReader &r, uint32_t count);

#endif
