#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "esp_log.h"
#include "ConfigFile.h"
#include "Utils.h"
#include "ConfigSettings.h"
#include "RollingCode.h"

static const char *TAG = "ConfigFile";

extern ConfigSettings settings;

// The records' strings are read straight into these fields.
static_assert(CFG_NAME_LEN == sizeof(SomfyShade::name), "shade name size");
static_assert(CFG_NAME_LEN == sizeof(SomfyGroup::name), "group name size");
static_assert(CFG_NAME_LEN == sizeof(SomfyRoom::name), "room name size");
static_assert(CFG_HOSTNAME_LEN == sizeof(ConfigSettings::hostname), "hostname size");
static_assert(CFG_NTP_SERVER_LEN == sizeof(NTPSettings::ntpServer), "NTP server size");
static_assert(CFG_POSIX_ZONE_LEN == sizeof(NTPSettings::posixZone), "time zone size");
static_assert(CFG_MQTT_PROTOCOL_LEN == sizeof(MQTTSettings::protocol), "MQTT protocol size");
static_assert(CFG_MQTT_HOSTNAME_LEN == sizeof(MQTTSettings::hostname), "MQTT hostname size");
static_assert(CFG_MQTT_TOPIC_LEN == sizeof(MQTTSettings::rootTopic), "MQTT root topic size");
static_assert(CFG_MQTT_TOPIC_LEN == sizeof(MQTTSettings::discoTopic), "MQTT discovery topic size");

bool ConfigFile::begin(const char* filename, bool readOnly) {
  this->file = LittleFS.open(filename, readOnly ? "r" : "w");
  this->_opened = true;
  return true;
}
bool ConfigFile::beginRAM(String *buf) {
  _ramBuf = buf;
  _opened = true;
  return true;
}
void ConfigFile::end() {
  if(this->isOpen()) {
    if(_ramBuf) { _ramBuf = nullptr; }
    else { if(!this->readOnly) this->file.flush(); this->file.close(); }
  }
  this->_opened = false;
}
bool ConfigFile::isOpen() { return this->_opened; }
bool ConfigFile::readByte(uint8_t &val) {
  if(!this->file) return false;
  return this->file.read(&val, 1) == 1;
}
uint32_t ConfigFile::position() { return this->file.position(); }
bool ConfigFile::writeBytes(const uint8_t *buf, size_t len) {
  if(!this->isOpen()) return false;
  if(_ramBuf) {
    for(size_t i = 0; i < len; i++) _ramBuf->concat(static_cast<char>(buf[i]));
    return true;
  }
  return this->file.write(buf, len) == len;
}
bool ConfigFile::writeHeader() {
  if(!this->isOpen()) return false;
  return writeConfigHeader(*this, this->header);
}
bool ConfigFile::readHeader() {
  if(!this->isOpen()) return false;
  readConfigHeader(*this, this->header);
  ESP_LOGD(TAG, "version:%u len:%u roomSize:%u roomRecs:%u shadeSize:%u shadeRecs:%u groupSize:%u groupRecs: %u pos:%d", this->header.version, this->header.length, this->header.roomRecordSize, this->header.roomRecords, this->header.shadeRecordSize, this->header.shadeRecords, this->header.groupRecordSize, this->header.groupRecords, this->file.position());
  return true;
}

