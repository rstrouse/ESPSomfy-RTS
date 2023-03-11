#include <ArduinoJson.h>
#include <LittleFS.h>
#include "Somfy.h"
#ifndef configfile_h
#define configfile_h

#define CFG_VALUE_SEP ','
#define CFG_REC_END '\n'
#define CFG_TOK_NONE 0x00

typedef struct config_header_t {
  uint8_t version = 1;
  uint16_t recordSize = 0;
  uint16_t records = 0;
  uint8_t length = 0;
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
    bool save();
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
    bool writeUInt8(const uint8_t val, const char tok = CFG_VALUE_SEP);
    bool writeUInt16(const uint16_t val, const char tok = CFG_VALUE_SEP);
    bool writeUInt32(const uint32_t val, const char tok = CFG_VALUE_SEP);
    bool writeBool(const bool val, const char tok = CFG_VALUE_SEP);
    bool writeFloat(const float val, const uint8_t prec, const char tok = CFG_VALUE_SEP);
    bool readString(char *buff, size_t len);
    bool writeString(const char *val, size_t len, const char tok = CFG_VALUE_SEP);
    char readChar(const char defVal = '\0');
    uint8_t readUInt8(const uint8_t defVal = 0);
    uint16_t readUInt16(const uint16_t defVal = 0);
    uint32_t readUInt32(const uint32_t defVal = 0);
    bool readBool(const bool defVal = false);
    float readFloat(const float defVal = 0.00);
};
class ShadeConfigFile : public ConfigFile {
  protected:
    bool writeShadeRecord(SomfyShade *shade);
  public:
    static bool getAppVersion(appver_t &ver);
    static bool exists();
    static bool load(SomfyShadeController *somfy, const char *filename = "/shades.cfg");
    bool begin(const char *filename, bool readOnly = false);
    bool begin(bool readOnly = false);
    bool save(SomfyShadeController *sofmy);
    bool loadFile(SomfyShadeController *somfy, const char *filename = "/shades.cfg");
    void end();
    bool seekRecordById(uint8_t id);
    bool validate();
};
#endif;
