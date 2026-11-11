#include "ConfigRecords.h"
#include "Utils.h"

bool ConfigReader::next(uint8_t &val) {
  if(!this->readByte(val)) return false;
  this->_last = val;
  return true;
}
void ConfigReader::endRecord() {
  if(!this->atRecordEnd()) this->seekChar(CFG_REC_END);
}
bool ConfigReader::seekChar(const char val) {
  uint8_t ch;
  do {
    if(!this->next(ch)) return false;
  } while(ch != static_cast<uint8_t>(val));
  return true;
}
bool ConfigReader::readString(char *buff, size_t len) {
  memset(buff, 0x00, len);
  if(this->atRecordEnd()) return false;
  size_t i = 0;
  uint8_t val;
  while(this->next(val)) {
    if(val == CFG_REC_END || val == CFG_VALUE_SEP) {
      _rtrim(buff);
      return true;
    }
    if(i < len - 1) buff[i++] = val;
  }
  return false;
}
bool ConfigReader::readFixedString(char *buff, size_t len) {
  memset(buff, 0x00, len);
  if(this->atRecordEnd()) return false;
  uint8_t val;
  for(size_t i = 0; i < len - 1; i++) {
    if(!this->next(val)) return false;
    if(val == CFG_REC_END) {
      _rtrim(buff);
      return true;
    }
    buff[i] = val;
  }
  // The separator after the padding.
  if(!this->next(val)) return false;
  _rtrim(buff);
  return true;
}
bool ConfigReader::readVarString(char *buff, size_t len) {
  memset(buff, 0x00, len);
  if(this->atRecordEnd()) return false;
  uint8_t quotes = 0;
  size_t i = 0;
  uint8_t val;
  while(this->next(val)) {
    if(val == CFG_REC_END || (val == CFG_VALUE_SEP && quotes >= 2)) {
      _rtrim(buff);
      return true;
    }
    if(val == CFG_TOK_QUOTE) {
      quotes++;
      continue;
    }
    if(i < len - 1) buff[i++] = val;
  }
  return false;
}
int8_t ConfigReader::readInt8(const int8_t defVal) {
  char buff[5];
  if(this->readString(buff, sizeof(buff))) return static_cast<int8_t>(atoi(buff));
  return defVal;
}
uint8_t ConfigReader::readUInt8(const uint8_t defVal) {
  char buff[4];
  if(this->readString(buff, sizeof(buff))) return static_cast<uint8_t>(atoi(buff));
  return defVal;
}
uint16_t ConfigReader::readUInt16(const uint16_t defVal) {
  char buff[6];
  if(this->readString(buff, sizeof(buff))) return static_cast<uint16_t>(atoi(buff));
  return defVal;
}
uint32_t ConfigReader::readUInt32(const uint32_t defVal) {
  char buff[11];
  if(this->readString(buff, sizeof(buff))) return static_cast<uint32_t>(strtoul(buff, nullptr, 10));
  return defVal;
}
float ConfigReader::readFloat(const float defVal) {
  char buff[25];
  if(this->readString(buff, sizeof(buff))) return atof(buff);
  return defVal;
}
bool ConfigReader::readBool(const bool defVal) {
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

bool ConfigWriter::writeChar(const char val) {
  const uint8_t b = static_cast<uint8_t>(val);
  return this->writeBytes(&b, 1);
}
bool ConfigWriter::writeString(const char *val, size_t len, const char tok) {
  size_t slen = strlen(val);
  if(slen > 0 && !this->writeBytes(reinterpret_cast<const uint8_t *>(val), slen)) return false;
  // Pad the value to its fixed width.
  while(slen + 1 < len) {
    if(!this->writeChar(' ')) return false;
    slen++;
  }
  if(tok != CFG_TOK_NONE) return this->writeChar(tok);
  return true;
}
bool ConfigWriter::writeVarString(const char *val, const char tok) {
  this->writeChar(CFG_TOK_QUOTE);
  const size_t slen = strlen(val);
  if(slen > 0 && !this->writeBytes(reinterpret_cast<const uint8_t *>(val), slen)) return false;
  this->writeChar(CFG_TOK_QUOTE);
  if(tok != CFG_TOK_NONE) return this->writeChar(tok);
  return true;
}
bool ConfigWriter::writeInt8(const int8_t val, const char tok) {
  char buff[5];
  snprintf(buff, sizeof(buff), "%4d", val);
  return this->writeString(buff, sizeof(buff), tok);
}
bool ConfigWriter::writeUInt8(const uint8_t val, const char tok) {
  char buff[4];
  snprintf(buff, sizeof(buff), "%3u", val);
  return this->writeString(buff, sizeof(buff), tok);
}
bool ConfigWriter::writeUInt16(const uint16_t val, const char tok) {
  char buff[6];
  snprintf(buff, sizeof(buff), "%5u", val);
  return this->writeString(buff, sizeof(buff), tok);
}
bool ConfigWriter::writeUInt32(const uint32_t val, const char tok) {
  char buff[11];
  snprintf(buff, sizeof(buff), "%10u", static_cast<unsigned>(val));
  return this->writeString(buff, sizeof(buff), tok);
}
bool ConfigWriter::writeFloat(const float val, const uint8_t prec, const char tok) {
  char buff[20];
  snprintf(buff, sizeof(buff), "%*.*f", 7 + prec, prec, val);
  return this->writeString(buff, 8 + prec, tok);
}
bool ConfigWriter::writeBool(const bool val, const char tok) {
  return this->writeString(val ? "true" : "false", 6, tok);
}

bool writeConfigHeader(ConfigWriter &w, const config_header_t &header) {
  w.writeUInt8(header.version);
  w.writeUInt8(header.length);
  w.writeUInt16(header.roomRecordSize);
  w.writeUInt8(header.roomRecords);
  w.writeUInt16(header.shadeRecordSize);
  w.writeUInt8(header.shadeRecords);
  w.writeUInt16(header.groupRecordSize);
  w.writeUInt8(header.groupRecords);
  w.writeUInt16(header.repeaterRecordSize);
  w.writeUInt8(header.repeaterRecords);
  w.writeUInt16(header.settingsRecordSize);
  w.writeUInt16(header.netRecordSize);
  w.writeUInt16(header.transRecordSize);
  return w.writeString(header.serverId, sizeof(header.serverId), CFG_REC_END);
}
bool readConfigHeader(ConfigReader &r, config_header_t &header) {
  r.beginRecord();
  header.version = r.readUInt8(header.version);
  header.length = r.readUInt8(0);
  if(header.version >= 19) {
    header.roomRecordSize = r.readUInt16(header.roomRecordSize);
    header.roomRecords = r.readUInt8(header.roomRecords);
  }
  if(header.version >= 13) header.shadeRecordSize = r.readUInt16(header.shadeRecordSize);
  else header.shadeRecordSize = r.readUInt8(static_cast<uint8_t>(header.shadeRecordSize));
  header.shadeRecords = r.readUInt8(header.shadeRecords);
  if(header.version > 10) {
    if(header.version >= 13) header.groupRecordSize = r.readUInt16(header.groupRecordSize);
    else header.groupRecordSize = r.readUInt8(header.groupRecordSize);
    header.groupRecords = r.readUInt8(header.groupRecords);
  }
  if(header.version >= 21) {
    header.repeaterRecordSize = r.readUInt16(header.repeaterRecordSize);
    header.repeaterRecords = r.readUInt8(header.repeaterRecords);
  }
  if(header.version > 13) {
    header.settingsRecordSize = r.readUInt16(header.settingsRecordSize);
    header.netRecordSize = r.readUInt16(header.netRecordSize);
    header.transRecordSize = r.readUInt16(header.transRecordSize);
    r.readString(header.serverId, sizeof(header.serverId));
  }
  return true;
}

bool writeRoomRecord(ConfigWriter &w, const room_record_t &rec) {
  w.writeUInt8(rec.roomId);
  w.writeString(rec.name, sizeof(rec.name));
  return w.writeUInt8(static_cast<uint8_t>(rec.sortOrder), CFG_REC_END);
}
bool readRoomRecord(ConfigReader &r, room_record_t &rec) {
  r.beginRecord();
  rec.roomId = r.readUInt8(0);
  r.readFixedString(rec.name, sizeof(rec.name));
  rec.sortOrder = static_cast<int8_t>(r.readUInt8(rec.roomId - 1));
  r.endRecord();
  return true;
}

bool writeShadeRecord(ConfigWriter &w, const shade_record_t &rec) {
  w.writeUInt8(rec.shadeId);
  w.writeBool(rec.paired);
  w.writeUInt8(static_cast<uint8_t>(rec.shadeType));
  w.writeUInt32(rec.remoteAddress);
  w.writeString(rec.name, sizeof(rec.name));
  w.writeUInt8(static_cast<uint8_t>(rec.tiltType));
  w.writeUInt8(static_cast<uint8_t>(rec.proto));
  w.writeUInt8(rec.bitLength);
  w.writeUInt32(rec.upTime);
  w.writeUInt32(rec.downTime);
  w.writeUInt32(rec.tiltTime);
  w.writeUInt16(rec.stepSize);
  for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) w.writeUInt32(rec.linkedRemotes[j]);
  w.writeUInt16(rec.lastRollingCode);
  if(rec.shadeId != 255) {
    w.writeUInt8(rec.flags);
    w.writeFloat(rec.myPos, 5);
    w.writeFloat(rec.myTiltPos, 5);
    w.writeFloat(rec.currentPos, 5);
    w.writeFloat(rec.currentTiltPos, 5);
  }
  else {
    // A deleted shade is written cleared.
    w.writeUInt8(0);
    w.writeFloat(-1.0f, 5);
    w.writeFloat(-1.0f, 5);
    w.writeFloat(0.0f, 5);
    w.writeFloat(0.0f, 5);
  }
  w.writeBool(rec.flipCommands);
  w.writeBool(rec.flipPosition);
  w.writeUInt8(rec.repeats);
  w.writeUInt8(static_cast<uint8_t>(rec.sortOrder));
  w.writeUInt8(rec.gpioUp);
  w.writeUInt8(rec.gpioDown);
  w.writeUInt8(rec.gpioMy);
  w.writeUInt8(rec.gpioFlags);
  return w.writeUInt8(rec.roomId, CFG_REC_END);
}
bool readShadeRecord(ConfigReader &r, const config_header_t &header, shade_record_t &rec) {
  const uint8_t ver = header.version;
  r.beginRecord();
  rec.shadeId = r.readUInt8(255);
  rec.paired = r.readBool(false);
  rec.shadeType = static_cast<shade_types>(r.readUInt8(0));
  rec.remoteAddress = r.readUInt32(0);
  r.readFixedString(rec.name, sizeof(rec.name));
  if(ver < 3) rec.tiltType = r.readBool(false) ? tilt_types::none : tilt_types::tiltmotor;
  else rec.tiltType = static_cast<tilt_types>(r.readUInt8(0));
  if(ver > 6) rec.proto = static_cast<radio_proto>(r.readUInt8(0));
  if(ver > 1) rec.bitLength = r.readUInt8(56);
  rec.upTime = r.readUInt32(rec.upTime);
  rec.downTime = r.readUInt32(rec.downTime);
  rec.tiltTime = r.readUInt32(rec.tiltTime);
  if(ver > 5) rec.stepSize = r.readUInt16(100);
  // Before version 5 a shade had 5 linked remotes.
  const uint8_t remotes = ver < 5 ? 5 : SOMFY_MAX_LINKED_REMOTES;
  for(uint8_t j = 0; j < remotes; j++) rec.linkedRemotes[j] = r.readUInt32(0);
  rec.lastRollingCode = r.readUInt16(0);
  if(ver > 7) rec.flags = r.readUInt8(0);
  if(ver < 4) rec.myPos = static_cast<float>(r.readUInt8(255));
  else {
    rec.myPos = r.readFloat(-1);
    rec.myTiltPos = r.readFloat(-1);
  }
  if(rec.myPos > 100 || rec.myPos < 0) rec.myPos = -1;
  if(rec.myTiltPos > 100 || rec.myTiltPos < 0) rec.myTiltPos = -1;
  rec.currentPos = r.readFloat(0);
  rec.currentTiltPos = r.readFloat(0);
  if(rec.tiltType == tilt_types::none || rec.shadeType != shade_types::blind) {
    rec.myTiltPos = -1;
    rec.currentTiltPos = 0;
    rec.tiltType = tilt_types::none;
  }
  if(ver < 3) {
    // Positions were fractions.
    rec.currentPos = rec.currentPos * 100;
    rec.currentTiltPos = rec.currentTiltPos * 100;
  }
  if(ver >= 9) rec.flipCommands = r.readBool(false);
  if(ver >= 10) rec.flipPosition = r.readBool(false);
  if(ver >= 12) rec.repeats = r.readUInt8(1);
  if(ver >= 13) rec.sortOrder = static_cast<int8_t>(r.readUInt8(rec.shadeId - 1));
  else rec.sortOrder = static_cast<int8_t>(rec.shadeId - 1);
  if(ver > 14) {
    rec.gpioUp = r.readUInt8(rec.gpioUp);
    rec.gpioDown = r.readUInt8(rec.gpioDown);
  }
  if(ver > 15) rec.gpioMy = r.readUInt8(rec.gpioMy);
  if(ver > 16) rec.gpioFlags = r.readUInt8(rec.gpioFlags);
  if(rec.shadeId != 255 && rec.tiltType == tilt_types::tiltonly) {
    // A tilt only shade is always all the way down.
    rec.currentPos = 100.0f;
    rec.myPos = 100.0f;
  }
  if(ver >= 19) rec.roomId = r.readUInt8(0);
  r.endRecord();
  return true;
}