// Between the shade model and its records.
static room_record_t roomRecord(const SomfyRoom *room) {
  room_record_t rec;
  rec.roomId = room->roomId;
  strlcpy(rec.name, room->name, sizeof(rec.name));
  rec.sortOrder = room->sortOrder;
  return rec;
}
static shade_record_t shadeRecord(SomfyShade *shade) {
  shade_record_t rec;
  rec.shadeId = shade->getShadeId();
  rec.paired = shade->paired;
  rec.shadeType = shade->shadeType;
  rec.remoteAddress = shade->getRemoteAddress();
  strlcpy(rec.name, shade->name, sizeof(rec.name));
  rec.tiltType = shade->tiltType;
  rec.proto = shade->proto;
  rec.bitLength = shade->bitLength;
  rec.upTime = shade->upTime;
  rec.downTime = shade->downTime;
  rec.tiltTime = shade->tiltTime;
  rec.stepSize = shade->stepSize;
  for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) rec.linkedRemotes[j] = shade->linkedRemotes[j].getRemoteAddress();
  rec.lastRollingCode = shade->lastRollingCode;
  rec.flags = shade->getFlags();
  rec.myPos = shade->state().myPos;
  rec.myTiltPos = shade->state().myTiltPos;
  rec.currentPos = shade->state().currentPos;
  rec.currentTiltPos = shade->state().currentTiltPos;
  rec.flipCommands = shade->flipCommands;
  rec.flipPosition = shade->flipPosition;
  rec.repeats = shade->repeats;
  rec.sortOrder = shade->sortOrder;
  rec.gpioUp = shade->gpioUp;
  rec.gpioDown = shade->gpioDown;
  rec.gpioMy = shade->gpioMy;
  rec.gpioFlags = shade->gpioFlags;
  rec.roomId = shade->roomId;
  return rec;
}
static group_record_t groupRecord(SomfyGroup *group) {
  group_record_t rec;
  rec.groupId = group->getGroupId();
  rec.groupType = group->groupType;
  rec.remoteAddress = group->getRemoteAddress();
  strlcpy(rec.name, group->name, sizeof(rec.name));
  rec.proto = group->proto;
  rec.bitLength = group->bitLength;
  memcpy(rec.linkedShades, group->linkedShades, sizeof(rec.linkedShades));
  rec.repeats = group->repeats;
  rec.sortOrder = group->sortOrder;
  rec.flipCommands = group->flipCommands;
  rec.roomId = group->roomId;
  rec.lastRollingCode = group->lastRollingCode;
  return rec;
}
static settings_record_t settingsRecord() {
  settings_record_t rec;
  strlcpy(rec.fwVersion, settings.fwVersion.name, sizeof(rec.fwVersion));
  strlcpy(rec.hostname, settings.hostname, sizeof(rec.hostname));
  strlcpy(rec.ntpServer, settings.NTP.ntpServer, sizeof(rec.ntpServer));
  strlcpy(rec.posixZone, settings.NTP.posixZone, sizeof(rec.posixZone));
  rec.ssdpBroadcast = settings.ssdpBroadcast;
  rec.checkForUpdate = settings.checkForUpdate;
  return rec;
}
static net_record_t netRecord() {
  net_record_t rec;
  rec.connType = static_cast<uint8_t>(settings.connType);
  rec.dhcp = settings.IP.dhcp;
  strlcpy(rec.ip, settings.IP.ip.toString().c_str(), sizeof(rec.ip));
  strlcpy(rec.gateway, settings.IP.gateway.toString().c_str(), sizeof(rec.gateway));
  strlcpy(rec.subnet, settings.IP.subnet.toString().c_str(), sizeof(rec.subnet));
  strlcpy(rec.dns1, settings.IP.dns1.toString().c_str(), sizeof(rec.dns1));
  strlcpy(rec.dns2, settings.IP.dns2.toString().c_str(), sizeof(rec.dns2));
  strlcpy(rec.mqttProtocol, settings.MQTT.protocol, sizeof(rec.mqttProtocol));
  strlcpy(rec.mqttHostname, settings.MQTT.hostname, sizeof(rec.mqttHostname));
  rec.mqttPort = settings.MQTT.port;
  rec.mqttPubDisco = settings.MQTT.pubDisco;
  strlcpy(rec.mqttRootTopic, settings.MQTT.rootTopic, sizeof(rec.mqttRootTopic));
  strlcpy(rec.mqttDiscoTopic, settings.MQTT.discoTopic, sizeof(rec.mqttDiscoTopic));
#if HAS_ETHERNET
  rec.ethBoardType = settings.Ethernet.boardType;
  rec.ethPhyType = settings.Ethernet.phyType;
  rec.ethClkMode = settings.Ethernet.CLKMode;
  rec.ethPhyAddress = settings.Ethernet.phyAddress;
  rec.ethPWRPin = settings.Ethernet.PWRPin;
  rec.ethMDCPin = settings.Ethernet.MDCPin;
  rec.ethMDIOPin = settings.Ethernet.MDIOPin;
#endif
  return rec;
}
static trans_record_t transRecord(const transceiver_config_t &cfg) {
  trans_record_t rec;
  rec.enabled = cfg.enabled;
  rec.proto = cfg.proto;
  rec.type = cfg.type;
  rec.SCKPin = cfg.SCKPin;
  rec.CSNPin = cfg.CSNPin;
  rec.MOSIPin = cfg.MOSIPin;
  rec.MISOPin = cfg.MISOPin;
  rec.TXPin = cfg.TXPin;
  rec.RXPin = cfg.RXPin;
  rec.frequency = cfg.frequency;
  rec.rxBandwidth = cfg.rxBandwidth;
  rec.deviation = cfg.deviation;
  rec.txPower = cfg.txPower;
  rec.noiseDetection = cfg.noiseDetection;
  return rec;
}
static void applySettingsRecord(const settings_record_t &rec) {
  strlcpy(settings.hostname, rec.hostname, sizeof(settings.hostname));
  strlcpy(settings.NTP.ntpServer, rec.ntpServer, sizeof(settings.NTP.ntpServer));
  strlcpy(settings.NTP.posixZone, rec.posixZone, sizeof(settings.NTP.posixZone));
  settings.ssdpBroadcast = rec.ssdpBroadcast;
  settings.checkForUpdate = rec.checkForUpdate;
}
static void applyNetRecord(const net_record_t &rec, const restore_options_t &opts, bool sameBoard) {
  if(opts.network) {
    settings.connType = static_cast<conn_types_t>(rec.connType);
    settings.IP.dhcp = rec.dhcp;
    settings.IP.ip.fromString(rec.ip);
    settings.IP.gateway.fromString(rec.gateway);
    settings.IP.subnet.fromString(rec.subnet);
    settings.IP.dns1.fromString(rec.dns1);
    settings.IP.dns2.fromString(rec.dns2);
#if HAS_ETHERNET
    if(sameBoard) {
      ESP_LOGI(TAG, "Restoring Ethernet adapter settings");
      settings.Ethernet.boardType = rec.ethBoardType;
      settings.Ethernet.phyType = rec.ethPhyType;
      settings.Ethernet.CLKMode = rec.ethClkMode;
      settings.Ethernet.phyAddress = rec.ethPhyAddress;
      settings.Ethernet.PWRPin = rec.ethPWRPin;
      settings.Ethernet.MDCPin = rec.ethMDCPin;
      settings.Ethernet.MDIOPin = rec.ethMDIOPin;
    }
#else
    (void)sameBoard;
#endif
  }
  if(opts.mqtt) {
    strlcpy(settings.MQTT.protocol, rec.mqttProtocol, sizeof(settings.MQTT.protocol));
    strlcpy(settings.MQTT.hostname, rec.mqttHostname, sizeof(settings.MQTT.hostname));
    settings.MQTT.port = rec.mqttPort;
    settings.MQTT.pubDisco = rec.mqttPubDisco;
    strlcpy(settings.MQTT.rootTopic, rec.mqttRootTopic, sizeof(settings.MQTT.rootTopic));
    strlcpy(settings.MQTT.discoTopic, rec.mqttDiscoTopic, sizeof(settings.MQTT.discoTopic));
  }
}
static void applyTransRecord(const trans_record_t &rec, transceiver_config_t &cfg) {
  cfg.enabled = rec.enabled;
  cfg.proto = rec.proto;
  cfg.type = rec.type;
  cfg.SCKPin = rec.SCKPin;
  cfg.CSNPin = rec.CSNPin;
  cfg.MOSIPin = rec.MOSIPin;
  cfg.MISOPin = rec.MISOPin;
  cfg.TXPin = rec.TXPin;
  cfg.RXPin = rec.RXPin;
  cfg.frequency = rec.frequency;
  cfg.rxBandwidth = rec.rxBandwidth;
  cfg.deviation = rec.deviation;
  cfg.txPower = rec.txPower;
  cfg.noiseDetection = rec.noiseDetection;
}
// NVS keeps the last rolling code each remote sent.
static uint16_t keepRollingCode(Preferences &pref, SomfyRemote *remote, uint16_t inFile, bool restoring) {
  const uint16_t stored = pref.getUShort(remote->getRemotePrefId(), 0);
  const uint16_t code = rollingCodeAfterLoad(stored, inFile, restoring);
  if(code != stored) pref.putUShort(remote->getRemotePrefId(), code);
  return code;
}

