#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "ConfigFile.h"
#include "Utils.h"
#include "ConfigSettings.h"

extern Preferences pref;

#define SHADE_HDR_VER 24
#define SHADE_HDR_SIZE 76
#define SHADE_REC_SIZE 276
#define GROUP_REC_SIZE 200
#define TRANS_REC_SIZE 74
#define ROOM_REC_SIZE 29
#define REPEATER_REC_SIZE 77

extern ConfigSettings settings;

bool ConfigFile::begin(const char* filename, bool readOnly) {
  this->file = LittleFS.open(filename, readOnly ? "r" : "w");
  this->_opened = true;
  return true;
}
void ConfigFile::end() {
  if(this->isOpen()) {
    if(!this->readOnly) this->file.flush();
    this->file.close();
  }
  this->_opened = false;
}
bool ConfigFile::isOpen() { return this->_opened; }
bool ConfigFile::seekChar(const char val) {
  if(!this->isOpen()) return false;
  char ch;
  do {
    ch = this->readChar('\0');
    if(ch == '\0') return false;
  } while(ch != val);
  return true;
}
bool ConfigFile::writeSeparator() {return this->writeChar(CFG_VALUE_SEP); }
bool ConfigFile::writeRecordEnd() { return this->writeChar(CFG_REC_END); }
bool ConfigFile::writeHeader() { return this->writeHeader(this->header); }
bool ConfigFile::writeHeader(const config_header_t &hdr) {
  if(!this->isOpen()) return false;
  this->writeUInt8(hdr.version);
  this->writeUInt8(hdr.length);
  this->writeUInt16(hdr.roomRecordSize);
  this->writeUInt8(hdr.roomRecords);
  this->writeUInt16(hdr.shadeRecordSize);
  this->writeUInt8(hdr.shadeRecords);
  this->writeUInt16(hdr.groupRecordSize);
  this->writeUInt8(hdr.groupRecords);
  this->writeUInt16(hdr.repeaterRecordSize);
  this->writeUInt8(hdr.repeaterRecords);
  this->writeUInt16(hdr.settingsRecordSize);
  this->writeUInt16(hdr.netRecordSize);
  this->writeUInt16(hdr.transRecordSize);
  this->writeString(settings.serverId, sizeof(hdr.serverId), CFG_REC_END);
  return true;
}
bool ConfigFile::readHeader() {
  if(!this->isOpen()) return false;
  //if(this->file.position() != 0) this->file.seek(0, SeekSet);
  Serial.printf("Reading header at %u\n", this->file.position());
  this->header.version = this->readUInt8(this->header.version);
  this->header.length = this->readUInt8(0);
  if(this->header.version >= 19) {
    this->header.roomRecordSize = this->readUInt16(this->header.roomRecordSize);
    this->header.roomRecords = this->readUInt8(this->header.roomRecords);
  }
  if(this->header.version >= 13) this->header.shadeRecordSize = this->readUInt16(this->header.shadeRecordSize);
  else this->header.shadeRecordSize = this->readUInt8((uint8_t)this->header.shadeRecordSize);
  this->header.shadeRecords = this->readUInt8(this->header.shadeRecords);
  if(this->header.version > 10) {
    if(this->header.version >= 13) this->header.groupRecordSize = this->readUInt16(this->header.groupRecordSize);
    else this->header.groupRecordSize = this->readUInt8(this->header.groupRecordSize);
    this->header.groupRecords = this->readUInt8(this->header.groupRecords);
  }
  if(this->header.version >= 21) {
    this->header.repeaterRecordSize = this->readUInt16(this->header.repeaterRecordSize);
    this->header.repeaterRecords = this->readUInt8(this->header.repeaterRecords);
  }
  if(this->header.version > 13) {
    this->header.settingsRecordSize = this->readUInt16(this->header.settingsRecordSize);
    this->header.netRecordSize = this->readUInt16(this->header.netRecordSize);
    this->header.transRecordSize = this->readUInt16(this->header.transRecordSize);
    this->readString(this->header.serverId, sizeof(this->header.serverId));
  }
  Serial.printf("version:%u len:%u roomSize:%u roomRecs:%u shadeSize:%u shadeRecs:%u groupSize:%u groupRecs: %u pos:%d\n", this->header.version, this->header.length, this->header.roomRecordSize, this->header.roomRecords, this->header.shadeRecordSize, this->header.shadeRecords, this->header.groupRecordSize, this->header.groupRecords, this->file.position());
  return true;
}
/*
bool ConfigFile::seekRecordByIndex(uint16_t ndx) {
  if(!this->file) {
    return false;
  }
  if(((this->header.recordSize * ndx) + this->header.length) > this->file.size()) return false;
  return true;
}
*/
bool ConfigFile::readString(char *buff, size_t len) {
  if(!this->file) return false;
  memset(buff, 0x00, len);
  uint16_t i = 0;
  while(i < len) {
    uint8_t val;
    if(this->file.read(&val, 1) == 1) {
      switch(val) {
        case CFG_REC_END:
        case CFG_VALUE_SEP:
          _rtrim(buff);
          return true;
      }
      buff[i++] = val;
      if(i == len) {
        _rtrim(buff);
        return true;
      }
    }
    else 
      return false;
  }
  _rtrim(buff);
  return true;
}
bool ConfigFile::skipValue(size_t len) {
  if(!this->file) return false;
  uint8_t quotes = 0;
  uint8_t j = 0;
  while(j < len) {
    uint8_t val;
    j++;
    if(this->file.read(&val, 1) == 1) {
      switch(val) {
        case CFG_VALUE_SEP:
          if(quotes >= 2 || quotes == 0) return true;
          break;
        case CFG_REC_END:
          return true;
        case CFG_TOK_QUOTE:
          quotes++;
          break;
      }
    }
    else return false;
  }
  return true;
}
bool ConfigFile::readVarString(char *buff, size_t len) {
  if(!this->file) return false;
  memset(buff, 0x00, len);
  uint8_t quotes = 0;
  uint16_t i = 0;
  uint16_t j = 0;
  while(j < len) {
    uint8_t val;
    j++;
    if(this->file.read(&val, 1) == 1) {
      switch(val) {
        case CFG_VALUE_SEP:
          if(quotes >= 2) {
            _rtrim(buff);
            return true;
          }
          break;
        case CFG_REC_END:
          return true;
        case CFG_TOK_QUOTE:
          quotes++;
          continue;
      }
      buff[i++] = val;
      if(i == len) {
        _rtrim(buff);
        return true;
      }
    }
    else 
      return false;
  }
  _rtrim(buff);
  return true;
}