bool writeGroupRecord(ConfigWriter &w, const group_record_t &rec) {
  w.writeUInt8(rec.groupId);
  w.writeUInt8(static_cast<uint8_t>(rec.groupType));
  w.writeUInt32(rec.remoteAddress);
  w.writeString(rec.name, sizeof(rec.name));
  w.writeUInt8(static_cast<uint8_t>(rec.proto));
  w.writeUInt8(rec.bitLength);
  for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) w.writeUInt8(rec.linkedShades[j]);
  w.writeUInt8(rec.repeats);
  w.writeUInt8(static_cast<uint8_t>(rec.sortOrder));
  w.writeBool(rec.flipCommands);
  w.writeUInt8(rec.roomId);
  return w.writeUInt16(rec.lastRollingCode, CFG_REC_END);
}
bool readGroupRecord(ConfigReader &r, const config_header_t &header, group_record_t &rec) {
  const uint8_t ver = header.version;
  r.beginRecord();
  rec.groupId = r.readUInt8(255);
  rec.groupType = static_cast<group_types>(r.readUInt8(0));
  rec.remoteAddress = r.readUInt32(0);
  r.readFixedString(rec.name, sizeof(rec.name));
  rec.proto = static_cast<radio_proto>(r.readUInt8(0));
  rec.bitLength = r.readUInt8(56);
  // Version 23 put the rolling code here, version 24 at the end.
  if(ver == 23) rec.lastRollingCode = r.readUInt16(0);
  rec.hasRollingCode = ver >= 23;
  memset(rec.linkedShades, 0x00, sizeof(rec.linkedShades));
  uint8_t linked = 0;
  for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) {
    const uint8_t shadeId = r.readUInt8(0);
    if(shadeId > 0) rec.linkedShades[linked++] = shadeId;
  }
  if(ver >= 12) rec.repeats = r.readUInt8(1);
  if(ver >= 13) rec.sortOrder = static_cast<int8_t>(r.readUInt8(rec.groupId - 1));
  else rec.sortOrder = static_cast<int8_t>(rec.groupId - 1);
  if(ver >= 18) rec.flipCommands = r.readBool(false);
  if(ver >= 19) rec.roomId = r.readUInt8(0);
  if(ver >= 24) rec.lastRollingCode = r.readUInt16(0);
  r.endRecord();
  return true;
}