void ShadeConfigFile::readRoom(SomfyRoom *room) {
  room_record_t rec = roomRecord(room);
  readRoomRecord(*this, rec);
  room->roomId = rec.roomId;
  strlcpy(room->name, rec.name, sizeof(room->name));
  room->sortOrder = rec.sortOrder;
}
void ShadeConfigFile::readShade(SomfyShade *shade) {
  shade_record_t rec = shadeRecord(shade);
  readShadeRecord(*this, this->header, rec);
  if(rec.shadeId == 255) {
    shade->clear();
    return;
  }
  Preferences pref;
  pref.begin("ShadeCodes");
  shade->setShadeId(rec.shadeId);
  shade->paired = rec.paired;
  shade->shadeType = rec.shadeType;
  shade->setRemoteAddress(rec.remoteAddress);
  strlcpy(shade->name, rec.name, sizeof(shade->name));
  shade->tiltType = rec.tiltType;
  shade->proto = rec.proto;
  shade->bitLength = rec.bitLength;
  shade->upTime = rec.upTime;
  shade->downTime = rec.downTime;
  shade->tiltTime = rec.tiltTime;
  shade->stepSize = rec.stepSize;
  for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
    SomfyLinkedRemote *rem = &shade->linkedRemotes[j];
    rem->setRemoteAddress(rec.linkedRemotes[j]);
    if(rem->getRemoteAddress() != 0) rem->lastRollingCode = pref.getUShort(rem->getRemotePrefId(), 0);
  }
  shade->lastRollingCode = rec.lastRollingCode;
  if(shade->getRemoteAddress() != 0) shade->lastRollingCode = keepRollingCode(pref, shade, rec.lastRollingCode, this->restoring);
  pref.end();
  shade->setFlags(rec.flags);
  // The targets are where the shade is.
  shade->restore(rec.currentPos, rec.currentTiltPos, rec.myPos, rec.myTiltPos);
  shade->flipCommands = rec.flipCommands;
  shade->flipPosition = rec.flipPosition;
  shade->repeats = rec.repeats;
  shade->sortOrder = rec.sortOrder;
  shade->gpioUp = rec.gpioUp;
  shade->gpioDown = rec.gpioDown;
  shade->gpioMy = rec.gpioMy;
  shade->gpioFlags = rec.gpioFlags;
  shade->roomId = rec.roomId;
  if(shade->proto == radio_proto::GP_Relay || shade->proto == radio_proto::GP_Remote) {
    pinMode(shade->gpioUp, OUTPUT);
    pinMode(shade->gpioDown, OUTPUT);
  }
  if(shade->proto == radio_proto::GP_Remote)
    pinMode(shade->gpioMy, OUTPUT);
}
void ShadeConfigFile::readGroup(SomfyGroup *group) {
  group_record_t rec = groupRecord(group);
  readGroupRecord(*this, this->header, rec);
  if(rec.groupId == 255) {
    group->clear();
    return;
  }
  group->setGroupId(rec.groupId);
  group->groupType = rec.groupType;
  group->setRemoteAddress(rec.remoteAddress);
  strlcpy(group->name, rec.name, sizeof(group->name));
  group->proto = rec.proto;
  group->bitLength = rec.bitLength;
  memcpy(group->linkedShades, rec.linkedShades, sizeof(group->linkedShades));
  group->compressLinkedShadeIds();
  group->repeats = rec.repeats;
  group->sortOrder = rec.sortOrder;
  group->flipCommands = rec.flipCommands;
  group->roomId = rec.roomId;
  group->lastRollingCode = rec.lastRollingCode;
  if(group->getRemoteAddress() != 0) {
    Preferences pref;
    pref.begin("ShadeCodes");
    // A backup from before version 23 has no code for the group to restore.
    group->lastRollingCode = keepRollingCode(pref, group, rec.lastRollingCode, this->restoring && rec.hasRollingCode);
    pref.end();
  }
}
void ShadeConfigFile::readRepeaters(SomfyShadeController *s) {
  uint32_t repeaters[SOMFY_MAX_REPEATERS];
  readRepeaterRecord(*this, repeaters);
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) s->linkRepeater(repeaters[i]);
}