bool ConfigFile::writeString(const char *val, size_t len, const char tok) {
  if(!this->isOpen()) return false;
  int slen = strlen(val);
  if(slen > 0)
    if(this->file.write((uint8_t *)val, slen) != slen) return false;
  // Now we need to pad the end of the string so that it is of a fixed length.
  while(slen < len - 1) {
    this->file.write(' ');
    slen++;
  }
  // 255 = len = 4 slen = 3
  if(tok != CFG_TOK_NONE)
    return this->writeChar(tok);
  return true;
}
bool ConfigFile::writeVarString(const char *val, const char tok) {
  if(!this->isOpen()) return false;
  int slen = strlen(val);
  this->writeChar(CFG_TOK_QUOTE);
  if(slen > 0) if(this->file.write((uint8_t *)val, slen) != slen) return false;
  this->writeChar(CFG_TOK_QUOTE);
  if(tok != CFG_TOK_NONE) return this->writeChar(tok);
  return true;
}
bool ConfigFile::writeChar(const char val) {
  if(!this->isOpen()) return false;
  if(this->file.write(static_cast<uint8_t>(val)) == 1) return true;
  return false;
}
bool ConfigFile::writeInt8(const int8_t val, const char tok) {
  char buff[5];
  snprintf(buff, sizeof(buff), "%4d", val);
  return this->writeString(buff, sizeof(buff), tok);
}
bool ConfigFile::writeUInt8(const uint8_t val, const char tok) {
  char buff[4];
  snprintf(buff, sizeof(buff), "%3u", val);
  return this->writeString(buff, sizeof(buff), tok); 
}
bool ConfigFile::writeUInt16(const uint16_t val, const char tok) {
  char buff[6];
  snprintf(buff, sizeof(buff), "%5u", val);
  return this->writeString(buff, sizeof(buff), tok); 
}
bool ConfigFile::writeUInt32(const uint32_t val, const char tok) {
  char buff[11];
  snprintf(buff, sizeof(buff), "%10u", val);
  return this->writeString(buff, sizeof(buff), tok); 
}
bool ConfigFile::writeFloat(const float val, const uint8_t prec, const char tok) {
  char buff[20];
  snprintf(buff, sizeof(buff), "%*.*f", 7 + prec, prec, val);
  return this->writeString(buff, 8 + prec, tok);
}
bool ConfigFile::writeBool(const bool val, const char tok) {
  return this->writeString(val ? "true" : "false", 6, tok);
}
char ConfigFile::readChar(const char defVal) {
  uint8_t ch;
  if(this->file.read(&ch, 1) == 1) return (char)ch;
  return defVal;
}
int8_t ConfigFile::readInt8(const int8_t defVal) {
  char buff[5];
  if(this->readString(buff, sizeof(buff)))
    return static_cast<int8_t>(atoi(buff));
  return defVal;
}
uint8_t ConfigFile::readUInt8(const uint8_t defVal) {
  char buff[4];
  if(this->readString(buff, sizeof(buff)))
    return static_cast<uint8_t>(atoi(buff));
  return defVal;
}
uint16_t ConfigFile::readUInt16(const uint16_t defVal) {
  char buff[6];
  if(this->readString(buff, sizeof(buff)))
    return static_cast<uint16_t>(atoi(buff));
  return defVal;
}
uint32_t ConfigFile::readUInt32(const uint32_t defVal) {
  char buff[11];
  if(this->readString(buff, sizeof(buff)))
    return static_cast<uint32_t>(atoi(buff));
  return defVal;
}
float ConfigFile::readFloat(const float defVal) {
  char buff[25];
  if(this->readString(buff, sizeof(buff)))
    return atof(buff);
  return defVal;
}
bool ConfigFile::readBool(const bool defVal) {
  char buff[6];
  if(this->readString(buff, sizeof(buff))) {
    switch(buff[0]) {
      case 't':
      case 'T':
      case '1':
        return true;
      default: 
        return false;    
    }
  }
  return defVal;
}
/*
bool ShadeConfigFile::seekRecordById(uint8_t id) {
  if(this->isOpen()) return false;
  this->file.seek(this->header.length, SeekSet);  // Start at the beginning of the file after the header.
  uint8_t i = 0;
  while(i < SOMFY_MAX_SHADES) {
    uint32_t pos = this->file.position();
    uint8_t len = this->readUInt8(this->header.recordSize);
    uint8_t cid = this->readUInt8(255);
    if(cid == id) {
      this->file.seek(pos, SeekSet);
      return true;
    }
    pos += len;
    this->file.seek(pos, SeekSet);
  }
  return false;
}
*/
bool ShadeConfigFile::begin(bool readOnly) { return this->begin("/shades.cfg", readOnly); }
bool ShadeConfigFile::begin(const char *filename, bool readOnly) { return ConfigFile::begin(filename, readOnly); }
void ShadeConfigFile::end() { ConfigFile::end(); }
bool ShadeConfigFile::save(SomfyShadeController *s) {
  this->header.version = SHADE_HDR_VER;
  this->header.roomRecordSize = ROOM_REC_SIZE;
  this->header.roomRecords = s->roomCount();
  this->header.shadeRecordSize = SHADE_REC_SIZE;
  this->header.length = SHADE_HDR_SIZE;
  this->header.shadeRecords = s->shadeCount();
  this->header.groupRecordSize = GROUP_REC_SIZE;
  this->header.groupRecords = s->groupCount();
  this->header.repeaterRecords = 1;
  this->header.repeaterRecordSize = REPEATER_REC_SIZE;
  this->header.settingsRecordSize = 0;
  this->header.netRecordSize = 0;
  this->header.transRecordSize = 0;
  this->writeHeader();
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    SomfyRoom *room = &s->rooms[i];
    if(room->roomId != 0)
      this->writeRoomRecord(room);
  }
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &s->shades[i];
    if(shade->getShadeId() != 255)
      this->writeShadeRecord(shade);
  }
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &s->groups[i];
    if(group->getGroupId() != 255)
      this->writeGroupRecord(group);
  }
  this->writeRepeaterRecord(s);
  return true;
}
bool ShadeConfigFile::backup(SomfyShadeController *s) {
  this->header.version = SHADE_HDR_VER;
  this->header.roomRecordSize = ROOM_REC_SIZE;
  this->header.roomRecords = s->roomCount();
  this->header.shadeRecordSize = SHADE_REC_SIZE;
  this->header.length = SHADE_HDR_SIZE;
  this->header.shadeRecords = s->shadeCount();
  this->header.groupRecordSize = GROUP_REC_SIZE;
  this->header.groupRecords = s->groupCount();
  this->header.repeaterRecords = 1;
  this->header.repeaterRecordSize = REPEATER_REC_SIZE;
  this->header.settingsRecordSize = settings.calcSettingsRecSize();
  this->header.netRecordSize = settings.calcNetRecSize();
  this->header.transRecordSize = TRANS_REC_SIZE;
  this->writeHeader();
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    SomfyRoom *room = &s->rooms[i];
    if(room->roomId != 0)
      this->writeRoomRecord(room);
  }
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &s->shades[i];
    if(shade->getShadeId() != 255)
      this->writeShadeRecord(shade);
  }
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &s->groups[i];
    if(group->getGroupId() != 255)
      this->writeGroupRecord(group);
  }
  this->writeRepeaterRecord(s);
  this->writeSettingsRecord();
  this->writeNetRecord();
  this->writeTransRecord(s->transceiver.config);
  return true;
}
bool ShadeConfigFile::validate() {
  this->readHeader();
  if(this->header.version < 1) {
    Serial.print("Invalid Header Version:");
    Serial.println(this->header.version);
    return false;
  }
  if(this->header.shadeRecordSize < 100) {
    Serial.print("Invalid Shade Record Size:");
    Serial.println(this->header.shadeRecordSize);
    return false;
  }
  /*
  if(this->header.shadeRecords != SOMFY_MAX_SHADES) {
    Serial.print("Invalid Shade Record Count:");
    Serial.println(this->header.shadeRecords);
    return false;
  }
  */
  if(this->header.version > 10) {
    if(this->header.groupRecordSize < 100) {
      Serial.print("Invalid Group Record Size:");
      Serial.println(this->header.groupRecordSize);
      return false;
    }
    /*
    if(this->header.groupRecords != SOMFY_MAX_GROUPS) {
      Serial.print("Invalid Group Record Count:");
      Serial.println(this->header.groupRecords);
      return false;
      
    }
    */
  }
  if(this->file.position() != this->header.length) {
    Serial.printf("File not positioned at %u end of header: %d\n", this->header.length, this->file.position());
    return false;
  }
  
  // We should know the file size based upon the record information in the header
  uint32_t fsize = this->header.length + (this->header.shadeRecordSize * this->header.shadeRecords);
  if(this->header.version > 10) fsize += (this->header.groupRecordSize * this->header.groupRecords);
  if(this->header.version >= 19) fsize += (this->header.roomRecordSize * this->header.roomRecords);
  if(this->header.version > 13) {
    fsize += (this->header.settingsRecordSize);
    fsize += (this->header.netRecordSize);
    fsize += (this->header.transRecordSize);
  }
  if(this->header.version >= 21) {
    fsize += (this->header.repeaterRecordSize * this->header.repeaterRecords);
  }
  if(this->file.size() != fsize) {
    Serial.printf("File size is not correct should be %d and got %d\n", fsize, this->file.size());
  }
  // Next check to see if the records match the header length.
  uint8_t recs = 0;
  uint32_t startPos = this->file.position();
  if(this->header.version >= 19) {
    while(recs < this->header.roomRecords) {
      uint32_t pos = this->file.position();
      if(!this->seekChar(CFG_REC_END)) {
        Serial.printf("Failed to find the room record end %d\n", recs);
        return false;
      }
      if(this->file.position() - pos != this->header.roomRecordSize) {
        Serial.printf("Room record length is %d and should be %d\n", this->file.position() - pos, this->header.roomRecordSize);
        return false;
      }
      recs++;
    }
    recs = 0;
  }
  while(recs < this->header.shadeRecords) {
    uint32_t pos = this->file.position();
    if(!this->seekChar(CFG_REC_END)) {
      Serial.printf("Failed to find the shade record end %d\n", recs);
      return false;
    }
    if(this->file.position() - pos != this->header.shadeRecordSize) {
      Serial.printf("Shade record length is %d and should be %d\n", this->file.position() - pos, this->header.shadeRecordSize);
      return false;
    }
    recs++;
  }
  if(this->header.version > 10) {
    recs = 0;
    while(recs < this->header.groupRecords) {
      uint32_t pos = this->file.position();
      if(!this->seekChar(CFG_REC_END)) {
        Serial.printf("Failed to find the group record end %d\n", recs);
        return false;
      }
      recs++;
      if(this->file.position() - pos != this->header.groupRecordSize) {
        Serial.printf("Group record length is %d and should be %d\n", this->file.position() - pos, this->header.groupRecordSize);
        return false;
      }
    }
  }
  if(this->header.version >= 21) {
    recs = 0;
    while(recs < this->header.repeaterRecords) {
      //uint32_t pos = this->file.position();
      if(!this->seekChar(CFG_REC_END)) {
        Serial.printf("Failed to find the repeater record end %d\n", recs);
      }
      recs++;
      
    }
  }
  this->file.seek(startPos, SeekSet);
  return true;  
}
bool ShadeConfigFile::load(SomfyShadeController *s, const char *filename) {
  ShadeConfigFile file;
  if(file.begin(filename, true)) {
    bool success = file.loadFile(s, filename);
    file.end();
    return success;
  }
  return false;
}
bool ShadeConfigFile::restore(SomfyShadeController *s, const char *filename, restore_options_t &opts) {
  ShadeConfigFile file;
  if(file.begin(filename, true)) {
    bool success = file.restoreFile(s, filename, opts);
    file.end();
    return success;
  }
  return false;
}
bool ShadeConfigFile::restoreFile(SomfyShadeController *s, const char *filename, restore_options_t &opts) {
  bool opened = false;
  if(!this->isOpen()) {
    Serial.println("Opening shade restore file");
    this->begin(filename, true);
    opened = true;
  }
  if(!this->validate()) {
    Serial.println("Shade restore file invalid!");
    if(opened) this->end();
    return false;
  }
  if(opts.shades) {
    Serial.println("Restoring Rooms...");
    for(uint8_t i = 0; i < this->header.roomRecords; i++) {
      this->readRoomRecord(&s->rooms[i]);
      if(i > 0) Serial.print(",");
      Serial.print(s->rooms[i].roomId);
    }
    Serial.println("Restoring Shades...");
    // We should be valid so start reading.
    for(uint8_t i = 0; i < this->header.shadeRecords; i++) {
      this->readShadeRecord(&s->shades[i]);
      if(i > 0) Serial.print(",");
      Serial.print(s->shades[i].getShadeId());
    }
    Serial.println("");
    if(this->header.shadeRecords < SOMFY_MAX_SHADES) {
      uint8_t ndx = this->header.shadeRecords;
      // Clear out any positions that are not in the shade file.
      while(ndx < SOMFY_MAX_SHADES) {
        ((SomfyShade *)&s->shades[ndx++])->clear();
      }
    }
    Serial.println("Restoring Groups...");
    for(uint8_t i = 0; i < this->header.groupRecords; i++) {
      if(i > 0) Serial.print(",");
      Serial.print(s->groups[i].getGroupId());
      this->readGroupRecord(&s->groups[i]);
    }
    Serial.println("");
    if(this->header.groupRecords < SOMFY_MAX_GROUPS) {
      uint8_t ndx = this->header.groupRecords;
      // Clear out any positions that are not in the shade file.
      while(ndx < SOMFY_MAX_GROUPS) {
        ((SomfyGroup *)&s->groups[ndx++])->clear();
      }
    }
  }
  else {
    Serial.println("Shade data ignored");
    // FF past the shades and groups.
    this->file.seek(this->file.position()
      + (this->header.shadeRecords * this->header.shadeRecordSize)
      + (this->header.groupRecords * this->header.groupRecordSize), SeekSet);  // Start at the beginning of the file after the header.
  }
  if(opts.repeaters) {
    Serial.println("Restoring Repeaters...");
    if(this->header.repeaterRecords > 0) {
      memset(s->repeaters, 0x00, sizeof(uint32_t) * SOMFY_MAX_REPEATERS);
      for(uint8_t i = 0; i < this->header.repeaterRecords; i++) {
        this->readRepeaterRecord(s);
      }
    }
  }
  else {
    this->file.seek(this->file.position() + this->header.repeaterRecordSize, SeekSet);
  }
  if(opts.settings) {
    // First read out the data.
    this->readSettingsRecord();
  }
  else {
    this->file.seek(this->file.position() + this->header.settingsRecordSize, SeekSet);
  }
  if(opts.network || opts.mqtt) {
    this->readNetRecord(opts);
  }
  else {
    this->file.seek(this->file.position() + this->header.netRecordSize, SeekSet);
  }
  if(opts.shades) s->commit();
  if(opts.transceiver)
  {
    this->readTransRecord(s->transceiver.config);
    s->transceiver.save();
  }
  if(opts.settings || opts.network) settings.save();
  if(opts.settings) settings.NTP.save();
  if(opts.network) {
    settings.IP.save();
    settings.WIFI.save();
    settings.Ethernet.save();
  }
  if(opts.mqtt) settings.MQTT.save();
  return true;
}
bool ShadeConfigFile::readNetRecord(restore_options_t &opts) {
  if(this->header.netRecordSize > 0) {
    uint32_t startPos = this->file.position();
    if(opts.network) {
      Serial.println("Reading network settings from file...");
      settings.connType = static_cast<conn_types_t>(this->readUInt8(static_cast<uint8_t>(conn_types_t::unset)));
      settings.IP.dhcp = this->readBool(true);
      char ip[24];
      this->readVarString(ip, sizeof(ip));
      settings.IP.ip.fromString(ip);
      this->readVarString(ip, sizeof(ip));
      settings.IP.gateway.fromString(ip);
      this->readVarString(ip, sizeof(ip));
      settings.IP.subnet.fromString(ip);
      this->readVarString(ip, sizeof(ip));
      settings.IP.dns1.fromString(ip);
      this->readVarString(ip, sizeof(ip));
      settings.IP.dns2.fromString(ip);
    }
    else {
      this->skipValue(4); // connType
      this->skipValue(6); // dhcp flag
      this->skipValue(24); // ip
      this->skipValue(24); // gateway
      this->skipValue(24); // subnet
      this->skipValue(24); // dns1
      this->skipValue(24); // dns2
    }
    if(this->header.version >= 22) {
      if(opts.mqtt) {
        this->readVarString(settings.MQTT.protocol, sizeof(settings.MQTT.protocol));
        this->readVarString(settings.MQTT.hostname, sizeof(settings.MQTT.hostname));
        settings.MQTT.port = this->readUInt16(1883);
        settings.MQTT.pubDisco = this->readBool(false);
        this->readVarString(settings.MQTT.rootTopic, sizeof(settings.MQTT.rootTopic));
        this->readVarString(settings.MQTT.discoTopic, sizeof(settings.MQTT.discoTopic));
      }
      else {
        this->skipValue(sizeof(settings.MQTT.protocol));
        this->skipValue(sizeof(settings.MQTT.hostname));
        this->skipValue(6); // Port
        this->skipValue(6); // pubDisco
        this->skipValue(sizeof(settings.MQTT.rootTopic));
        this->skipValue(sizeof(settings.MQTT.discoTopic));
      }
    }
    // Now lets check to see if we are the same board.  If we are then we will restore
    // the ethernet phy settings.
    if(opts.network) {
      if(strncmp(settings.serverId, this->header.serverId, sizeof(settings.serverId)) == 0) {
        Serial.println("Restoring Ethernet adapter settings");
        settings.Ethernet.boardType = this->readUInt8(1);
        settings.Ethernet.phyType = static_cast<eth_phy_type_t>(this->readUInt8(0));
        settings.Ethernet.CLKMode = static_cast<eth_clock_mode_t>(this->readUInt8(0));
        settings.Ethernet.phyAddress = this->readInt8(1);
        settings.Ethernet.PWRPin = this->readInt8(1);
        settings.Ethernet.MDCPin = this->readInt8(16);
        settings.Ethernet.MDIOPin = this->readInt8(23);
      }
    }
    if(this->file.position() != startPos + this->header.netRecordSize) {
      Serial.println("Reading to end of network record");
      this->seekChar(CFG_REC_END);
    }
  }
  return true;
}
bool ShadeConfigFile::readTransRecord(transceiver_config_t &cfg) {
  if(this->header.transRecordSize > 0) {
    uint32_t startPos = this->file.position();
    Serial.println("Reading Transceiver settings from file...");
    cfg.enabled = this->readBool(false);
    cfg.proto = static_cast<radio_proto>(this->readUInt8(0));
    cfg.type = this->readUInt8(56);
    cfg.SCKPin = this->readUInt8(cfg.SCKPin);
    cfg.CSNPin = this->readUInt8(cfg.CSNPin);
    cfg.MOSIPin = this->readUInt8(cfg.MOSIPin);
    cfg.MISOPin = this->readUInt8(cfg.MISOPin);
    cfg.TXPin = this->readUInt8(cfg.TXPin);
    cfg.RXPin = this->readUInt8(cfg.RXPin);
    cfg.frequency = this->readFloat(cfg.frequency);
    cfg.rxBandwidth = this->readFloat(cfg.rxBandwidth);
    cfg.deviation = this->readFloat(cfg.deviation);
    cfg.txPower = this->readInt8(cfg.txPower);  
    if(this->file.position() != startPos + this->header.transRecordSize) {
      Serial.println("Reading to end of transceiver record");
      this->seekChar(CFG_REC_END);
    }
    
  }
  return true; 
}
bool ShadeConfigFile::readSettingsRecord() {
  if(this->header.settingsRecordSize > 0) {
    uint32_t startPos = this->file.position();
    Serial.println("Reading settings from file...");
    char ver[24];
    this->readVarString(ver, sizeof(ver));
    this->readVarString(settings.hostname, sizeof(settings.hostname));
    this->readVarString(settings.NTP.ntpServer, sizeof(settings.NTP.ntpServer));
    this->readVarString(settings.NTP.posixZone, sizeof(settings.NTP.posixZone));
    settings.ssdpBroadcast = this->readBool(false);
    if(this->header.version >= 20) settings.checkForUpdate = this->readBool(true);
    if(this->file.position() != startPos + this->header.settingsRecordSize) {
      Serial.println("Reading to end of settings record");
      this->seekChar(CFG_REC_END);
    }
  }
  return true;
}
bool ShadeConfigFile::readGroupRecord(SomfyGroup *group) {
  pref.begin("ShadeCodes");
  uint32_t startPos = this->file.position();
  group->setGroupId(this->readUInt8(255));
  group->groupType = static_cast<group_types>(this->readUInt8(0));
  group->setRemoteAddress(this->readUInt32(0));
  this->readString(group->name, sizeof(group->name));
  group->proto = static_cast<radio_proto>(this->readUInt8(0));
  group->bitLength = this->readUInt8(56);
  if(this->header.version == 23) group->lastRollingCode = this->readUInt16(0);
  uint8_t lsd = 0;
  memset(group->linkedShades, 0x00, sizeof(group->linkedShades));
  for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) {
    uint8_t shadeId = this->readUInt8(0);
    // Do this to eliminate gaps.
    if(shadeId > 0) group->linkedShades[lsd++] = shadeId;
  }
  if(this->header.version >= 12) group->repeats = this->readUInt8(1);
  if(this->header.version >= 13) group->sortOrder = this->readUInt8(group->getGroupId() - 1);
  else group->sortOrder = group->getGroupId() - 1;
  
  if(group->getGroupId() == 255) group->clear();
  else group->compressLinkedShadeIds();
  if(this->header.version >= 18) group->flipCommands = this->readBool(false);
  if(this->header.version >= 19) group->roomId = this->readUInt8(0);
  if(this->header.version >= 24) group->lastRollingCode = this->readUInt16(0);
  if(group->getRemoteAddress() != 0) {
    uint16_t rc = pref.getUShort(group->getRemotePrefId(), 0);
    group->lastRollingCode = max(rc, group->lastRollingCode);
    if(rc < group->lastRollingCode) pref.putUShort(group->getRemotePrefId(), group->lastRollingCode);
  }
  
  pref.end();
  if(this->file.position() != startPos + this->header.groupRecordSize) {
    Serial.println("Reading to end of group record");
    this->seekChar(CFG_REC_END);
  }
  return true;
}
bool ShadeConfigFile::readRepeaterRecord(SomfyShadeController *s) {
  uint32_t startPos = this->file.position();
  
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    s->linkRepeater(this->readUInt32(0));  
  }
  if(this->file.position() != startPos + this->header.repeaterRecordSize) {
    Serial.println("Reading to end of repeater record");
    this->seekChar(CFG_REC_END);
  }
  return true;
}
bool ShadeConfigFile::readRoomRecord(SomfyRoom *room) {
  uint32_t startPos = this->file.position();
  room->roomId = this->readUInt8(0);
  this->readString(room->name, sizeof(room->name));
  room->sortOrder = this->readUInt8(room->roomId - 1);
  if(this->file.position() != startPos + this->header.roomRecordSize) {
    Serial.println("Reading to end of room record");
    this->seekChar(CFG_REC_END);
  }
  return true;
}