bool writeRepeaterRecord(ConfigWriter &w, const uint32_t (&repeaters)[SOMFY_MAX_REPEATERS]) {
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++)
    w.writeUInt32(repeaters[i], i == SOMFY_MAX_REPEATERS - 1 ? CFG_REC_END : CFG_VALUE_SEP);
  return true;
}
bool readRepeaterRecord(ConfigReader &r, uint32_t (&repeaters)[SOMFY_MAX_REPEATERS]) {
  r.beginRecord();
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) repeaters[i] = r.readUInt32(0);
  r.endRecord();
  return true;
}

bool writeSettingsRecord(ConfigWriter &w, const settings_record_t &rec) {
  w.writeVarString(rec.fwVersion);
  w.writeVarString(rec.hostname);
  w.writeVarString(rec.ntpServer);
  w.writeVarString(rec.posixZone);
  w.writeBool(rec.ssdpBroadcast);
  return w.writeBool(rec.checkForUpdate, CFG_REC_END);
}
bool readSettingsRecord(ConfigReader &r, const config_header_t &header, settings_record_t &rec) {
  r.beginRecord();
  r.readVarString(rec.fwVersion, sizeof(rec.fwVersion));
  r.readVarString(rec.hostname, sizeof(rec.hostname));
  r.readVarString(rec.ntpServer, sizeof(rec.ntpServer));
  r.readVarString(rec.posixZone, sizeof(rec.posixZone));
  rec.ssdpBroadcast = r.readBool(false);
  if(header.version >= 20) rec.checkForUpdate = r.readBool(true);
  r.endRecord();
  return true;
}