// The rooms, shades and groups, in place of the ones the controller has.
void ShadeConfigFile::readModel(SomfyShadeController *s) {
  for(uint8_t i = 0; i < this->header.roomRecords; i++) this->readRoom(&s->rooms[i]);
  for(uint8_t i = this->header.roomRecords; i < SOMFY_MAX_ROOMS; i++) s->rooms[i].clear();
  for(uint8_t i = 0; i < this->header.shadeRecords; i++) this->readShade(&s->shades[i]);
  for(uint8_t i = this->header.shadeRecords; i < SOMFY_MAX_SHADES; i++) s->shades[i].clear();
  for(uint8_t i = 0; i < this->header.groupRecords; i++) this->readGroup(&s->groups[i]);
  for(uint8_t i = this->header.groupRecords; i < SOMFY_MAX_GROUPS; i++) s->groups[i].clear();
}
bool ShadeConfigFile::begin(bool readOnly) { return this->begin("/shades.cfg", readOnly); }
bool ShadeConfigFile::begin(const char *filename, bool readOnly) { return ConfigFile::begin(filename, readOnly); }
void ShadeConfigFile::end() { ConfigFile::end(); }
bool ShadeConfigFile::writeModel(SomfyShadeController *s) {
  this->header.version = SHADE_HDR_VER;
  this->header.length = SHADE_HDR_SIZE;
  this->header.roomRecordSize = ROOM_REC_SIZE;
  this->header.roomRecords = s->roomCount();
  this->header.shadeRecordSize = SHADE_REC_SIZE;
  this->header.shadeRecords = s->shadeCount();
  this->header.groupRecordSize = GROUP_REC_SIZE;
  this->header.groupRecords = s->groupCount();
  this->header.repeaterRecords = 1;
  this->header.repeaterRecordSize = REPEATER_REC_SIZE;
  strlcpy(this->header.serverId, settings.serverId, sizeof(this->header.serverId));
  this->writeHeader();
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    SomfyRoom *room = &s->rooms[i];
    if(room->roomId != 0) writeRoomRecord(*this, roomRecord(room));
  }
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &s->shades[i];
    if(shade->getShadeId() == 255) continue;
    // Only a blind tilts: the others forget any tilt they were given.
    if(shade->tiltType == tilt_types::none || shade->shadeType != shade_types::blind) {
      shade->clearTilt();
      shade->tiltType = tilt_types::none;
    }
    writeShadeRecord(*this, shadeRecord(shade));
  }
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &s->groups[i];
    if(group->getGroupId() != 255) writeGroupRecord(*this, groupRecord(group));
  }
  writeRepeaterRecord(*this, s->repeaters);
  return true;
}
bool ShadeConfigFile::save(SomfyShadeController *s) {
  this->header.settingsRecordSize = 0;
  this->header.netRecordSize = 0;
  this->header.transRecordSize = 0;
  return this->writeModel(s);
}
bool ShadeConfigFile::backup(SomfyShadeController *s) {
  const settings_record_t settingsRec = settingsRecord();
  const net_record_t netRec = netRecord();
  const trans_record_t transRec = transRecord(s->transceiver.config);
  // The header gives each record's size, which a counter measures.
  ConfigCounter settingsBytes, netBytes;
  writeSettingsRecord(settingsBytes, settingsRec);
  writeNetRecord(netBytes, netRec, HAS_ETHERNET);
  this->header.settingsRecordSize = settingsBytes.bytes;
  this->header.netRecordSize = netBytes.bytes;
  this->header.transRecordSize = TRANS_REC_SIZE;
  this->writeModel(s);
  writeSettingsRecord(*this, settingsRec);
  writeNetRecord(*this, netRec, HAS_ETHERNET);
  writeTransRecord(*this, transRec);
  return true;
}
bool ShadeConfigFile::validate() {
  this->readHeader();
  if(this->header.version < 1) {
    ESP_LOGE(TAG, "Invalid Header Version:%u", this->header.version);
    return false;
  }
  if(this->header.shadeRecordSize < 100) {
    ESP_LOGE(TAG, "Invalid Shade Record Size:%u", this->header.shadeRecordSize);
    return false;
  }
  if(this->header.version > 10) {
    if(this->header.groupRecordSize < 100) {
      ESP_LOGE(TAG, "Invalid Group Record Size:%u", this->header.groupRecordSize);
      return false;
    }
  }
  if(this->file.position() != this->header.length) {
    ESP_LOGE(TAG, "File not positioned at %u end of header: %d", this->header.length, this->file.position());
    return false;
  }

  // Only a warning: backups written before 4.0 declare the transceiver record
  // 6 bytes short, and 4.0 backups from boards without Ethernet declare the
  // network record 28 bytes long.  Records are read to their newline.
  const uint32_t fsize = expectedFileBytes(this->header);
  if(this->file.size() != fsize) {
    ESP_LOGW(TAG, "File is %u bytes but its header adds up to %u", (unsigned)this->file.size(), (unsigned)fsize);
  }
  // Next check to see if the records match the header length.
  uint8_t recs = 0;
  uint32_t startPos = this->file.position();
  if(this->header.version >= 19) {
    while(recs < this->header.roomRecords) {
      uint32_t pos = this->file.position();
      if(!this->seekChar(CFG_REC_END)) {
        ESP_LOGE(TAG, "Failed to find the room record end %d", recs);
        return false;
      }
      if(this->file.position() - pos != this->header.roomRecordSize) {
        ESP_LOGE(TAG, "Room record length is %d and should be %d", this->file.position() - pos, this->header.roomRecordSize);
        return false;
      }
      recs++;
    }
    recs = 0;
  }
  while(recs < this->header.shadeRecords) {
    uint32_t pos = this->file.position();
    if(!this->seekChar(CFG_REC_END)) {
      ESP_LOGE(TAG, "Failed to find the shade record end %d", recs);
      return false;
    }
    if(this->file.position() - pos != this->header.shadeRecordSize) {
      ESP_LOGE(TAG, "Shade record length is %d and should be %d", this->file.position() - pos, this->header.shadeRecordSize);
      return false;
    }
    recs++;
  }
  if(this->header.version > 10) {
    recs = 0;
    while(recs < this->header.groupRecords) {
      uint32_t pos = this->file.position();
      if(!this->seekChar(CFG_REC_END)) {
        ESP_LOGE(TAG, "Failed to find the group record end %d", recs);
        return false;
      }
      recs++;
      if(this->file.position() - pos != this->header.groupRecordSize) {
        ESP_LOGE(TAG, "Group record length is %d and should be %d", this->file.position() - pos, this->header.groupRecordSize);
        return false;
      }
    }
  }
  if(this->header.version >= 21) {
    recs = 0;
    while(recs < this->header.repeaterRecords) {
      // Only logged: this also runs on shades.cfg at boot, and failing it
      // there would start the device without its shades.
      uint32_t pos = this->file.position();
      if(!this->seekChar(CFG_REC_END)) {
        ESP_LOGE(TAG, "Failed to find the repeater record end %d", recs);
      }
      else if(this->file.position() - pos != this->header.repeaterRecordSize) {
        ESP_LOGW(TAG, "Repeater record length is %d and should be %d", this->file.position() - pos, this->header.repeaterRecordSize);
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
bool ShadeConfigFile::validateFile(const char *filename) {
  if(!LittleFS.exists(filename)) return false;
  ShadeConfigFile file;
  file.begin(filename, true);
  const bool valid = file.validate();
  file.end();
  return valid;
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
    ESP_LOGI(TAG, "Opening shade restore file");
    this->begin(filename, true);
    opened = true;
  }
  if(!this->validate()) {
    ESP_LOGE(TAG, "Shade restore file invalid!");
    if(opened) this->end();
    return false;
  }
  this->restoring = true;
  if(opts.shades) {
    ESP_LOGI(TAG, "Restoring the rooms, shades and groups");
    this->readModel(s);
  }
  else {
    ESP_LOGI(TAG, "Shade data ignored");
    skipRecords(*this, this->header.roomRecords + this->header.shadeRecords + this->header.groupRecords);
  }
  if(opts.repeaters && this->header.repeaterRecords > 0) {
    ESP_LOGI(TAG, "Restoring Repeaters...");
    memset(s->repeaters, 0x00, sizeof(s->repeaters));
    for(uint8_t i = 0; i < this->header.repeaterRecords; i++) this->readRepeaters(s);
  }
  else skipRecords(*this, this->header.repeaterRecords);
  if(this->header.settingsRecordSize > 0) {
    if(opts.settings) {
      settings_record_t rec = settingsRecord();
      readSettingsRecord(*this, this->header, rec);
      applySettingsRecord(rec);
    }
    else skipRecords(*this, 1);
  }
  if(this->header.netRecordSize > 0) {
    if(opts.network || opts.mqtt) {
      // Only the board that wrote the Ethernet settings takes them back.
      const bool sameBoard = HAS_ETHERNET && strncmp(settings.serverId, this->header.serverId, sizeof(settings.serverId)) == 0;
      net_record_t rec = netRecord();
      readNetRecord(*this, this->header, rec, sameBoard);
      applyNetRecord(rec, opts, sameBoard);
    }
    else skipRecords(*this, 1);
  }
  // The repeaters are saved in shades.cfg too.  Left to somfy.loop(), they
  // race the reboot.
  if(opts.shades || opts.repeaters) s->commit();
  if(opts.transceiver && this->header.transRecordSize > 0) {
    // Read into a copy so the radio task never transmits on a half restored config.
    trans_record_t rec = transRecord(s->transceiver.config);
    readTransRecord(*this, rec);
    transceiver_config_t cfg = s->transceiver.config;
    applyTransRecord(rec, cfg);
    s->transceiver.setConfig(cfg);
  }
  if(opts.settings || opts.network) settings.save();
  if(opts.settings) settings.NTP.save();
  if(opts.network) {
    settings.IP.save();
    settings.WIFI.save();
#if HAS_ETHERNET
    settings.Ethernet.save();
#endif
  }
  if(opts.mqtt) settings.MQTT.save();
  return true;
}
bool ShadeConfigFile::loadFile(SomfyShadeController *s, const char *filename) {
  bool opened = false;
  if(!this->isOpen()) {
    ESP_LOGI(TAG, "Opening shade config file");
    this->begin(filename, true);
    opened = true;
  }
  if(!this->validate()) {
    ESP_LOGE(TAG, "Shade config file invalid!");
    if(opened) this->end();
    return false;
  }
  this->restoring = false;
  this->readModel(s);
  if(this->header.repeaterRecords > 0) {
    memset(s->repeaters, 0x00, sizeof(s->repeaters));
    for(uint8_t i = 0; i < this->header.repeaterRecords; i++) this->readRepeaters(s);
  }
  if(opened) {
    ESP_LOGI(TAG, "Closing shade config file");
    this->end();
  }
  return true;
}
bool ShadeConfigFile::exists() { return LittleFS.exists("/shades.cfg"); }
