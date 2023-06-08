#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "ConfigFile.h"
#include "Utils.h"

extern Preferences pref;

#define SHADE_HDR_VER 8
#define SHADE_HDR_SIZE 16
#define SHADE_REC_SIZE 236

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
  this->writeUInt8(hdr.recordSize);
  this->writeUInt8(hdr.records, CFG_REC_END);
  return true;
}
bool ConfigFile::readHeader() {
  if(!this->isOpen()) return false;
  //if(this->file.position() != 0) this->file.seek(0, SeekSet);
  Serial.printf("Reading header at %u\n", this->file.position());
  this->header.version = this->readUInt8(this->header.version);
  this->header.length = this->readUInt8(0);
  this->header.recordSize = this->readUInt8(this->header.recordSize);
  this->header.records = this->readUInt8(this->header.records);
  Serial.printf("version:%u len:%u size:%u recs:%u pos:%d\n", this->header.version, this->header.length, this->header.recordSize, this->header.records, this->file.position());
  return true;
}
bool ConfigFile::seekRecordByIndex(uint16_t ndx) {
  if(!this->file) {
    return false;
  }
  if(((this->header.recordSize * ndx) + this->header.length) > this->file.size()) return false;
  return true;
}
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
bool ConfigFile::writeChar(const char val) {
  if(!this->isOpen()) return false;
  if(this->file.write(static_cast<uint8_t>(val)) == 1) return true;
  return false;
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
bool ShadeConfigFile::begin(bool readOnly) { return this->begin("/shades.cfg", readOnly); }
bool ShadeConfigFile::begin(const char *filename, bool readOnly) { return ConfigFile::begin(filename, readOnly); }
void ShadeConfigFile::end() { ConfigFile::end(); }
bool ShadeConfigFile::save(SomfyShadeController *s) {
  this->header.version = SHADE_HDR_VER;
  this->header.recordSize = SHADE_REC_SIZE;
  this->header.length = SHADE_HDR_SIZE;
  this->header.records = SOMFY_MAX_SHADES;
  this->writeHeader();
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &s->shades[i];
    this->writeShadeRecord(shade);
  }
  return true;
}
bool ShadeConfigFile::validate() {
  this->readHeader();
  if(this->header.version < 1) {
    Serial.print("Invalid Header Version:");
    Serial.println(this->header.version);
    return false;
  }
  if(this->header.recordSize < 100) {
    Serial.print("Invalid Record Size:");
    Serial.println(this->header.recordSize);
    return false;
  }
  if(this->header.records != 32) {
    Serial.print("Invalid Record Count:");
    Serial.println(this->header.records);
    return false;
  }
  if(this->file.position() != this->header.length) {
    Serial.printf("File not positioned at %u end of header: %d\n", this->header.length, this->file.position());
    return false;
  }
  // We should know the file size based upon the record information in the header
  if(this->file.size() != this->header.length + (this->header.recordSize * this->header.records)) {
    Serial.printf("File size is not correct should be %d and got %d\n", this->header.length + (this->header.recordSize * this->header.records), this->file.size());
  }
  // Next check to see if the records match the header length.
  uint8_t recs = 0;
  uint32_t startPos = this->file.position();
  while(recs < this->header.records) {
    uint32_t pos = this->file.position();
    if(!this->seekChar(CFG_REC_END)) {
      Serial.printf("Failed to find the record end %d\n", recs);
      return false;
    }
    if(this->file.position() - pos != this->header.recordSize) {
      Serial.printf("Record length is %d and should be %d\n", this->file.position() - pos, this->header.recordSize);
      return false;
    }
    recs++;
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
bool ShadeConfigFile::loadFile(SomfyShadeController *s, const char *filename) {
  bool opened = false;
  if(!this->isOpen()) {
    Serial.println("Opening shade config file");
    this->begin(filename, true);
    opened = true;
  }
  else {
    //this->file.seek(0, SeekSet);
  }
  if(!this->validate()) {
    Serial.println("Shade config file invalid!");
    if(opened) this->end();
    return false;
  }
  // We should be valid so start reading.
  pref.begin("ShadeCodes");
  for(uint8_t i = 0; i < this->header.records; i++) {
    SomfyShade *shade = &s->shades[i];
    shade->setShadeId(this->readUInt8(255));
    shade->paired = this->readBool(false);
    shade->shadeType = static_cast<shade_types>(this->readUInt8(0));
    shade->setRemoteAddress(this->readUInt32(0));
    this->readString(shade->name, sizeof(shade->name));
    if(this->header.version < 3)
      shade->tiltType = this->readBool(false) ? tilt_types::none : tilt_types::tiltmotor;
    else
      shade->tiltType = static_cast<tilt_types>(this->readUInt8(0));
    if(this->header.version > 6) {
      shade->proto = static_cast<radio_proto>(this->readUInt8(0));
    }
    if(this->header.version > 1) {
      shade->bitLength = this->readUInt8(56);
    }
    shade->upTime = this->readUInt32(shade->upTime);
    shade->downTime = this->readUInt32(shade->downTime);
    shade->tiltTime = this->readUInt32(shade->tiltTime);
    if(this->header.version > 5) {
      shade->stepSize = this->readUInt16(100);
    }
    for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
      SomfyLinkedRemote *rem = &shade->linkedRemotes[j];
      rem->setRemoteAddress(this->readUInt32(0));
      if(rem->getRemoteAddress() != 0) rem->lastRollingCode = pref.getUShort(rem->getRemotePrefId(), 0);
      if(this->header.version < 5 && j == 4) break; // Prior to version 5 we only supported 5 linked remotes.
    }
    shade->lastRollingCode = this->readUInt16(0);
    if(this->header.version > 7) shade->flags = this->readUInt8(0);
    if(shade->getRemoteAddress() != 0) shade->lastRollingCode = max(pref.getUShort(shade->getRemotePrefId(), shade->lastRollingCode), shade->lastRollingCode);
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
  }
  pref.end();
  if(opened) {
    Serial.println("Closing shade config file");
    this->end();
  }
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
  this->writeUInt8(shade->flags & static_cast<uint8_t>(somfy_flags_t::SunFlag));
  this->writeFloat(shade->myPos, 5);
  this->writeFloat(shade->myTiltPos, 5);
  this->writeFloat(shade->currentPos, 5);
  this->writeFloat(shade->currentTiltPos, 5, CFG_REC_END);
  return true;  
}
bool ShadeConfigFile::exists() { return LittleFS.exists("/shades.cfg"); }
bool ShadeConfigFile::getAppVersion(appver_t &ver) {
  char app[15];
  if(!LittleFS.exists("/appversion")) return false;
  File f = LittleFS.open("/appversion", "r");
  memset(app, 0x00, sizeof(app));
  f.read((uint8_t *)app, sizeof(app) - 1);
  f.close();
  // Now lets parse this pig.
  memset(&ver, 0x00, sizeof(appver_t));
  char num[3];
  uint8_t i = 0;
  for(uint8_t j = 0; j < 3 && i < strlen(app); j++) {
    char ch = app[i++];
    if(ch != '.')
      num[j] = ch;
    else
      break;
  }
  ver.major = static_cast<uint8_t>(atoi(num) & 0xFF);
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 3 && i < strlen(app); j++) {
    char ch = app[i++];
    if(ch != '.')
      num[j] = ch;
    else
      break;
  }
  ver.minor = static_cast<uint8_t>(atoi(num) & 0xFF);
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 3 && i < strlen(app); j++) {
    char ch = app[i++];
    if(ch != '.')
      num[j] = ch;
    else
      break;
  }
  ver.build = static_cast<uint8_t>(atoi(num) & 0xFF);
  return true;
}