bool writeNetRecord(ConfigWriter &w, const net_record_t &rec, bool withEthernet) {
  w.writeUInt8(rec.connType);
  w.writeBool(rec.dhcp);
  w.writeVarString(rec.ip);
  w.writeVarString(rec.gateway);
  w.writeVarString(rec.subnet);
  w.writeVarString(rec.dns1);
  w.writeVarString(rec.dns2);
  w.writeVarString(rec.mqttProtocol);
  w.writeVarString(rec.mqttHostname);
  w.writeUInt16(rec.mqttPort);
  w.writeBool(rec.mqttPubDisco);
  w.writeVarString(rec.mqttRootTopic);
  w.writeVarString(rec.mqttDiscoTopic);
  if(!withEthernet) return w.writeUInt8(0, CFG_REC_END);
  w.writeUInt8(rec.ethBoardType);
  w.writeUInt8(rec.ethPhyType);
  w.writeUInt8(rec.ethClkMode);
  w.writeInt8(rec.ethPhyAddress);
  w.writeInt8(rec.ethPWRPin);
  w.writeInt8(rec.ethMDCPin);
  return w.writeInt8(rec.ethMDIOPin, CFG_REC_END);
}
bool readNetRecord(ConfigReader &r, const config_header_t &header, net_record_t &rec, bool withEthernet) {
  r.beginRecord();
  rec.connType = r.readUInt8(0);
  rec.dhcp = r.readBool(true);
  r.readVarString(rec.ip, sizeof(rec.ip));
  r.readVarString(rec.gateway, sizeof(rec.gateway));
  r.readVarString(rec.subnet, sizeof(rec.subnet));
  r.readVarString(rec.dns1, sizeof(rec.dns1));
  r.readVarString(rec.dns2, sizeof(rec.dns2));
  if(header.version >= 22) {
    r.readVarString(rec.mqttProtocol, sizeof(rec.mqttProtocol));
    r.readVarString(rec.mqttHostname, sizeof(rec.mqttHostname));
    rec.mqttPort = r.readUInt16(1883);
    rec.mqttPubDisco = r.readBool(false);
    r.readVarString(rec.mqttRootTopic, sizeof(rec.mqttRootTopic));
    r.readVarString(rec.mqttDiscoTopic, sizeof(rec.mqttDiscoTopic));
  }
  if(withEthernet) {
    rec.ethBoardType = r.readUInt8(1);
    rec.ethPhyType = r.readUInt8(0);
    rec.ethClkMode = r.readUInt8(0);
    rec.ethPhyAddress = r.readInt8(1);
    rec.ethPWRPin = r.readInt8(1);
    rec.ethMDCPin = r.readInt8(16);
    rec.ethMDIOPin = r.readInt8(23);
  }
  r.endRecord();
  return true;
}

