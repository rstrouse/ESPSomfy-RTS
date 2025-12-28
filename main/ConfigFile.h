#include <ArduinoJson.h>
#include <LittleFS.h>
#include "Somfy.h"
#include "ConfigSettings.h"

#ifndef configfile_h
#define configfile_h

#define CFG_VALUE_SEP ','
#define CFG_REC_END '\n'
#define CFG_TOK_NONE 0x00
#define CFG_TOK_QUOTE '"'


struct config_header_t {
  uint8_t version = 1;
  uint8_t repeaterRecords = 0;
  uint8_t repeaterRecordSize = 0;
  uint8_t roomRecords = 0;
  uint16_t roomRecordSize = 0;
  uint16_t shadeRecordSize = 0;
  uint8_t shadeRecords = 0;
  uint16_t groupRecordSize = 0;
  uint8_t groupRecords = 0;
  uint16_t settingsRecordSize = 0;
  uint16_t netRecordSize = 0;
  uint16_t transRecordSize = 0;
  char serverId[10] = ""; // This must match the server id size in the ConfigSettings.
  int8_t length = 0;
};
class ConfigFile {
  protected:
    File file;
    bool readOnly = false;
    bool begin(const char *filename, bool readOnly = false);
    uint32_t startRecPos = 0;
    bool _opened = false;
  public:
    config_header_t header;
    void end();
    bool isOpen();
    bool seekRecordByIndex(uint16_t ndx);
    bool readHeader();
    bool seekChar(const char val);
    bool writeHeader(const config_header_t &header);
    bool writeHeader();
    bool writeSeparator();
    bool writeRecordEnd();
    bool writeChar(const char val);
    bool writeInt8(const int8_t val, const char tok = CFG_VALUE_SEP);
    bool writeUInt8(const uint8_t val, const char tok = CFG_VALUE_SEP);
    bool writeUInt16(const uint16_t val, const char tok = CFG_VALUE_SEP);
    bool writeUInt32(const uint32_t val, const char tok = CFG_VALUE_SEP);
    bool writeBool(const bool val, const char tok = CFG_VALUE_SEP);
    bool writeFloat(const float val, const uint8_t prec, const char tok = CFG_VALUE_SEP);
    bool readString(char *buff, size_t len);
    bool readVarString(char *buff, size_t len);
    bool skipValue(size_t len);
    bool writeString(const char *val, size_t len, const char tok = CFG_VALUE_SEP);
    bool writeVarString(const char *val, const char tok = CFG_VALUE_SEP);
    char readChar(const char defVal = '\0');
    int8_t readInt8(const int8_t defVal = 0);
    uint8_t readUInt8(const uint8_t defVal = 0);
    uint16_t readUInt16(const uint16_t defVal = 0);
    uint32_t readUInt32(const uint32_t defVal = 0);
    bool readBool(const bool defVal = false);
    float readFloat(const float defVal = 0.00);
};
class ShadeConfigFile : public ConfigFile {
  protected:
    bool writeRepeaterRecord(SomfyShadeController *s);
    bool writeRoomRecord(SomfyRoom *room);
    bool writeShadeRecord(SomfyShade *shade);
    bool writeGroupRecord(SomfyGroup *group);
    bool writeSettingsRecord();
    bool writeNetRecord();
    bool writeTransRecord(transceiver_config_t &cfg);
    bool readRepeaterRecord(SomfyShadeController *s);
    bool readRoomRecord(SomfyRoom *room);
    bool readShadeRecord(SomfyShade *shade);
    bool readGroupRecord(SomfyGroup *group);
    bool readSettingsRecord();
    bool readNetRecord(restore_options_t &opts);
    bool readTransRecord(transceiver_config_t &cfg);
  public:
    static bool exists();
    static bool load(SomfyShadeController *somfy, const char *filename = "/shades.cfg");
    static bool restore(SomfyShadeController *somfy, const char *filename, restore_options_t &opts);
    bool begin(const char *filename, bool readOnly = false);
    bool begin(bool readOnly = false);
    bool save(SomfyShadeController *somfy);
    bool backup(SomfyShadeController *somfy);
    bool loadFile(SomfyShadeController *somfy, const char *filename = "/shades.cfg");
    bool restoreFile(SomfyShadeController *somfy, const char *filename, restore_options_t &opts);
    void end();
    //bool seekRecordById(uint8_t id);
    bool validate();
};
#endif
