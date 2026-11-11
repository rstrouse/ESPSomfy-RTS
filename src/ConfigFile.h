#include <ArduinoJson.h>
#include <LittleFS.h>
#include "Somfy.h"
#include "ConfigSettings.h"
#include "ConfigHeader.h"
#include "ConfigRecords.h"

#ifndef configfile_h
#define configfile_h

// Opens shades.cfg or a backup and writes or reads its values
// (ConfigRecords.h), from the file or into a String in RAM.
class ConfigFile : public ConfigReader, public ConfigWriter {
  protected:
    File file;
    String *_ramBuf = nullptr;
    bool readOnly = false;
    bool begin(const char *filename, bool readOnly = false);
    bool _opened = false;
  public:
    config_header_t header;
    bool beginRAM(String *buf);
    void end();
    bool isOpen();
    bool readHeader();
    bool writeHeader();
    bool readByte(uint8_t &val) override;
    uint32_t position() override;
    bool writeBytes(const uint8_t *buf, size_t len) override;
};
class ShadeConfigFile : public ConfigFile {
  protected:
    // A restore takes the rolling codes in the file (rollingCodeAfterLoad).
    bool restoring = false;
    bool writeModel(SomfyShadeController *s);
    void readModel(SomfyShadeController *s);
    void readRoom(SomfyRoom *room);
    void readShade(SomfyShade *shade);
    void readGroup(SomfyGroup *group);
    void readRepeaters(SomfyShadeController *s);
  public:
    static bool exists();
    static bool load(SomfyShadeController *somfy, const char *filename = "/shades.cfg");
    static bool restore(SomfyShadeController *somfy, const char *filename, restore_options_t &opts);
    // Whether the file would pass the checks restore() starts with.  Only
    // reads the file, so any task can call it.
    static bool validateFile(const char *filename);
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