bool writeTransRecord(ConfigWriter &w, const trans_record_t &rec) {
  w.writeBool(rec.enabled);
  w.writeUInt8(static_cast<uint8_t>(rec.proto));
  w.writeUInt8(rec.type);
  w.writeUInt8(rec.SCKPin);
  w.writeUInt8(rec.CSNPin);
  w.writeUInt8(rec.MOSIPin);
  w.writeUInt8(rec.MISOPin);
  w.writeUInt8(rec.TXPin);
  w.writeUInt8(rec.RXPin);
  w.writeFloat(rec.frequency, 3);
  w.writeFloat(rec.rxBandwidth, 2);
  w.writeFloat(rec.deviation, 2);
  w.writeInt8(rec.txPower);
  return w.writeBool(rec.noiseDetection, CFG_REC_END);
}
bool readTransRecord(ConfigReader &r, trans_record_t &rec) {
  r.beginRecord();
  rec.enabled = r.readBool(false);
  rec.proto = static_cast<radio_proto>(r.readUInt8(0));
  rec.type = r.readUInt8(56);
  rec.SCKPin = r.readUInt8(rec.SCKPin);
  rec.CSNPin = r.readUInt8(rec.CSNPin);
  rec.MOSIPin = r.readUInt8(rec.MOSIPin);
  rec.MISOPin = r.readUInt8(rec.MISOPin);
  rec.TXPin = r.readUInt8(rec.TXPin);
  rec.RXPin = r.readUInt8(rec.RXPin);
  rec.frequency = r.readFloat(rec.frequency);
  rec.rxBandwidth = r.readFloat(rec.rxBandwidth);
  rec.deviation = r.readFloat(rec.deviation);
  rec.txPower = r.readInt8(rec.txPower);
  rec.noiseDetection = r.readBool(false);
  r.endRecord();
  return true;
}

bool skipRecords(ConfigReader &r, uint32_t count) {
  for(uint32_t i = 0; i < count; i++) {
    r.beginRecord();
    if(!r.seekChar(CFG_REC_END)) return false;
  }
  return true;
}