bool ShadeConfigFile::readShadeRecord(SomfyShade *shade) {
  pref.begin("ShadeCodes");
  uint32_t startPos = this->file.position();
  shade->setShadeId(this->readUInt8(255));
  shade->paired = this->readBool(false);
  shade->shadeType = static_cast<shade_types>(this->readUInt8(0));
  shade->setRemoteAddress(this->readUInt32(0));
  this->readString(shade->name, sizeof(shade->name));
  if(this->header.version < 3)
    shade->tiltType = this->readBool(false) ? tilt_types::none : tilt_types::tiltmotor;
  else
    shade->tiltType = static_cast<tilt_types>(this->readUInt8(0));
  if(this->header.version > 6) shade->proto = static_cast<radio_proto>(this->readUInt8(0));
  if(this->header.version > 1) shade->bitLength = this->readUInt8(56);
  shade->upTime = this->readUInt32(shade->upTime);
  shade->downTime = this->readUInt32(shade->downTime);
  shade->tiltTime = this->readUInt32(shade->tiltTime);
  if(this->header.version > 5) shade->stepSize = this->readUInt16(100);
  for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
    SomfyLinkedRemote *rem = &shade->linkedRemotes[j];
    rem->setRemoteAddress(this->readUInt32(0));
    if(rem->getRemoteAddress() != 0) rem->lastRollingCode = pref.getUShort(rem->getRemotePrefId(), 0);
    if(this->header.version < 5 && j == 4) break; // Prior to version 5 we only supported 5 linked remotes.
  }
  shade->lastRollingCode = this->readUInt16(0);
  if(this->header.version > 7) shade->flags = this->readUInt8(0);
  if(shade->getRemoteAddress() != 0) {
    // If the last rolling code stored on the nvs is less than the rc we currently have
    // then we need to set it.
    uint16_t rc = pref.getUShort(shade->getRemotePrefId(), 0);
    shade->lastRollingCode = max(rc, shade->lastRollingCode);
    if(rc < shade->lastRollingCode) pref.putUShort(shade->getRemotePrefId(), shade->lastRollingCode);
  }
  if(this->header.version < 4)
    shade->myPos = static_cast<float>(this->readUInt8(255));
  else {
    shade->myPos = this->readFloat(-1);
    shade->myTiltPos = this->readFloat(-1);
  }
  if(shade->myPos > 100 || shade->myPos < 0) shade->myPos = -1;
  if(shade->myTiltPos > 100 || shade->myTiltPos < 0) shade->myTiltPos = -1;
  shade->currentPos = this->readFloat(0);
  shade->currentTiltPos = this->readFloat(0);
  if(shade->tiltType == tilt_types::none || shade->shadeType != shade_types::blind) {
    shade->myTiltPos = -1;
    shade->currentTiltPos = 0;
    shade->tiltType = tilt_types::none;
  }
  if(this->header.version < 3) {
    shade->currentPos = shade->currentPos * 100;
    shade->currentTiltPos = shade->currentTiltPos * 100;
  }
  shade->target = floor(shade->currentPos);
  shade->tiltTarget = floor(shade->currentTiltPos);
  if(this->header.version >= 9) shade->flipCommands = this->readBool(false);
  if(this->header.version >= 10) shade->flipPosition = this->readBool(false);
  if(this->header.version >= 12) shade->repeats = this->readUInt8(1);
  if(this->header.version >= 13) shade->sortOrder = this->readUInt8(shade->getShadeId() - 1);
  else shade->sortOrder = shade->getShadeId() - 1;
  if(this->header.version > 14) {
    shade->gpioUp = this->readUInt8(shade->gpioUp);
    shade->gpioDown = this->readUInt8(shade->gpioDown);
  }
  if(this->header.version > 15)
    shade->gpioMy = this->readUInt8(shade->gpioMy);
  if(this->header.version > 16)
    shade->gpioFlags = this->readUInt8(shade->gpioFlags);
  if(shade->getShadeId() == 255) shade->clear();
  else if(shade->tiltType == tilt_types::tiltonly) {
    shade->myPos = shade->currentPos = shade->target = 100.0f;
  }
  pref.end();
  if(shade->proto == radio_proto::GP_Relay || shade->proto == radio_proto::GP_Remote) {
    pinMode(shade->gpioUp, OUTPUT);
    pinMode(shade->gpioDown, OUTPUT);
  }
  if(shade->proto == radio_proto::GP_Remote)
    pinMode(shade->gpioMy, OUTPUT);
  if(this->header.version >= 19) shade->roomId = this->readUInt8(0);
  if(this->file.position() != startPos + this->header.shadeRecordSize) {
    Serial.println("Reading to end of shade record");
    this->seekChar(CFG_REC_END);
  }
  return true;
}
bool ShadeConfigFile::loadFile(SomfyShadeController *s, const char *filename) {
  bool opened = false;
  if(!this->isOpen()) {
    Serial.println("Opening shade config file");
    this->begin(filename, true);
    opened = true;
  }
  if(!this->validate()) {
    Serial.println("Shade config file invalid!");
    if(opened) this->end();
    return false;
  }
  for(uint8_t i = 0; i < this->header.roomRecords;i++) {
    this->readRoomRecord(&s->rooms[i]);
  }
  if(this->header.roomRecords < SOMFY_MAX_ROOMS) {
    uint8_t ndx = this->header.roomRecords;
    // Clear out any positions that are not in the shade file.
    while(ndx < SOMFY_MAX_ROOMS) {
      ((SomfyRoom *)&s->rooms[ndx++])->clear();
    }
  }
  
  // We should be valid so start reading.
  for(uint8_t i = 0; i < this->header.shadeRecords; i++) {
    this->readShadeRecord(&s->shades[i]);
  }
  if(this->header.shadeRecords < SOMFY_MAX_SHADES) {
    uint8_t ndx = this->header.shadeRecords;
    // Clear out any positions that are not in the shade file.
    while(ndx < SOMFY_MAX_SHADES) {
      ((SomfyShade *)&s->shades[ndx++])->clear();
    }
  }
  for(uint8_t i = 0; i < this->header.groupRecords; i++) {
    this->readGroupRecord(&s->groups[i]);
  }
  if(this->header.groupRecords < SOMFY_MAX_GROUPS) {
    uint8_t ndx = this->header.groupRecords;
    // Clear out any positions that are not in the shade file.
    while(ndx < SOMFY_MAX_GROUPS) {
      ((SomfyGroup *)&s->groups[ndx++])->clear();
    }
  }
  if(this->header.repeaterRecords > 0) {
    memset(s->repeaters, 0x00, sizeof(uint32_t) * SOMFY_MAX_REPEATERS);
    for(uint8_t i = 0; i < this->header.repeaterRecords; i++)
      this->readRepeaterRecord(s);
  }
  if(opened) {
    Serial.println("Closing shade config file");
    this->end();
  }
  return true;
}
bool ShadeConfigFile::writeGroupRecord(SomfyGroup *group) {
  this->writeUInt8(group->getGroupId());
  this->writeUInt8(static_cast<uint8_t>(group->groupType));
  this->writeUInt32(group->getRemoteAddress());
  this->writeString(group->name, sizeof(group->name));
  this->writeUInt8(static_cast<uint8_t>(group->proto));
  this->writeUInt8(group->bitLength);
  for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) {
    this->writeUInt8(group->linkedShades[j]);
  }
  this->writeUInt8(group->repeats);
  this->writeUInt8(group->sortOrder);
  this->writeBool(group->flipCommands);
  this->writeUInt8(group->roomId);
  this->writeUInt16(group->lastRollingCode, CFG_REC_END);
  return true;
}
bool ShadeConfigFile::writeRepeaterRecord(SomfyShadeController *s) {
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    this->writeUInt32(s->repeaters[i], i == SOMFY_MAX_REPEATERS - 1 ? CFG_REC_END : CFG_VALUE_SEP);
  }
  return true;
}
bool ShadeConfigFile::writeRoomRecord(SomfyRoom *room) {
  this->writeUInt8(room->roomId);
  this->writeString(room->name, sizeof(room->name));
  this->writeUInt8(room->sortOrder, CFG_REC_END);
  return true;
}
bool ShadeConfigFile::writeShadeRecord(SomfyShade *shade) {
  if(shade->tiltType == tilt_types::none || shade->shadeType != shade_types::blind) {
    shade->myTiltPos = -1;
    shade->currentTiltPos = 0;
    shade->tiltType = tilt_types::none;
  }
  this->writeUInt8(shade->getShadeId());
  this->writeBool(shade->paired);
  this->writeUInt8(static_cast<uint8_t>(shade->shadeType));
  this->writeUInt32(shade->getRemoteAddress());
  this->writeString(shade->name, sizeof(shade->name));
  this->writeUInt8(static_cast<uint8_t>(shade->tiltType));
  this->writeUInt8(static_cast<uint8_t>(shade->proto));
  this->writeUInt8(shade->bitLength);
  this->writeUInt32(shade->upTime);
  this->writeUInt32(shade->downTime);
  this->writeUInt32(shade->tiltTime);
  this->writeUInt16(shade->stepSize);
  for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
    SomfyLinkedRemote *rem = &shade->linkedRemotes[j];
    this->writeUInt32(rem->getRemoteAddress());
  }
  this->writeUInt16(shade->lastRollingCode);
  if(shade->getShadeId() != 255) {
    this->writeUInt8(shade->flags & 0xFF);
    this->writeFloat(shade->myPos, 5);
    this->writeFloat(shade->myTiltPos, 5);
    this->writeFloat(shade->currentPos, 5);
    this->writeFloat(shade->currentTiltPos, 5);
  }
  else {
    // Make sure that we write cleared values when the shade is deleted.
    this->writeUInt8(0);
    this->writeFloat(-1.0f, 5); // MyPos
    this->writeFloat(-1.0f, 5); // MyTiltPos
    this->writeFloat(0.0f, 5);  // currentPos
    this->writeFloat(0.0f, 5); // currentTiltPos
  }
  this->writeBool(shade->flipCommands);
  this->writeBool(shade->flipPosition);
  this->writeUInt8(shade->repeats);
  this->writeUInt8(shade->sortOrder);
  this->writeUInt8(shade->gpioUp);
  this->writeUInt8(shade->gpioDown);
  this->writeUInt8(shade->gpioMy);
  this->writeUInt8(shade->gpioFlags);
  this->writeUInt8(shade->roomId, CFG_REC_END);
  return true;  
}
bool ShadeConfigFile::writeSettingsRecord() {
  this->writeVarString(settings.fwVersion.name);
  this->writeVarString(settings.hostname);
  this->writeVarString(settings.NTP.ntpServer);
  this->writeVarString(settings.NTP.posixZone);
  this->writeBool(settings.ssdpBroadcast);
  this->writeBool(settings.checkForUpdate, CFG_REC_END);
  return true;
}
bool ShadeConfigFile::writeNetRecord() {
  this->writeUInt8(static_cast<uint8_t>(settings.connType));
  this->writeBool(settings.IP.dhcp); 
  this->writeVarString(settings.IP.ip.toString().c_str());
  this->writeVarString(settings.IP.gateway.toString().c_str());
  this->writeVarString(settings.IP.subnet.toString().c_str());
  this->writeVarString(settings.IP.dns1.toString().c_str());
  this->writeVarString(settings.IP.dns2.toString().c_str());
  this->writeVarString(settings.MQTT.protocol);
  this->writeVarString(settings.MQTT.hostname);
  this->writeUInt16(settings.MQTT.port);
  this->writeBool(settings.MQTT.pubDisco);
  this->writeVarString(settings.MQTT.rootTopic);
  this->writeVarString(settings.MQTT.discoTopic);
  this->writeUInt8(settings.Ethernet.boardType);
  this->writeUInt8(static_cast<uint8_t>(settings.Ethernet.phyType));
  this->writeUInt8(static_cast<uint8_t>(settings.Ethernet.CLKMode));
  this->writeInt8(settings.Ethernet.phyAddress);
  this->writeInt8(settings.Ethernet.PWRPin);
  this->writeInt8(settings.Ethernet.MDCPin);
  this->writeInt8(settings.Ethernet.MDIOPin, CFG_REC_END);
  return true;
}
bool ShadeConfigFile::writeTransRecord(transceiver_config_t &cfg) {
  this->writeBool(cfg.enabled);
  this->writeUInt8(static_cast<uint8_t>(cfg.proto));
  this->writeUInt8(cfg.type);
  this->writeUInt8(cfg.SCKPin);
  this->writeUInt8(cfg.CSNPin);
  this->writeUInt8(cfg.MOSIPin);
  this->writeUInt8(cfg.MISOPin);
  this->writeUInt8(cfg.TXPin);
  this->writeUInt8(cfg.RXPin);
  this->writeFloat(cfg.frequency, 3);
  this->writeFloat(cfg.rxBandwidth, 2);
  this->writeFloat(cfg.deviation, 2);
  this->writeInt8(cfg.txPower, CFG_REC_END);
  return true;
}
bool ShadeConfigFile::exists() { return LittleFS.exists("/shades.cfg"); }
