#include <Preferences.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <SPI.h>
#include <WebServer.h>
#include <esp_task_wdt.h>
#include "Utils.h"
#include "ConfigSettings.h"
#include "Somfy.h"
#include "Sockets.h"
#include "MQTT.h"
#include "ConfigFile.h"
#include "GitOTA.h"

extern Preferences pref;
extern SomfyShadeController somfy;
extern SocketEmitter sockEmit;
extern ConfigSettings settings;
extern MQTTClass mqtt;
extern GitUpdater git;


uint8_t rxmode = 0;  // Indicates whether the radio is in receive mode.  Just to ensure there isn't more than one interrupt hooked.
#define SYMBOL 640
#if defined(ESP8266)
    #define RECEIVE_ATTR ICACHE_RAM_ATTR
#elif defined(ESP32)
    #define RECEIVE_ATTR IRAM_ATTR
#else
    #define RECEIVE_ATTR
#endif

#define SETMY_REPEATS 35
#define TILT_REPEATS 15
#define TX_QUEUE_DELAY 100

int sort_asc(const void *cmp1, const void *cmp2) {
  int a = *((uint8_t *)cmp1);
  int b = *((uint8_t *)cmp2);
  if(a == b) return 0;
  else if(a < b) return -1;
  return 1;
}

static int interruptPin = 0;
static uint8_t bit_length = 56;
somfy_commands translateSomfyCommand(const String& string) {
    if (string.equalsIgnoreCase("My")) return somfy_commands::My;
    else if (string.equalsIgnoreCase("Up")) return somfy_commands::Up;
    else if (string.equalsIgnoreCase("MyUp")) return somfy_commands::MyUp;
    else if (string.equalsIgnoreCase("Down")) return somfy_commands::Down;
    else if (string.equalsIgnoreCase("MyDown")) return somfy_commands::MyDown;
    else if (string.equalsIgnoreCase("UpDown")) return somfy_commands::UpDown;
    else if (string.equalsIgnoreCase("MyUpDown")) return somfy_commands::MyUpDown;
    else if (string.equalsIgnoreCase("Prog")) return somfy_commands::Prog;
    else if (string.equalsIgnoreCase("SunFlag")) return somfy_commands::SunFlag;
    else if (string.equalsIgnoreCase("StepUp")) return somfy_commands::StepUp;
    else if (string.equalsIgnoreCase("StepDown")) return somfy_commands::StepDown;
    else if (string.equalsIgnoreCase("Flag")) return somfy_commands::Flag;
    else if (string.equalsIgnoreCase("Sensor")) return somfy_commands::Sensor;
    else if (string.equalsIgnoreCase("Toggle")) return somfy_commands::Toggle;
    else if (string.equalsIgnoreCase("Favorite")) return somfy_commands::Favorite;
    else if (string.equalsIgnoreCase("Stop")) return somfy_commands::Stop;
    else if (string.startsWith("fav") || string.startsWith("FAV")) return somfy_commands::Favorite;
    else if (string.startsWith("mud") || string.startsWith("MUD")) return somfy_commands::MyUpDown;
    else if (string.startsWith("md") || string.startsWith("MD")) return somfy_commands::MyDown;
    else if (string.startsWith("ud") || string.startsWith("UD")) return somfy_commands::UpDown;
    else if (string.startsWith("mu") || string.startsWith("MU")) return somfy_commands::MyUp;
    else if (string.startsWith("su") || string.startsWith("SU")) return somfy_commands::StepUp;
    else if (string.startsWith("sd") || string.startsWith("SD")) return somfy_commands::StepDown;
    else if (string.startsWith("sen") || string.startsWith("SEN")) return somfy_commands::Sensor;
    else if (string.startsWith("p") || string.startsWith("P")) return somfy_commands::Prog;
    else if (string.startsWith("u") || string.startsWith("U")) return somfy_commands::Up;
    else if (string.startsWith("d") || string.startsWith("D")) return somfy_commands::Down;
    else if (string.startsWith("m") || string.startsWith("M")) return somfy_commands::My;
    else if (string.startsWith("f") || string.startsWith("F")) return somfy_commands::Flag;
    else if (string.startsWith("s") || string.startsWith("S")) return somfy_commands::SunFlag;
    else if (string.startsWith("t") || string.startsWith("T")) return somfy_commands::Toggle;
    else if (string.length() == 1) return static_cast<somfy_commands>(strtol(string.c_str(), nullptr, 16));
    else return somfy_commands::My;
}
String translateSomfyCommand(const somfy_commands cmd) {
    switch (cmd) {
    case somfy_commands::Up:
        return "Up";
    case somfy_commands::Down:
        return "Down";
    case somfy_commands::My:
        return "My";
    case somfy_commands::MyUp:
        return "My+Up";
    case somfy_commands::UpDown:
        return "Up+Down";
    case somfy_commands::MyDown:
        return "My+Down";
    case somfy_commands::MyUpDown:
        return "My+Up+Down";
    case somfy_commands::Prog:
        return "Prog";
    case somfy_commands::SunFlag:
        return "Sun Flag";
    case somfy_commands::Flag:
        return "Flag";
    case somfy_commands::StepUp:
        return "Step Up";
    case somfy_commands::StepDown:
        return "Step Down";
    case somfy_commands::Sensor:
        return "Sensor";
    case somfy_commands::Toggle:
        return "Toggle";
    case somfy_commands::Favorite:
        return "Favorite";
    case somfy_commands::Stop:
        return "Stop";
    default:
        return "Unknown(" + String((uint8_t)cmd) + ")";
    }
}
byte somfy_frame_t::calc80Checksum(byte b0, byte b1, byte b2) {
  byte cs80 = 0;
  cs80 = (((b0 & 0xF0) >> 4) ^ ((b1 & 0xF0) >> 4));
  cs80 ^= ((b2 & 0xF0) >> 4);
  cs80 ^= (b0 & 0x0F);
  cs80 ^= (b1 & 0x0F);
  return cs80;
}

void somfy_frame_t::decodeFrame(byte* frame) {
    byte decoded[10];
    decoded[0] = frame[0];
    // The last 3 bytes are not encoded even on 80-bits. Go figure.
    decoded[7] = frame[7];
    decoded[8] = frame[8];
    decoded[9] = frame[9];
    for (byte i = 1; i < 7; i++) {
        decoded[i] = frame[i] ^ frame[i - 1];
    }
    byte checksum = 0;
    // We only want the upper nibble for the command byte.
    for (byte i = 0; i < 7; i++) {
        if (i == 1) checksum = checksum ^ (decoded[i] >> 4);
        else checksum = checksum ^ decoded[i] ^ (decoded[i] >> 4);
    }
    checksum &= 0b1111;  // We keep the last 4 bits only

    this->checksum = decoded[1] & 0b1111;
    this->encKey = decoded[0];
    // Lets first determine the protocol.
    this->cmd = (somfy_commands)((decoded[1] >> 4));
    if(this->cmd == somfy_commands::RTWProto) {
      if(this->encKey >= 160) {
        this->proto = radio_proto::RTS;
        if(this->encKey == 164) this->cmd = somfy_commands::Toggle;
      }
      else if(this->encKey > 148) {
        this->proto = radio_proto::RTV;
        this->cmd = (somfy_commands)(this->encKey - 148);
      }
      else if(this->encKey > 133) {
        this->proto = radio_proto::RTW;
        this->cmd = (somfy_commands)(this->encKey - 133);
      }
    }
    else this->proto = radio_proto::RTS;
    // We reuse this memory address so we must reset the processed
    // flag.  This will ensure we can see frames on the first beat.
    this->processed = false;
    this->rollingCode = decoded[3] + (decoded[2] << 8);
    this->remoteAddress = (decoded[6] + (decoded[5] << 8) + (decoded[4] << 16));
    this->valid = this->checksum == checksum && this->remoteAddress > 0 && this->remoteAddress < 16777215;
    if (this->cmd != somfy_commands::Sensor && this->valid) this->valid = (this->rollingCode > 0);
    // Next lets process some of the RTS extensions for 80-bit frames
    if(this->valid && this->proto == radio_proto::RTS && this->bitLength == 80) {
      // Do a parity checksum on the 80 bit data.
      if((decoded[9] & 0x0F) != this->calc80Checksum(decoded[7], decoded[8], decoded[9])) this->valid = false;
      if(this->valid) {
        // Translate extensions for stop and favorite.
        if(this->cmd == somfy_commands::My) this->cmd = (somfy_commands)((decoded[1] >> 4) | ((decoded[8] & 0x0F) << 4));
        // Bit packing to get the step size prohibits translation on the byte.
        else if(this->cmd == somfy_commands::StepDown) this->cmd = (somfy_commands)((decoded[1] >> 4) | ((decoded[8] & 0x08) << 4));
      }
    }
    if (this->valid) {

        // Check for valid command.
        switch (this->cmd) {
        //case somfy_commands::Unknown0:
        case somfy_commands::My:
        case somfy_commands::Up:
        case somfy_commands::MyUp:
        case somfy_commands::Down:
        case somfy_commands::MyDown:
        case somfy_commands::UpDown:
        case somfy_commands::MyUpDown:
        case somfy_commands::Prog:
        case somfy_commands::Flag:
        case somfy_commands::SunFlag:
        case somfy_commands::Sensor:
            break;
        case somfy_commands::UnknownD:
        case somfy_commands::RTWProto:
            this->valid = false;
            break;
        case somfy_commands::StepUp:
        case somfy_commands::StepDown:
            // Decode the step size.
            this->stepSize = ((decoded[8] & 0x07) << 4) | ((decoded[9] & 0xF0) >> 4);
            break;
        case somfy_commands::Toggle:
        case somfy_commands::Favorite:
        case somfy_commands::Stop:
            break;
        default:
            this->valid = false;
            break;
        }
    }
    if(this->valid && this->encKey == 0) this->valid = false; 
    if (!this->valid) {
        Serial.print("INVALID FRAME ");
        Serial.print("KEY:");
        Serial.print(this->encKey);
        Serial.print(" ADDR:");
        Serial.print(this->remoteAddress);
        Serial.print(" CMD:");
        Serial.print(translateSomfyCommand(this->cmd));
        Serial.print(" RCODE:");
        Serial.println(this->rollingCode);
        Serial.println("    KEY  1   2   3   4   5   6  ");
        Serial.println("--------------------------------");
        Serial.print("ENC ");
        for (byte i = 0; i < 10; i++) {
            if (frame[i] < 10)
                Serial.print("  ");
            else if (frame[i] < 100)
                Serial.print(" ");
            Serial.print(frame[i]);
            Serial.print(" ");
        }
        Serial.println();
        Serial.print("DEC ");
        for (byte i = 0; i < 10; i++) {
            if (decoded[i] < 10)
                Serial.print("  ");
            else if (decoded[i] < 100)
                Serial.print(" ");
            Serial.print(decoded[i]);
            Serial.print(" ");
        }
        Serial.println();
    }
}
void somfy_frame_t::decodeFrame(somfy_rx_t *rx) {
  this->hwsync = rx->cpt_synchro_hw;
  this->pulseCount = rx->pulseCount;
  this->bitLength = rx->bit_length;
  this->rssi = ELECHOUSE_cc1101.getRssi();
  this->decodeFrame(rx->payload);
}
byte somfy_frame_t::encode80Byte7(byte start, uint8_t repeat) {
  while((repeat * 4) + start > 255) repeat -= 15;
  return start + (repeat * 4);
}
void somfy_frame_t::encode80BitFrame(byte *frame, uint8_t repeat) {
  switch(this->cmd) {
    // Step up and down commands encode the step size into the last 3 bytes.
    case somfy_commands::StepUp:
      if(repeat == 0) frame[1] = (static_cast<byte>(somfy_commands::StepDown) << 4) | (frame[1] & 0x0F);
      if(this->stepSize == 0) this->stepSize = 1;
      frame[7] = 132; // For simplicity this appears to be constant.
      frame[8] = ((this->stepSize & 0x70) >> 4) | 0x38;
      frame[9] = ((this->stepSize & 0x0F) << 4);
      frame[9] |= this->calc80Checksum(frame[7], frame[8], frame[9]);
      break;
    case somfy_commands::StepDown:
      if(repeat == 0) frame[1] = (static_cast<byte>(somfy_commands::StepDown) << 4) | (frame[1] & 0x0F);
      if(this->stepSize == 0) this->stepSize = 1;
      frame[7] = 132; // For simplicity this appears to be constant.
      frame[8] = ((this->stepSize & 0x70) >> 4) | 0x30;
      frame[9] = ((this->stepSize & 0x0F) << 4);
      frame[9] |= this->calc80Checksum(frame[7], frame[8], frame[9]);
      break;
    case somfy_commands::Favorite:
      if(repeat == 0) frame[1] = (static_cast<byte>(somfy_commands::My) << 4) | (frame[1] & 0x0F);
      frame[7] = repeat > 0 ? 132 : 196;
      frame[8] = 44;
      frame[9] = 0x90;
      frame[9] |= this->calc80Checksum(frame[7], frame[8], frame[9]);
      break;
    case somfy_commands::Stop:
      if(repeat == 0) frame[1] = (static_cast<byte>(somfy_commands::My) << 4) | (frame[1] & 0x0F);
      frame[7] = repeat > 0 ? 132 : 196;
      frame[8] = 47;
      frame[9] = 0xF0;
      frame[9] |= this->calc80Checksum(frame[7], frame[8], frame[9]);
      break;
    case somfy_commands::Toggle:
      frame[0] = 164;
      frame[1] |= 0xF0;
      frame[7] = this->encode80Byte7(196, repeat);
      frame[8] = 0;
      frame[9] = 0x10;
      frame[9] |= this->calc80Checksum(frame[7], frame[8], frame[9]);
      break;
    case somfy_commands::Up:
      frame[7] = this->encode80Byte7(196, repeat);
      frame[8] = 32;
      frame[9] = 0x00;
      frame[9] |= this->calc80Checksum(frame[7], frame[8], frame[9]);
      break;
    case somfy_commands::Down:
      frame[7] = this->encode80Byte7(196, repeat);
      frame[8] = 44;
      frame[9] = 0x80;
      frame[9] |= this->calc80Checksum(frame[7], frame[8], frame[9]);
      break;
    case somfy_commands::Prog:
    case somfy_commands::UpDown:
    case somfy_commands::MyDown:
    case somfy_commands::MyUp:
    case somfy_commands::MyUpDown:
    case somfy_commands::My:
      frame[7] = this->encode80Byte7(196, repeat);
      frame[8] = 0x00;
      frame[9] = 0x10;
      frame[9] |= this->calc80Checksum(frame[7], frame[8], frame[9]);
      break;      
    
    default:
      break;
  }
}
void somfy_frame_t::encodeFrame(byte *frame) { 
  const byte btn = static_cast<byte>(cmd);
  this->valid = true;
  frame[0] = this->encKey;              // Encryption key. Doesn't matter much
  frame[1] = (btn & 0x0F) << 4;         // Which button did you press? The 4 LSB will be the checksum
  frame[2] = this->rollingCode >> 8;    // Rolling code (big endian)
  frame[3] = this->rollingCode;         // Rolling code
  frame[4] = this->remoteAddress >> 16; // Remote address
  frame[5] = this->remoteAddress >> 8;  // Remote address
  frame[6] = this->remoteAddress;       // Remote address
  frame[7] = 132;
  frame[8] = 0;
  frame[9] = 29;
  // Ok so if this is an RTW things are a bit different.
  if(this->proto == radio_proto::RTW) {
    frame[1] = 0xF0;
    switch(this->cmd) {
      case somfy_commands::My:
        frame[0] = 133;
        break;
      case somfy_commands::Up:
        frame[0] = 134;
        break;
      case somfy_commands::MyUp:
        frame[0] = 135;
        break;
      case somfy_commands::Down:
        frame[0] = 136;
        break;
      case somfy_commands::MyDown:
        frame[0] = 137;
        break;
      case somfy_commands::UpDown:
        frame[0] = 138;
        break;
      case somfy_commands::MyUpDown:
        frame[0] = 139;
        break;
      case somfy_commands::Prog:
        frame[0] = 140;
        break;
      case somfy_commands::SunFlag:
        frame[0] = 141;
        break;
      case somfy_commands::Flag:
        frame[0] = 142;
        break;
      default:
        break;
    }
  }
  else if(this->proto == radio_proto::RTV) {
    frame[1] = 0xF0;
    switch(this->cmd) {
      case somfy_commands::My:
        frame[0] = 149;
        break;
      case somfy_commands::Up:
        frame[0] = 150;
        break;
      case somfy_commands::MyUp:
        frame[0] = 151;
        break;
      case somfy_commands::Down:
        frame[0] = 152;
        break;
      case somfy_commands::MyDown:
        frame[0] = 153;
        break;
      case somfy_commands::UpDown:
        frame[0] = 154;
        break;
      case somfy_commands::MyUpDown:
        frame[0] = 155;
        break;
      case somfy_commands::Prog:
        frame[0] = 156;
        break;
      case somfy_commands::SunFlag:
        frame[0] = 157;
        break;
      case somfy_commands::Flag:
        frame[0] = 158;
        break;
      default:
        break;
    }
    
  }
  else {
    if(this->bitLength == 80) this->encode80BitFrame(&frame[0], this->repeats);
  }
  byte checksum = 0;
 
  for (byte i = 0; i < 7; i++) {
      checksum = checksum ^ frame[i] ^ (frame[i] >> 4);
  }
  checksum &= 0b1111;  // We keep the last 4 bits only
  // Checksum integration
  frame[1] |= checksum;
  // Obfuscation: a XOR of all the bytes
  for (byte i = 1; i < 7; i++) {
      frame[i] ^= frame[i - 1];
  }
}
void somfy_frame_t::print() {
    Serial.println("----------- Receiving -------------");
    Serial.print("RSSI:");
    Serial.print(this->rssi);
    Serial.print(" LQI:");
    Serial.println(this->lqi);
    Serial.print("CMD:");
    Serial.print(translateSomfyCommand(this->cmd));
    Serial.print(" ADDR:");
    Serial.print(this->remoteAddress);
    Serial.print(" RCODE:");
    Serial.println(this->rollingCode);
    Serial.print("KEY:");
    Serial.print(this->encKey, HEX);
    Serial.print(" CS:");
    Serial.println(this->checksum);
}
bool somfy_frame_t::isSynonym(somfy_frame_t &frame) { return this->remoteAddress == frame.remoteAddress && this->cmd != frame.cmd && this->rollingCode == frame.rollingCode; }
bool somfy_frame_t::isRepeat(somfy_frame_t &frame) { return this->remoteAddress == frame.remoteAddress && this->cmd == frame.cmd && this->rollingCode == frame.rollingCode; }
void somfy_frame_t::copy(somfy_frame_t &frame) {
  if(this->isRepeat(frame)) {
    this->repeats++;
    this->rssi = frame.rssi;
    this->lqi = frame.lqi;
  }
  else {
    this->synonym = this->isSynonym(frame);
    this->valid = frame.valid;
    if(!this->synonym) this->processed = frame.processed;
    this->rssi = frame.rssi;
    this->lqi = frame.lqi;
    this->cmd = frame.cmd;
    this->remoteAddress = frame.remoteAddress;
    this->rollingCode = frame.rollingCode;
    this->encKey = frame.encKey;
    this->checksum = frame.checksum;
    this->hwsync = frame.hwsync;
    this->repeats = frame.repeats;
  }
}
void SomfyShadeController::end() { this->transceiver.disableReceive(); }
SomfyShadeController::SomfyShadeController() {
  memset(this->m_shadeIds, 255, sizeof(this->m_shadeIds));
  uint64_t mac = ESP.getEfuseMac();
  this->startingAddress = mac & 0x0FFFFF;
}
bool SomfyShadeController::useNVS() { return !(settings.appVersion.major > 1 || settings.appVersion.minor >= 4); };
SomfyShade *SomfyShadeController::findShadeByRemoteAddress(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade &shade = this->shades[i];
    if(shade.getRemoteAddress() == address) return &shade;
    else {
      for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
        if(shade.linkedRemotes[j].getRemoteAddress() == address) return &shade;
      }
    }
  }
  return nullptr;
}
SomfyGroup *SomfyShadeController::findGroupByRemoteAddress(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup &group = this->groups[i];
    if(group.getRemoteAddress() == address) return &group;
  }
  return nullptr;
}
void SomfyShadeController::updateGroupFlags() {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &this->groups[i];
    if(group && group->getGroupId() != 255) {
      uint8_t flags = group->flags;
      group->updateFlags();
      if(flags != group->flags)
        group->emitState();
    }
  }
}
#ifdef USE_NVS
bool SomfyShadeController::loadLegacy() {
  Serial.println("Loading Legacy shades using NVS");
  pref.begin("Shades", true);
  pref.getBytes("shadeIds", this->m_shadeIds, sizeof(this->m_shadeIds));
  pref.end();
  for(uint8_t i = 0; i < sizeof(this->m_shadeIds); i++) {
    if(i != 0) DEBUG_SOMFY.print(",");
    DEBUG_SOMFY.print(this->m_shadeIds[i]);
  }
  DEBUG_SOMFY.println();
  sortArray<uint8_t>(this->m_shadeIds, sizeof(this->m_shadeIds));
  #ifdef DEBUG_SOMFY
  for(uint8_t i = 0; i < sizeof(this->m_shadeIds); i++) {
    if(i != 0) DEBUG_SOMFY.print(",");
    DEBUG_SOMFY.print(this->m_shadeIds[i]);
  }
  DEBUG_SOMFY.println();
  #endif

  uint8_t id = 0;
  for(uint8_t i = 0; i < sizeof(this->m_shadeIds); i++) {
    if(this->m_shadeIds[i] == id) this->m_shadeIds[i] = 255;
    id = this->m_shadeIds[i];
    SomfyShade *shade = &this->shades[i];
    shade->setShadeId(id);
    if(id == 255) {
      continue;
    }
    shade->load();
  }
  #ifdef DEBUG_SOMFY
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    DEBUG_SOMFY.print(this->shades[i].getShadeId());
    DEBUG_SOMFY.print(":");
    DEBUG_SOMFY.print(this->m_shadeIds[i]);
    if(i < SOMFY_MAX_SHADES - 1) DEBUG_SOMFY.print(",");
  }
  Serial.println();
  #endif
  #ifdef USE_NVS
  if(!this->useNVS()) {
    pref.begin("Shades");
    pref.putBytes("shadeIds", this->m_shadeIds, sizeof(this->m_shadeIds));
    pref.end();
  }
  #endif
  this->commit();
  return true;
}
#endif
bool SomfyShadeController::begin() {
  // Load up all the configuration data.
  //ShadeConfigFile::getAppVersion(this->appVersion);
  Serial.printf("App Version:%u.%u.%u\n", settings.appVersion.major, settings.appVersion.minor, settings.appVersion.build);
  #ifdef USE_NVS
  if(!this->useNVS()) {  // At 1.4 we started using the configuration file.  If the file doesn't exist then booh.
    // We need to remove all the extraeneous data from NVS for the shades.  From here on out we
    // will rely on the shade configuration.
    Serial.println("No longer using NVS");
    if(ShadeConfigFile::exists()) {
      ShadeConfigFile::load(this);
    }
    else {
      this->loadLegacy();
    }
    pref.begin("Shades");
    if(pref.isKey("shadeIds")) {
      pref.getBytes("shadeIds", this->m_shadeIds, sizeof(this->m_shadeIds));
      pref.clear(); // Delete all the keys.
    }
    pref.end();
    for(uint8_t i = 0; i < sizeof(this->m_shadeIds); i++) {
      // Start deleting the keys for the shades.
      if(this->m_shadeIds[i] == 255) continue;
      char shadeKey[15];
      sprintf(shadeKey, "SomfyShade%u", this->m_shadeIds[i]);
      pref.begin(shadeKey);
      pref.clear();
      pref.end();
    }
  }
  #endif
  if(ShadeConfigFile::exists()) {
    Serial.println("shades.cfg exists so we are using that");
    ShadeConfigFile::load(this);
  }
  else {
    Serial.println("Starting clean");
    #ifdef USE_NVS
    this->loadLegacy();
    #endif
  }
  this->transceiver.begin();

  // Set the radio type for shades that have yet to be specified.
  bool saveFlag = false;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() != 255 && shade->bitLength == 0) {
      //Serial.printf("Setting bit length to %d\n", this->transceiver.config.type);
      shade->bitLength = this->transceiver.config.type;
      saveFlag = true;
    }
  }
  if(saveFlag) somfy.commit();
  return true;
}
void SomfyShadeController::commit() {
  if(git.lockFS) return;
  esp_task_wdt_reset(); // Make sure we don't reset inadvertently.
  ShadeConfigFile file;
  file.begin();
  file.save(this);
  file.end();
  this->isDirty = false;
  this->lastCommit = millis();
}
void SomfyShadeController::writeBackup() {
  if(git.lockFS) return;
  esp_task_wdt_reset(); // Make sure we don't reset inadvertently.
  ShadeConfigFile file;
  file.begin("/controller.backup", false);
  file.backup(this);
  file.end();
}
SomfyRoom * SomfyShadeController::getRoomById(uint8_t roomId) {
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    if(this->rooms[i].roomId == roomId) return &this->rooms[i];
  }
  return nullptr;
}
SomfyShade * SomfyShadeController::getShadeById(uint8_t shadeId) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() == shadeId) return &this->shades[i];
  }
  return nullptr;
}
SomfyGroup * SomfyShadeController::getGroupById(uint8_t groupId) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() == groupId) return &this->groups[i];
  }
  return nullptr;
}
void SomfyShade::clear() {
  this->setShadeId(255);
  this->setRemoteAddress(0);
  this->moveStart = 0;
  this->tiltStart = 0;
  this->noSunStart = 0;
  this->sunStart = 0;
  this->windStart = 0;
  this->windLast = 0;
  this->noWindStart = 0;
  this->noSunDone = true;
  this->sunDone = true;
  this->windDone = true;
  this->noWindDone = true;
  this->startPos = 0.0f;
  this->startTiltPos = 0.0f;
  this->settingMyPos = false;
  this->settingPos = false;
  this->settingTiltPos = false;
  this->awaitMy = 0;
  this->flipPosition = false;
  this->flipCommands = false;
  this->lastRollingCode = 0;
  this->shadeType = shade_types::roller;
  this->tiltType = tilt_types::none;
  //this->txQueue.clear();
  this->currentPos = 0.0f;
  this->currentTiltPos = 0.0f;
  this->direction = 0;
  this->tiltDirection = 0;  
  this->target = 0.0f;
  this->tiltTarget = 0.0f;
  this->myPos = -1.0f;
  this->myTiltPos = -1.0f;
  this->bitLength = somfy.transceiver.config.type;
  this->proto = somfy.transceiver.config.proto;
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++)
    this->linkedRemotes[i].setRemoteAddress(0);
  this->paired = false;
  this->name[0] = 0x00;
  this->upTime = 10000;
  this->downTime = 10000;
  this->tiltTime = 7000;
  this->stepSize = 100;
  this->repeats = 1;
  this->sortOrder = 255;
}
void SomfyRoom::clear() {
  this->roomId = 0;
  strcpy(this->name, "");
}
void SomfyGroup::clear() {
  this->setGroupId(255);
  this->setRemoteAddress(0);
  this->repeats = 0;
  this->roomId = 0;
  this->name[0] = 0x00;
  memset(&this->linkedShades, 0x00, sizeof(this->linkedShades));
}
bool SomfyShade::linkRemote(uint32_t address, uint16_t rollingCode) {
  // Check to see if the remote is already linked. If it is
  // just return true after setting the rolling code
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    if(this->linkedRemotes[i].getRemoteAddress() == address) {
      this->linkedRemotes[i].setRollingCode(rollingCode);
      return true;
    }
  }
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    if(this->linkedRemotes[i].getRemoteAddress() == 0) {
      this->linkedRemotes[i].setRemoteAddress(address);
      this->linkedRemotes[i].setRollingCode(rollingCode);
      #ifdef USE_NVS
      if(somfy.useNVS()) {
        uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
        memset(linkedAddresses, 0x00, sizeof(linkedAddresses));
        uint8_t j = 0;
        for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
          SomfyLinkedRemote lremote = this->linkedRemotes[i];
          if(lremote.getRemoteAddress() != 0) linkedAddresses[j++] = lremote.getRemoteAddress();
        }
        char shadeKey[15];
        snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", this->getShadeId());
        pref.begin(shadeKey);
        pref.putBytes("linkedAddr", linkedAddresses, sizeof(uint32_t) * SOMFY_MAX_LINKED_REMOTES);
        pref.end();
      }
      #endif
      this->commit();
      return true;
    }
  }
  return false;
}
bool SomfyGroup::linkShade(uint8_t shadeId) {
  // Check to see if the shade is already linked. If it is just return true
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] == shadeId) {
      return true;
    }
  }
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] == 0) {
      this->linkedShades[i] = shadeId;
      somfy.commit();
      return true;
    }
  }
  return false;
}
void SomfyShade::commit() { somfy.commit(); }
void SomfyShade::commitShadePosition() {
  somfy.isDirty = true;
  #ifdef USE_NVS
  char shadeKey[15];
  if(somfy.useNVS()) {
    snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", this->shadeId);
    Serial.print("Writing current shade position: ");
    Serial.println(this->currentPos, 4);
    pref.begin(shadeKey);
    pref.putFloat("currentPos", this->currentPos);
    pref.end();
  }
  #endif
}
void SomfyShade::commitMyPosition() {
  somfy.isDirty = true;
  #ifdef USE_NVS
  if(somfy.useNVS()) {
    char shadeKey[15];
    snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", this->shadeId);
    Serial.print("Writing my shade position:");
    Serial.print(this->myPos);
    Serial.println("%");
    pref.begin(shadeKey);
    pref.putUShort("myPos", this->myPos);
    pref.end();
  }
  #endif
}
void SomfyShade::commitTiltPosition() {
  somfy.isDirty = true;
  #ifdef USE_NVS
  if(somfy.useNVS()) {
    char shadeKey[15];
    snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", this->shadeId);
    Serial.print("Writing current shade tilt position: ");
    Serial.println(this->currentTiltPos, 4);
    pref.begin(shadeKey);
    pref.putFloat("currentTiltPos", this->currentTiltPos);
    pref.end();
  }
  #endif
}
bool SomfyShade::unlinkRemote(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    if(this->linkedRemotes[i].getRemoteAddress() == address) {
      this->linkedRemotes[i].setRemoteAddress(0);
      #ifdef USE_NVS
      if(somfy.useNVS()) {
        char shadeKey[15];
        snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", this->getShadeId());
        uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
        memset(linkedAddresses, 0x00, sizeof(linkedAddresses));
        uint8_t j = 0;
        for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
          SomfyLinkedRemote lremote = this->linkedRemotes[i];
          if(lremote.getRemoteAddress() != 0) linkedAddresses[j++] = lremote.getRemoteAddress();
        }
        pref.begin(shadeKey);
        pref.putBytes("linkedAddr", linkedAddresses, sizeof(uint32_t) * SOMFY_MAX_LINKED_REMOTES);
        pref.end();
      }
      #endif
      this->commit();
      return true;
    }
  }
  return false;
}
bool SomfyGroup::unlinkShade(uint8_t shadeId) {
  bool removed = false;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] == shadeId) {
      this->linkedShades[i] = 0;
      removed = true;
    }
  }
  // Compress the linked shade ids so we can stop looking on the first 0
  if(removed) {
    this->compressLinkedShadeIds();
    somfy.commit();
  }
  return removed;
}
void SomfyGroup::compressLinkedShadeIds() {
  // [1,0,4,3,0,0,0] i:0,j:0
  // [1,0,4,3,0,0,0] i:1,j:1
  // [1,4,0,3,0,0,0] i:2,j:1
  // [1,4,3,0,0,0,0] i:3,j:2
  // [1,4,3,0,0,0,0] i:4,j:2

  // [1,2,0,0,3,0,0] i:0,j:0
  // [1,2,0,0,3,0,0] i:1,j:1
  // [1,2,0,0,3,0,0] i:2,j:2
  // [1,2,0,0,3,0,0] i:3,j:2
  // [1,2,3,0,0,0,0] i:4,j:2
  // [1,2,3,0,0,0,0] i:5,j:3
  for(uint8_t i = 0, j = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] != 0) {
      if(i != j) {
        this->linkedShades[j] = this->linkedShades[i];
        this->linkedShades[i] = 0;
      }
      j++;
    }
  }
}
void SomfyShadeController::compressRepeaters() {
  for(uint8_t i = 0, j = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(this->repeaters[i] != 0) {
      if(i != j) {
        this->repeaters[j] = this->repeaters[i];
        this->repeaters[i] = 0;
      }
      j++;
    }
  }
}
bool SomfyGroup::hasShadeId(uint8_t shadeId) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] == 0) break;
    if(this->linkedShades[i] == shadeId) return true;
  }
  return false;
}
bool SomfyShade::isAtTarget() { 
  float epsilon = .00001;
  if(this->tiltType == tilt_types::tiltonly) return fabs(this->currentTiltPos - this->tiltTarget) < epsilon;
  else if(this->tiltType == tilt_types::none) return fabs(this->currentPos - this->target) < epsilon;
  return fabs(this->currentPos - this->target) < epsilon && fabs(this->currentTiltPos - this->tiltTarget) < epsilon; 
}
bool SomfyRemote::simMy() { return (this->flags & static_cast<uint8_t>(somfy_flags_t::SimMy)) > 0; }
void SomfyRemote::setSimMy(bool bSimMy) { bSimMy ? this->flags |= static_cast<uint8_t>(somfy_flags_t::SimMy) : this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::SimMy)); }
bool SomfyRemote::hasSunSensor() { return (this->flags & static_cast<uint8_t>(somfy_flags_t::SunSensor)) > 0;}
bool SomfyRemote::hasLight() { return (this->flags & static_cast<uint8_t>(somfy_flags_t::Light)) > 0; }
void SomfyRemote::setSunSensor(bool bHasSensor ) { bHasSensor ? this->flags |= static_cast<uint8_t>(somfy_flags_t::SunSensor) : this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::SunSensor)); }
void SomfyRemote::setLight(bool bHasLight ) { bHasLight ? this->flags |= static_cast<uint8_t>(somfy_flags_t::Light) : this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::Light)); }

void SomfyGroup::updateFlags() { 
  uint8_t oldFlags = this->flags;
  this->flags = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] != 0) {
      SomfyShade *shade = somfy.getShadeById(this->linkedShades[i]);
      if(shade) this->flags |= shade->flags;
    }
    else break;
  }
  if(oldFlags != this->flags) this->emitState();
}
bool SomfyShade::isInGroup() {
  if(this->getShadeId() == 255) return false;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(somfy.groups[i].getGroupId() != 255 && somfy.groups[i].hasShadeId(this->getShadeId())) return true;
  }
  return false;
}
void SomfyShade::setGPIOs() {
  if(this->proto == radio_proto::GP_Relay) {
    // Determine whether the direction needs to be set.
    uint8_t p_on = (this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0x00 ? HIGH : LOW;
    uint8_t p_off = (this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0x00 ? LOW : HIGH;
    
    int8_t dir = this->direction;
    if(dir == 0 && this->tiltType == tilt_types::integrated)
      dir = this->tiltDirection;
    else if(this->tiltType == tilt_types::tiltonly)
      dir = this->tiltDirection;
    if(this->shadeType == shade_types::drycontact) {
      digitalWrite(this->gpioDown, this->currentPos == 100 ? p_on : p_off);
      this->gpioDir = this->currentPos == 100 ? 1 : -1;
    }
    else if(this->shadeType == shade_types::drycontact2) {
      if(this->currentPos == 100) {
        digitalWrite(this->gpioDown, p_off);
        digitalWrite(this->gpioUp, p_on);
      }
      else {
        digitalWrite(this->gpioUp, p_off);
        digitalWrite(this->gpioDown, p_on);
      }
      this->gpioDir = this->currentPos == 100 ? 1 : -1;
    }
    else {
      switch(dir) {
        case -1:
          digitalWrite(this->gpioDown, p_off);
          digitalWrite(this->gpioUp, p_on);
          if(dir != this->gpioDir) Serial.printf("UP: true, DOWN: false\n");
          this->gpioDir = dir;
          break;
        case 1:
          digitalWrite(this->gpioUp, p_off);
          digitalWrite(this->gpioDown, p_on);
          if(dir != this->gpioDir) Serial.printf("UP: false, DOWN: true\n");
          this->gpioDir = dir;
          break;
        default:
          digitalWrite(this->gpioUp, p_off);
          digitalWrite(this->gpioDown, p_off);
          if(dir != this->gpioDir) Serial.printf("UP: false, DOWN: false\n");
          this->gpioDir = dir;
          break;
      }
    }
  }
  else if(this->proto == radio_proto::GP_Remote) {
    if(millis() > this->gpioRelease) {
      //uint8_t p_on = (this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0x00 ? HIGH : LOW;
      uint8_t p_off = (this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0x00 ? LOW : HIGH;
      digitalWrite(this->gpioUp, p_off);
      digitalWrite(this->gpioDown, p_off);
      digitalWrite(this->gpioMy, p_off);
      this->gpioRelease = 0;
    }
  }
}
void SomfyRemote::triggerGPIOs(somfy_frame_t &frame) { }
void SomfyShade::triggerGPIOs(somfy_frame_t &frame) {
  if(this->proto == radio_proto::GP_Remote) {
    uint8_t p_on = (this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0x00 ? HIGH : LOW;
    uint8_t p_off = (this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0x00 ? LOW : HIGH;
    int8_t dir = 0;
    switch(frame.cmd) {
      case somfy_commands::My:
        if(this->shadeType != shade_types::drycontact && !this->isToggle()) {
          digitalWrite(this->gpioUp, p_off);
          digitalWrite(this->gpioDown, p_off);
          digitalWrite(this->gpioMy, p_on);
          dir = 0;
          if(dir != this->gpioDir) Serial.printf("UP: false, DOWN: false, MY: true\n");
        }
        break;
      case somfy_commands::Up:
        if(this->shadeType != shade_types::drycontact && !this->isToggle() && this->shadeType != shade_types::drycontact2) {
          digitalWrite(this->gpioMy, p_off);
          digitalWrite(this->gpioDown, p_off);
          digitalWrite(this->gpioUp, p_on);
          dir = -1;
          Serial.printf("UP: true, DOWN: false, MY: false\n");
        }
        break;
      case somfy_commands::Toggle:
      case somfy_commands::Down:
        if(this->shadeType != shade_types::drycontact && !this->isToggle() && this->shadeType != shade_types::drycontact2) {
          digitalWrite(this->gpioMy, p_off);
          digitalWrite(this->gpioUp, p_off);
        }
        digitalWrite(this->gpioDown, p_on);
        dir = 1;
        Serial.printf("UP: false, DOWN: true, MY: false\n");
        break;
      case somfy_commands::MyUp:
        if(this->shadeType != shade_types::drycontact && !this->isToggle() && this->shadeType != shade_types::drycontact2) {
          digitalWrite(this->gpioDown, p_off);
          digitalWrite(this->gpioMy, p_on);
          digitalWrite(this->gpioUp, p_on);
          Serial.printf("UP: true, DOWN: false, MY: true\n");
        }
        break;
      case somfy_commands::MyDown:
        if(this->shadeType != shade_types::drycontact && !this->isToggle() && this->shadeType != shade_types::drycontact2) {
          digitalWrite(this->gpioUp, p_off);
          digitalWrite(this->gpioMy, p_on);
          digitalWrite(this->gpioDown, p_on);
          Serial.printf("UP: false, DOWN: true, MY: true\n");
        }
        break;
      case somfy_commands::MyUpDown:
        if(this->shadeType != shade_types::drycontact && this->isToggle() && this->shadeType != shade_types::drycontact2) {
          digitalWrite(this->gpioUp, p_on);
          digitalWrite(this->gpioMy, p_on);
          digitalWrite(this->gpioDown, p_on);
          Serial.printf("UP: true, DOWN: true, MY: true\n");
        }
        break;
      default:
        break;
    }
    this->gpioRelease = millis() + (frame.repeats * 200);
    this->gpioDir = dir;
  }  
}
void SomfyShade::checkMovement() {
  const uint64_t curTime = millis();
  const bool sunFlag = this->flags & static_cast<uint8_t>(somfy_flags_t::SunFlag);
  const bool isSunny = this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny);
  const bool isWindy = this->flags & static_cast<uint8_t>(somfy_flags_t::Windy);
  // We need to first evaluate the sensor flags as these could be triggering movement from previous sensor inputs. So
  // we must check this before setting the directional items or it will not get processed until the next loop.
  int32_t downTime = (int32_t)this->downTime;
  int32_t upTime = (int32_t)this->upTime;
  int32_t tiltTime = (int32_t)this->tiltTime;
  if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) downTime = upTime = tiltTime = 1;
  

  // We are checking movement for essentially 3 types of motors.
  // If this is an integrated tilt we need to first tilt in the direction we are moving then move.  We know 
  // what needs to be done by the tilt type.  Set a tilt first flag to indicate whether we should be tilting or
  // moving. If this is only a tilt action then the regular tilt action should operate fine.
  int8_t currDir = this->direction;
  int8_t currTiltDir = this->tiltDirection;
  this->p_direction(this->currentPos == this->target ? 0 : this->currentPos > this->target ? -1 : 1);
  bool tilt_first = this->tiltType == tilt_types::integrated && ((this->direction == -1 && this->currentTiltPos != 0.0f) || (this->direction == 1 && this->currentTiltPos != 100.0f));

  this->p_tiltDirection(this->currentTiltPos == this->tiltTarget ? 0 : this->currentTiltPos > this->tiltTarget ? -1 : 1);
  if(tilt_first) this->p_tiltDirection(this->direction);
  else if(this->direction != 0) this->p_tiltDirection(0);
  uint8_t currPos = floor(this->currentPos);
  uint8_t currTiltPos = floor(this->currentTiltPos);
  if(this->direction != 0) this->lastMovement = this->direction;
  if (sunFlag) {
    if (isSunny && !isWindy) {  // It is sunny and there is no wind so we should be extended
      if (this->noWindDone
          && !this->sunDone
          && this->sunStart
          && (curTime - this->sunStart) >= SOMFY_SUN_TIMEOUT)
      {
        this->p_target(this->myPos >= 0 ? this->myPos : 100.0f);
        //this->target = this->myPos >= 0 ? this->myPos : 100.0f;
        this->sunDone = true;
        Serial.printf("[%u] Sun -> done\r\n", this->shadeId);
      }
      if (!this->noWindDone
          && this->noWindStart
          && (curTime - this->noWindStart) >= SOMFY_NO_WIND_TIMEOUT)
      {
        this->p_target(this->myPos >= 0 ? this->myPos : 100.0f);
        //this->target = this->myPos >= 0 ? this->myPos : 100.0f;
        this->noWindDone = true;
        Serial.printf("[%u] No Wind -> done\r\n", this->shadeId);
      }
    }
    if (!isSunny
        && !this->noSunDone
        && this->noSunStart
        && (curTime - this->noSunStart) >= SOMFY_NO_SUN_TIMEOUT)
    {
      if(this->tiltType == tilt_types::tiltonly) this->p_tiltTarget(0.0f);
      this->p_target(0.0f);
      this->noSunDone = true;
      Serial.printf("[%u] No Sun -> done\r\n", this->shadeId);
    }
  }

  if (isWindy
      && !this->windDone
      && this->windStart
      && (curTime - this->windStart) >= SOMFY_WIND_TIMEOUT)
  {
    if(this->tiltType == tilt_types::tiltonly) this->p_tiltTarget(0.0f);
    this->p_target(0.0f);
    this->windDone = true;
    Serial.printf("[%u] Wind -> done\r\n", this->shadeId);
  }

  if(!tilt_first && this->direction > 0) {
    if(downTime == 0) {
      this->p_currentPos(100.0);
      //this->p_direction(0);
    }
    else {
      // The shade is moving down so we need to calculate its position through the down position.
      // 10000ms from 0 to 100
      // The starting posion is a float value from 0-1 that indicates how much the shade is open. So
      // if we take the starting position * the total down time then this will tell us how many ms it
      // has moved in the down position.
      int32_t msFrom0 = (int32_t)floor((this->startPos/100) * downTime);
      
      // So if the start position is .1 it is 10% closed so we have a 1000ms (1sec) of time to account for
      // before we add any more time.
      msFrom0 += (curTime - this->moveStart);
      // Now we should have the total number of ms that the shade moved from the top.  But just so we
      // don't have any rounding errors make sure that it is not greater than the max down time.
      msFrom0 = min(downTime, msFrom0);
      if(msFrom0 >= downTime) {
        this->p_currentPos(100.0f);
        //this->p_direction(0);        
      }
      else {
        // So now we know how much time has elapsed from the 0 position to down.  The current position should be
        // a ratio of how much time has travelled over the total time to go 100%.
  
        // We should now have the number of ms it will take to reach the shade fully close.
        this->p_currentPos((min(max((float)0.0, (float)msFrom0 / (float)downTime), (float)1.0)) * 100);
        // If the current position is >= 1 then we are at the bottom of the shade.
        if(this->currentPos >= 100) {
          this->p_currentPos(100.0);
          //this->p_direction(0);
        }
      }
    }
    if(this->currentPos >= this->target) {
      this->p_currentPos(this->target);
      //if(this->settingMyPos) Serial.printf("IsAtTarget: %d  %f=%f\n", this->isAtTarget(), this->currentPos, this->target);
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingPos) {
        if(!isAtTarget()) {
          Serial.printf("We are not at our tilt target: %.2f\n", this->tiltTarget);
          if(this->target != 100.0) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
          delay(100);
          // We now need to move the tilt to the position we requested.
          this->moveToTiltTarget(this->tiltTarget);
        }
        else
          if(this->target != 100.0) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
      }
      this->p_direction(0);
      this->tiltStart = curTime;
      this->startTiltPos = this->currentTiltPos;
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  else if(!tilt_first && this->direction < 0) {
    if(upTime == 0) {
      this->p_currentPos(0);
      //this->p_direction(0);
    }
    else {
      // The shade is moving up so we need to calculate its position through the up position. Shades
      // often move slower in the up position so since we are using a relative position the up time
      // can be calculated.
      // 10000ms from 100 to 0;
      int32_t msFrom100 = upTime - (int32_t)floor((this->startPos/100) * upTime);
      msFrom100 += (curTime - this->moveStart);
      msFrom100 = min(upTime, msFrom100);
      if(msFrom100 >= upTime) {
        this->p_currentPos(0.0f);
        //this->p_direction(0);
      }
      else {
        float fpos = ((float)1.0 - min(max((float)0.0, (float)msFrom100 / (float)upTime), (float)1.0)) * 100;
        // We should now have the number of ms it will take to reach the shade fully open.
        // If we are at the top of the shade then set the movement to 0.
        if(fpos <= 0.0) {
          this->p_currentPos(0.0f);
          //this->p_direction(0);
        }
        else 
          this->p_currentPos(fpos);
      }
    }
    if(this->currentPos <= this->target) {
      this->p_currentPos(this->target);
      //if(this->settingMyPos) Serial.printf("IsAtTarget: %d  %f=%f\n", this->isAtTarget(), this->currentPos, this->target);
      
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingPos) {
        if(!isAtTarget()) {
          Serial.printf("We are not at our tilt target: %.2f\n", this->tiltTarget);
          if(this->target != 0.0) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
          delay(100);
          // We now need to move the tilt to the position we requested.
          this->moveToTiltTarget(this->tiltTarget);
        }
        else
          if(this->target != 0.0) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
      }
      this->p_direction(0);
      this->tiltStart = curTime;
      this->startTiltPos = this->currentTiltPos;
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  if(this->tiltDirection > 0) {
    if(tilt_first) this->moveStart = curTime;
    int32_t msFrom0 = (int32_t)floor((this->startTiltPos/100) * tiltTime);
    msFrom0 += (curTime - this->tiltStart);
    msFrom0 = min(tiltTime, msFrom0);
    if(msFrom0 >= tiltTime) {
      this->p_currentTiltPos(100.0f);
      //this->p_tiltDirection(0);        
      //Serial.printf("Setting tiltDirection to 0 (not enough time) %.4f %.4f\n", msFrom0, tiltTime);
    }
    else {
      float fpos = (min(max((float)0.0, (float)msFrom0 / (float)tiltTime), (float)1.0)) * 100;
      
      if(fpos > 100.0f) {
        this->p_currentTiltPos(100.0f);
        //this->p_tiltDirection(0);
        //Serial.println("Setting tiltDirection to 0 (100%)");
      }
      else this->p_currentTiltPos(fpos);
    }
    if(tilt_first) {
      if(this->currentTiltPos >= 100.0f) {
        this->p_currentTiltPos(100.0f);
        this->moveStart = curTime;
        this->startPos = this->currentPos;
        //this->p_tiltDirection(0);
        //Serial.println("Setting tiltDirection to 0 (tilt_first)");
      }
    }
    else if(this->currentTiltPos >= this->tiltTarget) {
      this->p_currentTiltPos(this->tiltTarget);
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingTiltPos) {
        if(this->tiltType == tilt_types::integrated) {
          // If this is an integrated tilt mechanism the we will simply let it finish.  If it is not then we will stop it.
          //Serial.printf("Sending My -- tiltTarget: %.2f, tiltDirection: %d\n", this->tiltTarget, this->tiltDirection);
          if(this->tiltTarget != 100.0f || this->currentPos != 100.0f) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
        }
        else {
          // This is a tilt motor so let it complete if it is going to 100.
          if(this->tiltTarget != 100.0f) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
        }
      }
      this->p_tiltDirection(0);
      this->settingTiltPos = false;
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  else if(this->tiltDirection < 0) {
    if(tilt_first) this->moveStart = curTime;
    if(tiltTime == 0) {
      this->p_tiltDirection(0);
      this->p_currentTiltPos(0.0f);
    }
    else {
      int32_t msFrom100 = tiltTime - (int32_t)floor((this->startTiltPos/100) * tiltTime);
      msFrom100 += (curTime - this->tiltStart);
      msFrom100 = min(tiltTime, msFrom100);
      if(msFrom100 >= tiltTime) {
        this->p_currentTiltPos(0.0f);
        //this->p_tiltDirection(0);
      }
      float fpos = ((float)1.0 - min(max((float)0.0, (float)msFrom100 / (float)tiltTime), (float)1.0)) * 100;
      // If we are at the top of the shade then set the movement to 0.
      if(fpos <= 0.0f) {
        this->p_currentTiltPos(0.0f);
        //this->p_tiltDirection(0);
      }
      else this->p_currentTiltPos(fpos);
    }
    if(tilt_first) {
      if(this->currentTiltPos <= 0.0f) {
        this->p_currentTiltPos(0.0f);
        this->moveStart = curTime;
        this->startPos = this->currentPos;
        //this->p_tiltDirection(0);
      }
    }
    else if(this->currentTiltPos <= this->tiltTarget) {
      this->p_currentTiltPos(this->tiltTarget);
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingTiltPos) {
        if(this->tiltType == tilt_types::integrated) {
          // If this is an integrated tilt mechanism the we will simply let it finish.  If it is not then we will stop it.
          //Serial.printf("Sending My -- tiltTarget: %.2f, tiltDirection: %d\n", this->tiltTarget, this->tiltDirection);
          if(this->tiltTarget != 0.0 || this->currentPos != 0.0) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
        }
        else {
          // This is a tilt motor so let it complete if it is going to 0.
          if(this->tiltTarget != 0.0) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
        }
      }
      this->p_tiltDirection(0);
      this->settingTiltPos = false;
      Serial.println("Stopping at tilt position");
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  if(this->settingMyPos && this->isAtTarget()) {
    delay(200);
    // Set this position before sending the command.  If you don't the processFrame function
    // will send the shade back to its original My position.
    if(this->tiltType != tilt_types::none) {
      if(this->myTiltPos == this->currentTiltPos && this->myPos == this->currentPos) this->myPos = this->myTiltPos = -1;
      else {
        this->p_myPos(this->currentPos);
        this->p_myTiltPos(this->currentTiltPos);
      }
    }
    else {
      this->p_myTiltPos(-1);
      if(this->myPos == this->currentPos) this->p_myPos(-1);
      else this->p_myPos(this->currentPos);
    }
    SomfyRemote::sendCommand(somfy_commands::My, SETMY_REPEATS);
    this->settingMyPos = false;
    this->commitMyPosition();
    this->emitState();
  }
  else if(currDir != this->direction || currPos != floor(this->currentPos) || currTiltDir != this->tiltDirection || currTiltPos != floor(this->currentTiltPos)) {
    // We need to emit on the socket that our state has changed.
    this->emitState();
  }
}
#ifdef USE_NVS
void SomfyShade::load() {
    char shadeKey[15];
    uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
    memset(linkedAddresses, 0x00, sizeof(uint32_t) * SOMFY_MAX_LINKED_REMOTES);
    snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", this->shadeId);
    // Now load up each of the shades into memory.
    //Serial.print("key:");
    //Serial.println(shadeKey);
    
    pref.begin(shadeKey, !somfy.useNVS());
    pref.getString("name", this->name, sizeof(this->name));
    this->paired = pref.getBool("paired", false);
    if(pref.isKey("upTime") && pref.getType("upTime") != PreferenceType::PT_U32) {
      // We need to convert these to 32 bits because earlier versions did not support this.
      this->upTime = static_cast<uint32_t>(pref.getUShort("upTime", 1000));
      this->downTime = static_cast<uint32_t>(pref.getUShort("downTime", 1000));
      this->tiltTime = static_cast<uint32_t>(pref.getUShort("tiltTime", 7000));
      if(somfy.useNVS()) {
        pref.remove("upTime");
        pref.putUInt("upTime", this->upTime);
        pref.remove("downTime");
        pref.putUInt("downTime", this->downTime);
        pref.remove("tiltTime");
        pref.putUInt("tiltTime", this->tiltTime);
      }
    }
    else {
      this->upTime = pref.getUInt("upTime", this->upTime);
      this->downTime = pref.getUInt("downTime", this->downTime);
      this->tiltTime = pref.getUInt("tiltTime", this->tiltTime);
    }
    this->setRemoteAddress(pref.getUInt("remoteAddress", 0));
    this->currentPos = pref.getFloat("currentPos", 0);
    this->target = floor(this->currentPos);
    this->myPos = static_cast<float>(pref.getUShort("myPos", this->myPos));
    this->tiltType = pref.getBool("hasTilt", false) ? tilt_types::none : tilt_types::tiltmotor;
    this->shadeType = static_cast<shade_types>(pref.getChar("shadeType", static_cast<uint8_t>(this->shadeType)));
    this->currentTiltPos = pref.getFloat("currentTiltPos", 0);
    this->tiltTarget = floor(this->currentTiltPos);
    pref.getBytes("linkedAddr", linkedAddresses, sizeof(linkedAddresses));
    pref.end();
    Serial.print("shadeId:");
    Serial.print(this->getShadeId());
    Serial.print(" name:");
    Serial.print(this->name);
    Serial.print(" address:");
    Serial.print(this->getRemoteAddress());
    Serial.print(" position:");
    Serial.print(this->currentPos);
    Serial.print(" myPos:");
    Serial.println(this->myPos);
    pref.begin("ShadeCodes");
    this->lastRollingCode = pref.getUShort(this->m_remotePrefId, 0);
    for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
      SomfyLinkedRemote &lremote = this->linkedRemotes[j];
      lremote.setRemoteAddress(linkedAddresses[j]);
      lremote.lastRollingCode = pref.getUShort(lremote.getRemotePrefId(), 0);
    }
    pref.end();
}
#endif
void SomfyRoom::publish() {
  if(mqtt.connected()) {
    char topic[64];
    sprintf(topic, "rooms/%d/roomId", this->roomId);
    mqtt.publish(topic, this->roomId, true);
    sprintf(topic, "rooms/%d/name", this->roomId);
    mqtt.publish(topic, this->name, true);
    sprintf(topic, "rooms/%d/sortOrder", this->roomId);
    mqtt.publish(topic, this->sortOrder, true);
  }
}
void SomfyRoom::unpublish() {
  if(mqtt.connected()) {
    char topic[64];
    sprintf(topic, "rooms/%d/roomId", this->roomId);
    mqtt.unpublish(topic);
    sprintf(topic, "rooms/%d/name", this->roomId);
    mqtt.unpublish(topic);
    sprintf(topic, "rooms/%d/sortOrder", this->roomId);
    mqtt.unpublish(topic);
  }
}
void SomfyShade::publishState() {
  if(mqtt.connected()) {
    this->publish("position", this->transformPosition(this->currentPos), true);
    this->publish("direction", this->direction, true);
    this->publish("target", this->transformPosition(this->target), true);
    this->publish("lastRollingCode", this->lastRollingCode);
    this->publish("mypos", this->transformPosition(this->myPos), true);
    this->publish("myTiltPos", this->transformPosition(this->myTiltPos), true);
    if(this->tiltType != tilt_types::none) {
      this->publish("tiltDirection", this->tiltDirection, true);
      this->publish("tiltPosition", this->transformPosition(this->currentTiltPos), true);
      this->publish("tiltTarget", this->transformPosition(this->tiltTarget), true);
    }
    const uint8_t sunFlag = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::SunFlag));
    const uint8_t isSunny = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny));
    const uint8_t isWindy = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::Windy));
    if(this->hasSunSensor()) {
      this->publish("sunFlag", sunFlag);
      this->publish("sunny", isSunny);
    }
    this->publish("windy", isWindy);
  }
}
void SomfyShade::publishDisco() {
  if(!mqtt.connected() || !settings.MQTT.pubDisco) return;
  char topic[128] = "";
  DynamicJsonDocument doc(2048);
  JsonObject obj = doc.to<JsonObject>();
  snprintf(topic, sizeof(topic), "%s/shades/%d", settings.MQTT.rootTopic, this->shadeId);
  obj["~"] = topic;
  JsonObject dobj = obj.createNestedObject("device");
  dobj["hw_version"] = settings.fwVersion.name;
  dobj["name"] = settings.hostname;
  dobj["mf"] = "rstrouse";
  JsonArray arrids = dobj.createNestedArray("identifiers");
  //snprintf(topic, sizeof(topic), "mqtt_espsomfyrts_%s_shade%d", settings.serverId, this->shadeId);
  snprintf(topic, sizeof(topic), "mqtt_espsomfyrts_%s", settings.serverId);
  arrids.add(topic);
  //snprintf(topic, sizeof(topic), "ESPSomfy-RTS_%s", settings.serverId);
  dobj["via_device"] = topic;
  dobj["model"] = "ESPSomfy-RTS MQTT";
  snprintf(topic, sizeof(topic), "%s/status", settings.MQTT.rootTopic);
  obj["availability_topic"] = topic;
  obj["payload_available"] = "online";
  obj["payload_not_available"] = "offline";
  obj["name"] = this->name;
  snprintf(topic, sizeof(topic), "mqtt_%s_shade%d", settings.serverId, this->shadeId);
  obj["unique_id"] = topic;
  switch(this->shadeType) {
    case shade_types::blind:
      obj["device_class"] = "blind";
      obj["payload_close"] = this->flipPosition ? "-1" : "1";
      obj["payload_open"] = this->flipPosition ? "1" : "-1";
      obj["position_open"] = this->flipPosition ? 100 : 0;
      obj["position_closed"] = this->flipPosition ? 0 : 100;
      obj["state_closing"] = this->flipPosition ? "-1" : "1";
      obj["state_opening"] = this->flipPosition ? "1" : "-1";
      break;
    case shade_types::lgate:
    case shade_types::cgate:
    case shade_types::rgate:
    case shade_types::lgate1:
    case shade_types::cgate1:
    case shade_types::rgate1:
    case shade_types::ldrapery:
    case shade_types::rdrapery:
    case shade_types::cdrapery:
      obj["device_class"] = "curtain";
      obj["payload_close"] = this->flipPosition ? "-1" : "1";
      obj["payload_open"] = this->flipPosition ? "1" : "-1";
      obj["position_open"] = this->flipPosition ? 100 : 0;
      obj["position_closed"] = this->flipPosition ? 0 : 100;
      obj["state_closing"] = this->flipPosition ? "-1" : "1";
      obj["state_opening"] = this->flipPosition ? "1" : "-1";
      break;
    case shade_types::garage1:
    case shade_types::garage3:
      obj["device_class"] = "garage";
      obj["payload_close"] = this->flipPosition ? "-1" : "1";
      obj["payload_open"] = this->flipPosition ? "1" : "-1";
      obj["position_open"] = this->flipPosition ? 100 : 0;
      obj["position_closed"] = this->flipPosition ? 0 : 100;
      obj["state_closing"] = this->flipPosition ? "-1" : "1";
      obj["state_opening"] = this->flipPosition ? "1" : "-1";
      break;
    case shade_types::awning:
      obj["device_class"] = "awning";
      obj["payload_close"] = this->flipPosition ? "1" : "-1";
      obj["payload_open"] = this->flipPosition ? "-1" : "1";
      obj["position_open"] = this->flipPosition ? 0 : 100;
      obj["position_closed"] = this->flipPosition ? 100 : 0;
      obj["state_closing"] = this->flipPosition ? "1" : "-1";
      obj["state_opening"] = this->flipPosition ? "-1" : "1";
      break;
    case shade_types::shutter:
      obj["device_class"] = "shutter";
      obj["payload_close"] = this->flipPosition ? "-1" : "1";
      obj["payload_open"] = this->flipPosition ? "1" : "-1";
      obj["position_open"] = this->flipPosition ? 100 : 0;
      obj["position_closed"] = this->flipPosition ? 0 : 100;
      obj["state_closing"] = this->flipPosition ? "-1" : "1";
      obj["state_opening"] = this->flipPosition ? "1" : "-1";
      break;
    case shade_types::drycontact2:
    case shade_types::drycontact:
      break;
    default:
      obj["device_class"] = "shade";
      obj["payload_close"] = this->flipPosition ? "-1" : "1";
      obj["payload_open"] = this->flipPosition ? "1" : "-1";
      obj["position_open"] = this->flipPosition ? 100 : 0;
      obj["position_closed"] = this->flipPosition ? 0 : 100;
      obj["state_closing"] = this->flipPosition ? "-1" : "1";
      obj["state_opening"] = this->flipPosition ? "1" : "-1";
      break;
  }
  if(this->shadeType != shade_types::drycontact && this->shadeType != shade_types::drycontact2) {
    if(this->tiltType != tilt_types::tiltonly) {
      obj["command_topic"] = "~/direction/set";
      obj["position_topic"] = "~/position";
      obj["set_position_topic"] = "~/target/set";
      obj["state_topic"] = "~/direction";
      obj["payload_stop"] = "0";
      obj["state_stopped"] = "0";
    }
    else {
      obj["payload_close"] = nullptr;
      obj["payload_open"] = nullptr;
      obj["payload_stop"] = nullptr;
    }
    
    if(this->tiltType != tilt_types::none) {
      obj["tilt_command_topic"] = "~/tiltTarget/set";
      obj["tilt_status_topic"] = "~/tiltPosition";
    }
    snprintf(topic, sizeof(topic), "%s/cover/%d/config", settings.MQTT.discoTopic, this->shadeId);
  }
  else {
    obj["payload_on"] = 100;
    obj["payload_off"] = 0;
    obj["state_off"] = 0;
    obj["state_on"] = 100;
    obj["state_topic"] = "~/position";
    obj["command_topic"] = "~/target/set";
    snprintf(topic, sizeof(topic), "%s/switch/%d/config", settings.MQTT.discoTopic, this->shadeId);
  }
  
  obj["enabled_by_default"] = true;
  mqtt.publishDisco(topic, obj, true);  
}
void SomfyShade::unpublishDisco() {
  if(!mqtt.connected() || !settings.MQTT.pubDisco) return;
  char topic[128] = "";
  if(this->shadeType != shade_types::drycontact && this->shadeType != shade_types::drycontact2) {
    snprintf(topic, sizeof(topic), "%s/cover/%d/config", settings.MQTT.discoTopic, this->shadeId);
  }
  else
    snprintf(topic, sizeof(topic), "%s/switch/%d/config", settings.MQTT.discoTopic, this->shadeId);
  mqtt.unpublish(topic);
}
void SomfyShade::publish() {
  if(mqtt.connected()) {
    this->publish("shadeId", this->shadeId, true);
    this->publish("name", this->name, true);
    this->publish("remoteAddress", this->getRemoteAddress(), true);
    this->publish("shadeType", static_cast<uint8_t>(this->shadeType), true);
    this->publish("tiltType", static_cast<uint8_t>(this->tiltType), true);
    this->publish("flags", this->flags, true);
    this->publish("flipCommands", this->flipCommands, true);
    this->publish("flipPosition", this->flipPosition, true);
    this->publishState();
    this->publishDisco();
    sockEmit.loop(); // Keep our socket alive.
  }
}
void SomfyGroup::publishState() {
  if(mqtt.connected()) {
    this->publish("direction", this->direction, true);
    this->publish("lastRollingCode", this->lastRollingCode, true);
    this->publish("flipCommands", this->flipCommands, true);
    const uint8_t sunFlag = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::SunFlag));
    const uint8_t isSunny = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny));
    const uint8_t isWindy = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::Windy));
    this->publish("sunFlag", sunFlag);
    this->publish("sunny", isSunny);
    this->publish("windy", isWindy);    
  }  
}
void SomfyGroup::publish() {
  if(mqtt.connected()) {
    this->publish("groupId", this->groupId, true);
    this->publish("name", this->name, true);
    this->publish("remoteAddress", this->getRemoteAddress(), true);
    this->publish("groupType", static_cast<uint8_t>(this->groupType), true);
    this->publish("flags", this->flags, true);
    this->publish("sunSensor", this->hasSunSensor(), true);
    this->publishState();
  }
}
char mqttTopicBuffer[55];
void SomfyGroup::unpublish() { SomfyGroup::unpublish(this->groupId); }
void SomfyShade::unpublish() { SomfyShade::unpublish(this->shadeId); }
void SomfyShade::unpublish(uint8_t id) {
  if(mqtt.connected()) {
    SomfyShade::unpublish(id, "shadeId");
    SomfyShade::unpublish(id, "name");
    SomfyShade::unpublish(id, "remoteAddress");
    SomfyShade::unpublish(id, "shadeType");
    SomfyShade::unpublish(id, "tiltType");
    SomfyShade::unpublish(id, "flags");
    SomfyShade::unpublish(id, "flipCommands");
    SomfyShade::unpublish(id, "flipPosition");
    SomfyShade::unpublish(id, "position");
    SomfyShade::unpublish(id, "direction");
    SomfyShade::unpublish(id, "target");
    SomfyShade::unpublish(id, "lastRollingCode");
    SomfyShade::unpublish(id, "mypos");
    SomfyShade::unpublish(id, "myTiltPos");
    SomfyShade::unpublish(id, "tiltDirection");
    SomfyShade::unpublish(id, "tiltPosition");
    SomfyShade::unpublish(id, "tiltTarget");
    SomfyShade::unpublish(id, "windy");
    SomfyShade::unpublish(id, "sunny");
    if(settings.MQTT.pubDisco) {
      char topic[128] = "";
      snprintf(topic, sizeof(topic), "%s/cover/%d/config", settings.MQTT.discoTopic, id);
      mqtt.unpublish(topic);
      snprintf(topic, sizeof(topic), "%s/switch/%d/config", settings.MQTT.discoTopic, id);
      mqtt.unpublish(topic);
    }
  }
}
void SomfyGroup::unpublish(uint8_t id) {
  if(mqtt.connected()) {
    SomfyGroup::unpublish(id, "groupId");
    SomfyGroup::unpublish(id, "name");
    SomfyGroup::unpublish(id, "remoteAddress");
    SomfyGroup::unpublish(id, "groupType");
    SomfyGroup::unpublish(id, "direction");
    SomfyGroup::unpublish(id, "lastRollingCode");
    SomfyGroup::unpublish(id, "flags");
    SomfyGroup::unpublish(id, "SunSensor");
    SomfyGroup::unpublish(id, "flipCommands");
  }
}
void SomfyGroup::unpublish(uint8_t id, const char *topic) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", id, topic);
    mqtt.unpublish(mqttTopicBuffer);
  }
}
void SomfyShade::unpublish(uint8_t id, const char *topic) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", id, topic);
    mqtt.unpublish(mqttTopicBuffer);
  }
}
bool SomfyShade::publish(const char *topic, int8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}

bool SomfyShade::publish(const char *topic, const char *val, bool retain) { 
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyShade::publish(const char *topic, uint8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyShade::publish(const char *topic, uint32_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyShade::publish(const char *topic, uint16_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyShade::publish(const char *topic, bool val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}

bool SomfyGroup::publish(const char *topic, int8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyGroup::publish(const char *topic, uint8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyGroup::publish(const char *topic, uint32_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyGroup::publish(const char *topic, uint16_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyGroup::publish(const char *topic, bool val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
// State Setters
float SomfyShade::p_currentPos(float pos) {
  float old = this->currentPos;
  this->currentPos = pos;
  if(floor(old) != floor(pos)) this->publish("position", this->transformPosition(static_cast<uint8_t>(floor(this->currentPos))));
  return old;
}
float SomfyShade::p_currentTiltPos(float pos) {
  float old = this->currentTiltPos;
  this->currentTiltPos = pos;
  if(floor(old) != floor(pos)) this->publish("tiltPosition", this->transformPosition(static_cast<uint8_t>(floor(this->currentTiltPos))));
  return old;
}
uint16_t SomfyShade::p_lastRollingCode(uint16_t code) {
  uint16_t old = SomfyRemote::p_lastRollingCode(code);
  if(old != code) this->publish("lastRollingCode", code);
  return old;
}
bool SomfyShade::p_flag(somfy_flags_t flag, bool val) {
  bool old = !!(this->flags & static_cast<uint8_t>(flag));
  if(val) 
      this->flags |= static_cast<uint8_t>(flag);
  else
      this->flags &= ~(static_cast<uint8_t>(flag));
  return old;
}
bool SomfyShade::p_sunFlag(bool val) {
  bool old = this->p_flag(somfy_flags_t::SunFlag, val);
  if(old != val) this->publish("sunFlag", static_cast<uint8_t>(val));
  return old;
}
bool SomfyShade::p_windy(bool val) {
  bool old = this->p_flag(somfy_flags_t::Windy, val);
  if(old != val) this->publish("windy", static_cast<uint8_t>(val));
  return old;
}
bool SomfyShade::p_sunny(bool val) {
  bool old = this->p_flag(somfy_flags_t::Sunny, val);
  if(old != val) this->publish("sunny", static_cast<uint8_t>(val));
  return old;
}
int8_t SomfyShade::p_direction(int8_t dir) {
  int8_t old = this->direction;
  if(old != dir) {
    this->direction = dir;
    this->publish("direction", this->direction, true);
  }
  return old;
}
int8_t SomfyGroup::p_direction(int8_t dir) {
  int8_t old = this->direction;
  if(old != dir) {
    this->direction = dir;
    this->publish("direction", this->direction);
  }
  return old;
}
int8_t SomfyShade::p_tiltDirection(int8_t dir) {
  int8_t old = this->tiltDirection;
  if(old != dir) {
    this->tiltDirection = dir;
    this->publish("tiltDirection", this->tiltDirection, true);
  }
  return old;
}
float SomfyShade::p_target(float target) {
  float old = this->target;
  if(old != target) {
    this->target = target;
    if(this->transformPosition(old) != this->transformPosition(target))
      this->publish("target", this->transformPosition(this->target), true);
  }
  return old;
}
float SomfyShade::p_tiltTarget(float target) {
  float old = this->tiltTarget;
  if(old != target) {
    this->tiltTarget = target;
    if(this->transformPosition(old) != this->transformPosition(target))
      this->publish("tiltTarget", this->transformPosition(this->tiltTarget), true);
  }
  return old;
}
float SomfyShade::p_myPos(float pos) {
  float old = this->myPos;
  if(old != pos) {
    //if(this->transformPosition(pos) == 0) Serial.println("MyPos = %.2f", pos);
    this->myPos = pos;
    if(this->transformPosition(old) != this->transformPosition(pos))
      this->publish("mypos", this->transformPosition(this->myPos), true);
  }
  return old;
}
float SomfyShade::p_myTiltPos(float pos) {
  float old = this->myTiltPos;
  if(old != pos) {
    this->myTiltPos = pos;
    if(this->transformPosition(old) != this->transformPosition(pos))
      this->publish("myTiltPos", this->transformPosition(this->myTiltPos), true);
  }
  return old;
}

void SomfyShade::emitState(const char *evt) { this->emitState(255, evt); }
void SomfyShade::emitState(uint8_t num, const char *evt) {
  JsonSockEvent *json = sockEmit.beginEmit(evt);
  json->beginObject();
  json->addElem("shadeId", this->shadeId);
  json->addElem("type", static_cast<uint8_t>(this->shadeType));
  json->addElem("remoteAddress", (uint32_t)this->getRemoteAddress());
  json->addElem("name", this->name);
  json->addElem("direction", this->direction);
  json->addElem("position", this->transformPosition(this->currentPos));
  json->addElem("target", this->transformPosition(this->target));
  json->addElem("myPos", this->transformPosition(this->myPos));
  json->addElem("tiltType", static_cast<uint8_t>(this->tiltType));
  json->addElem("flipCommands", this->flipCommands);
  json->addElem("flipPosition", this->flipPosition);
  json->addElem("flags", this->flags);
  json->addElem("sunSensor", this->hasSunSensor());
  json->addElem("light", this->hasLight());
  json->addElem("sortOrder", this->sortOrder);
  if(this->tiltType != tilt_types::none) {
    json->addElem("tiltDirection", this->tiltDirection);
    json->addElem("tiltTarget", this->transformPosition(this->tiltTarget));
    json->addElem("tiltPosition", this->transformPosition(this->currentTiltPos));
    json->addElem("myTiltPos", this->transformPosition(this->myTiltPos));
  }
  json->endObject();
  sockEmit.endEmit(num);
  /*
  char buf[420];
  if(this->tiltType != tilt_types::none)
    snprintf(buf, sizeof(buf), "{\"shadeId\":%d,\"type\":%u,\"remoteAddress\":%d,\"name\":\"%s\",\"direction\":%d,\"position\":%d,\"target\":%d,\"myPos\":%d,\"myTiltPos\":%d,\"tiltType\":%u,\"tiltDirection\":%d,\"tiltTarget\":%d,\"tiltPosition\":%d,\"flipCommands\":%s,\"flipPosition\":%s,\"flags\":%d,\"sunSensor\":%s,\"light\":%s,\"sortOrder\":%d}", 
      this->shadeId, static_cast<uint8_t>(this->shadeType), this->getRemoteAddress(), this->name, this->direction, 
      this->transformPosition(this->currentPos), this->transformPosition(this->target), this->transformPosition(this->myPos), this->transformPosition(this->myTiltPos), static_cast<uint8_t>(this->tiltType), this->tiltDirection, 
      this->transformPosition(this->tiltTarget), this->transformPosition(this->currentTiltPos),
      this->flipCommands ? "true" : "false", this->flipPosition ? "true": "false", this->flags, this->hasSunSensor() ? "true" : "false", this->hasLight() ? "true" : "false", this->sortOrder);
  else
    snprintf(buf, sizeof(buf), "{\"shadeId\":%d,\"type\":%u,\"remoteAddress\":%d,\"name\":\"%s\",\"direction\":%d,\"position\":%d,\"target\":%d,\"myPos\":%d,\"tiltType\":%u,\"flipCommands\":%s,\"flipPosition\":%s,\"flags\":%d,\"sunSensor\":%s,\"light\":%s,\"sortOrder\":%d}", 
      this->shadeId, static_cast<uint8_t>(this->shadeType), this->getRemoteAddress(), this->name, this->direction, 
      this->transformPosition(this->currentPos), this->transformPosition(this->target), this->transformPosition(this->myPos), 
      static_cast<uint8_t>(this->tiltType), this->flipCommands ? "true" : "false", this->flipPosition ? "true": "false", this->flags, this->hasSunSensor() ? "true" : "false", this->hasLight() ? "true" : "false", this->sortOrder);
  if(num >= 255) sockEmit.sendToClients(evt, buf);
  else sockEmit.sendToClient(num, evt, buf);
  */
}
void SomfyShade::emitCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt) { this->emitCommand(255, cmd, source, sourceAddress, evt); }
void SomfyShade::emitCommand(uint8_t num, somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt) {
  JsonSockEvent *json = sockEmit.beginEmit(evt);
  json->beginObject();
  json->addElem("shadeId", this->shadeId);
  json->addElem("remoteAddress", (uint32_t)this->getRemoteAddress());
  json->addElem("cmd", translateSomfyCommand(cmd).c_str());
  json->addElem("source", source);
  json->addElem("rcode", (uint32_t)this->lastRollingCode);
  json->addElem("sourceAddress", (uint32_t)sourceAddress);
  json->endObject();
  sockEmit.endEmit(num);
  /*
  ClientSocketEvent e(evt);
  char buf[30];
  snprintf(buf, sizeof(buf), "{\"shadeId\":%d", this->shadeId);
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), ",\"remoteAddress\":%d", this->getRemoteAddress());
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), ",\"cmd\":\"%s\"", translateSomfyCommand(cmd).c_str());
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), ",\"source\":\"%s\"", source);
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), ",\"rcode\":%d", this->lastRollingCode);
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), ",\"sourceAddress\":%d}", sourceAddress);
  e.appendMessage(buf);
  if(num >= 255) sockEmit.sendToClients(&e);
  else sockEmit.sendToClient(num, &e);
  */
  if(mqtt.connected()) {
    this->publish("cmdSource", source);
    this->publish("cmdAddress", sourceAddress);
    this->publish("cmd", translateSomfyCommand(cmd).c_str());
  }
}
void SomfyRoom::emitState(const char *evt) { this->emitState(255, evt); }
void SomfyRoom::emitState(uint8_t num, const char *evt) {
  JsonSockEvent *json = sockEmit.beginEmit(evt);
  json->beginObject();
  json->addElem("roomId", this->roomId);
  json->addElem("name", this->name);
  json->addElem("sortOrder", this->sortOrder);
  json->endObject();
  sockEmit.endEmit(num);
  /*
  ClientSocketEvent e(evt);
  char buf[55];
  uint8_t flags = 0;
  snprintf(buf, sizeof(buf), "{\"roomId\":%d,", this->roomId);
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), "\"name\":\"%s\",", this->name);
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), "\"sortOrder\":%d}", this->sortOrder);
  e.appendMessage(buf);
  if(num >= 255) sockEmit.sendToClients(&e);
  else sockEmit.sendToClient(num, &e);
  */
  this->publish();
}
void SomfyGroup::emitState(const char *evt) { this->emitState(255, evt); }
void SomfyGroup::emitState(uint8_t num, const char *evt) {
  uint8_t flags = 0;
  JsonSockEvent *json = sockEmit.beginEmit(evt);
  json->beginObject();
  json->addElem("groupId", this->groupId);
  json->addElem("remoteAddress", (uint32_t)this->getRemoteAddress());
  json->addElem("name", this->name);
  json->addElem("sunSensor", this->hasSunSensor());
  json->beginArray("shades");
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] != 255 && this->linkedShades[i] != 0) {
      SomfyShade *shade = somfy.getShadeById(this->linkedShades[i]);
      if(shade) json->addElem(this->linkedShades[i]);
      flags |= shade->flags;
    }
  }
  json->endArray();
  json->addElem("flags", flags);
  json->endObject();
  sockEmit.endEmit(num);
  /*
  ClientSocketEvent e(evt);
  char buf[55];
  uint8_t flags = 0;
  snprintf(buf, sizeof(buf), "{\"groupId\":%d,", this->groupId);
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), "\"remoteAddress\":%d,", this->getRemoteAddress());
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), "\"name\":\"%s\",", this->name);
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), "\"sunSensor\":%s,", this->hasSunSensor() ? "true" : "false");
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), "\"shades\":[");
  e.appendMessage(buf);
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] != 255) {
      if(this->linkedShades[i] != 0) {
        SomfyShade *shade = somfy.getShadeById(this->linkedShades[i]);
        if(shade) {
          flags |= shade->flags;
          snprintf(buf, sizeof(buf), "%s%d", i != 0 ? "," : "", this->linkedShades[i]);
          e.appendMessage(buf);
        }
      }
    }
  }
  snprintf(buf, sizeof(buf), "],\"flags\":%d}", flags);
  e.appendMessage(buf);
  
  if(num >= 255) sockEmit.sendToClients(&e);
  else sockEmit.sendToClient(num, &e);
  */
  this->publish();
}
int8_t SomfyShade::transformPosition(float fpos) { 
  if(fpos < 0) return -1;
  return static_cast<int8_t>(this->flipPosition && fpos >= 0.00f ? floor(100.0f - fpos) : floor(fpos)); 
}
bool SomfyShade::isIdle() { 
  return this->isAtTarget() && this->direction == 0 && this->tiltDirection == 0; 
}
void SomfyShade::processWaitingFrame() {
  if(this->shadeId == 255) {
    this->lastFrame.await = 0; 
    return;
  }
  if(this->lastFrame.processed) return;
  if(this->lastFrame.await > 0 && (millis() > this->lastFrame.await)) {
    somfy_commands cmd = this->transformCommand(this->lastFrame.cmd);
    switch(cmd) {
      case somfy_commands::StepUp:
          this->lastFrame.processed = true;
          // Simply move the shade up by 1%.
          if(this->currentPos > 0) {
            this->p_target(floor(this->currentPos) - 1);
            this->setMovement(-1);
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          break;
      case somfy_commands::StepDown:
          this->lastFrame.processed = true;
          // Simply move the shade down by 1%.
          if(this->currentPos < 100) {
            this->p_target(floor(this->currentPos) + 1);
            this->setMovement(1);
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          break;
      case somfy_commands::Down:
      case somfy_commands::Up:
        if(this->tiltType == tilt_types::tiltmotor) { // Theoretically this should get here unless it does have a tilt motor.
          if(this->lastFrame.repeats >= TILT_REPEATS) {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_tiltTarget(dir > 0 ? 100.0f : 0.0f);
            this->setTiltMovement(dir);
            this->lastFrame.processed = true;
            Serial.print(this->name);
            Serial.print(" Processing tilt ");
            Serial.print(translateSomfyCommand(this->lastFrame.cmd));
            Serial.print(" after ");
            Serial.print(this->lastFrame.repeats);
            Serial.println(" repeats");
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          else {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_target(dir > 0 ? 100 : 0);
            this->setMovement(dir);
            this->lastFrame.processed = true;
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          if(this->lastFrame.repeats > TILT_REPEATS + 2) {
            this->lastFrame.processed = true;
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
        }
        else if(this->tiltType == tilt_types::euromode) {
          if(this->lastFrame.repeats >= TILT_REPEATS) {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_target(dir > 0 ? 100.0f : 0.0f);
            this->setMovement(dir);
            this->lastFrame.processed = true;
            Serial.print(this->name);
            Serial.print(" Processing ");
            Serial.print(translateSomfyCommand(this->lastFrame.cmd));
            Serial.print(" after ");
            Serial.print(this->lastFrame.repeats);
            Serial.println(" repeats");
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          else {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_tiltTarget(dir > 0 ? 100 : 0);
            this->setTiltMovement(dir);
            this->lastFrame.processed = true;
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          if(this->lastFrame.repeats > TILT_REPEATS + 2) {
            this->lastFrame.processed = true;
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
        }
        break;
      case somfy_commands::My:
        if(this->lastFrame.repeats >= SETMY_REPEATS && this->isIdle()) {
          if(floor(this->myPos) == floor(this->currentPos)) {
            // We are clearing it.
            this->p_myPos(-1);
            this->p_myTiltPos(-1);
          }
          else {
            this->p_myPos(this->currentPos);
            this->p_myTiltPos(this->currentTiltPos);
          }
          this->commitMyPosition();
          this->lastFrame.processed = true;
          this->emitState();
        }
        else if(this->isIdle()) {
          if(this->simMy())
            this->moveToMyPosition(); // Call out like this (instead of move to target) so that we don't get some of the goofy tilt only problems.
          else {
            if(this->myPos >= 0.0f && this->myPos <= 100.0f) this->p_target(this->myPos);
            if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
          }
          this->setMovement(0);
          this->lastFrame.processed = true;
          this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
        }
        else {
          this->p_target(this->currentPos);
          this->p_tiltTarget(this->currentTiltPos);
        }
        if(this->lastFrame.repeats > SETMY_REPEATS + 2) this->lastFrame.processed = true;
        if(this->lastFrame.processed) {
          Serial.print(this->name);
          Serial.print(" Processing MY after ");
          Serial.print(this->lastFrame.repeats);
          Serial.println(" repeats");
        }
        break;
      default:
        break;
    }
  }
}
void SomfyShade::processFrame(somfy_frame_t &frame, bool internal) {
  // The reason why we are processing all frames here is so
  // any linked remotes that may happen to be on the same ESPSomfy RTS
  // device can trigger the appropriate actions.
  if(this->shadeId == 255) return; 
  bool hasRemote = this->getRemoteAddress() == frame.remoteAddress;
  if(!hasRemote) {
    for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
      if(this->linkedRemotes[i].getRemoteAddress() == frame.remoteAddress) {
        if(frame.cmd != somfy_commands::Sensor) this->linkedRemotes[i].setRollingCode(frame.rollingCode);
        hasRemote = true;
        break;      
      }
    }
  }
  if(!hasRemote) return;
  const uint64_t curTime = millis();
  this->lastFrame.copy(frame);
  int8_t dir = 0;
  this->moveStart = this->tiltStart = curTime;
  this->startPos = this->currentPos;
  this->startTiltPos = this->currentTiltPos;
  // If the command is coming from a remote then we are aborting all these positioning operations.
  if(!internal) this->settingMyPos = this->settingPos = this->settingTiltPos = false;
  somfy_commands cmd = this->transformCommand(frame.cmd);
  // At this point we are not processing the combo buttons
  // will need to see what the shade does when you press both.
  switch(cmd) {
    case somfy_commands::Sensor:
      this->lastFrame.processed = true;
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      {
        const uint8_t prevFlags = this->flags;
        const bool wasSunny = prevFlags & static_cast<uint8_t>(somfy_flags_t::Sunny);
        const bool wasWindy = prevFlags & static_cast<uint8_t>(somfy_flags_t::Windy);
        const uint16_t status = frame.rollingCode << 4;
        if (status & static_cast<uint8_t>(somfy_flags_t::Sunny))
          this->p_sunny(true);
          //this->flags |= static_cast<uint8_t>(somfy_flags_t::Sunny);
        else
          this->p_sunny(false);
          //this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::Sunny));
        if (status & static_cast<uint8_t>(somfy_flags_t::Windy))
          this->p_windy(true);
          //this->flags |= static_cast<uint8_t>(somfy_flags_t::Windy);
        else
          this->p_windy(false);
          //this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::Windy));
        if(frame.rollingCode & static_cast<uint8_t>(somfy_flags_t::DemoMode))
          this->flags |= static_cast<uint8_t>(somfy_flags_t::DemoMode);
        else
          this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::DemoMode));
        const bool isSunny = this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny);
        const bool isWindy = this->flags & static_cast<uint8_t>(somfy_flags_t::Windy);
        if (isSunny)
        {
          this->noSunStart = 0;
          this->noSunDone = true;
        }
        else
        {
          this->sunStart = 0;
          this->sunDone = true;
        }
        if (isWindy)
        {
          this->noWindStart = 0;
          this->noWindDone = true;
          this->windLast = curTime;
        }
        else
        {
          this->windStart = 0;
          this->windDone = true;
        }
        if (isSunny && !wasSunny)
        {
          this->sunStart = curTime;
          this->sunDone = false;
          Serial.printf("[%u] Sun -> start\r\n", this->shadeId);
        }
        else if (!isSunny && wasSunny)
        {
          this->noSunStart = curTime;
          this->noSunDone = false;
          Serial.printf("[%u] No Sun -> start\r\n", this->shadeId);
        }
        if (isWindy && !wasWindy)
        {
          this->windStart = curTime;
          this->windDone = false;
          Serial.printf("[%u] Wind -> start\r\n", this->shadeId);
        }
        else if (!isWindy && wasWindy)
        {
          this->noWindStart = curTime;
          this->noWindDone = false;
          Serial.printf("[%u] No Wind -> start\r\n", this->shadeId);
        }
        this->emitState();
        somfy.updateGroupFlags();
      }
      break;
    case somfy_commands::Prog:
    case somfy_commands::MyUp:
    case somfy_commands::MyDown:
    case somfy_commands::MyUpDown:
    case somfy_commands::UpDown:
      this->lastFrame.processed = true;
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
      
    case somfy_commands::Flag:
      this->lastFrame.processed = true;
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      if(this->lastFrame.rollingCode & 0x8000) return; // Some sensors send bogus frames with a rollingCode >= 32768 that cause them to change the state.
      this->p_sunFlag(false);
      //this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::SunFlag));
      somfy.isDirty = true;
      this->emitState();
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      somfy.updateGroupFlags();
      break;    
    case somfy_commands::SunFlag:
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      if(this->lastFrame.rollingCode & 0x8000) return; // Some sensors send bogus frames with a rollingCode >= 32768 that cause them to change the state.
      {
        const bool isWindy = this->flags & static_cast<uint8_t>(somfy_flags_t::Windy);
        //this->flags |= static_cast<uint8_t>(somfy_flags_t::SunFlag);
        this->p_sunFlag(true);
        if (!isWindy)
        {
          const bool isSunny = this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny);
          if (isSunny && this->sunDone) {
            if(this->tiltType == tilt_types::tiltonly)
              this->p_tiltTarget(this->myTiltPos >= 0 ? this->myTiltPos : 100.0f);
            else
              this->p_target(this->myPos >= 0 ? this->myPos : 100.0f);
          }
          else if (!isSunny && this->noSunDone) {
            if(this->tiltType == tilt_types::tiltonly)
              this->p_tiltTarget(0.0f);
            else
              this->p_target(0.0f);
          }
        }
        somfy.isDirty = true;
        this->emitState();
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        somfy.updateGroupFlags();
      }
      break;
    case somfy_commands::Up:
      if(this->shadeType == shade_types::drycontact) {
        this->lastFrame.processed = true;
        return;
      }
      else if(this->shadeType == shade_types::drycontact2) {
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        if(this->currentPos != 0.0f) this->p_target(0);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        return;
      }
      if(this->tiltType == tilt_types::tiltmotor || this->tiltType == tilt_types::euromode) {
        // Wait another half second just in case we are potentially processing a tilt.
        if(!internal) this->lastFrame.await = curTime + 500;
        else this->lastFrame.processed = true;
      }
      else {
        // If from a remote we will simply be going up.
        if(this->tiltType == tilt_types::tiltonly && !internal) this->p_tiltTarget(0.0f);
        else if(!internal) {
          if(this->tiltType != tilt_types::tiltonly) this->p_target(0.0f);
          this->p_tiltTarget(0.0f);
        }
        this->lastFrame.processed = true;
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      }
      break;
    case somfy_commands::Down:
      if(this->shadeType == shade_types::drycontact) {
        this->lastFrame.processed = true;
        return;
      }
      else if(this->shadeType == shade_types::drycontact2) {
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        if(this->currentPos != 100.0f) this->p_target(100);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        return;
      }
      if (!this->windLast || (curTime - this->windLast) >= SOMFY_NO_WIND_REMOTE_TIMEOUT) {
        if(this->tiltType == tilt_types::tiltmotor || this->tiltType == tilt_types::euromode) {
          // Wait another half seccond just in case we are potentially processing a tilt.
          if(!internal) this->lastFrame.await = curTime + 500;
          else this->lastFrame.processed = true;
        }
        else {
          this->lastFrame.processed = true;
          if(!internal) {
            if(this->tiltType != tilt_types::tiltonly) this->p_target(100.0f);
            if(this->tiltType != tilt_types::none) this->p_tiltTarget(100.0f);
          }
        }
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      }
      break;
    case somfy_commands::My:
      if(this->shadeType == shade_types::drycontact2) return;
      if(this->isToggle()) { // This is a one button device
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        if(!this->isIdle()) this->p_target(this->currentPos);
        else if(this->currentPos == 100.0f) this->p_target(0.0f);
        else if(this->currentPos == 0.0f) this->p_target(100.0f);
        else this->p_target(this->lastMovement == -1 ? 100 : 0);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        return;
      }
      else if(this->shadeType == shade_types::drycontact) {
        // In this case we need to toggle the contact but we only should do this if
        // this is not a repeat.
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        if(this->currentPos == 100.0f) this->p_target(0);
        else if(this->currentPos == 0.0f) this->p_target(100);
        else this->p_target(this->lastMovement == -1 ? 100 : 0);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        return;
      }
      if(this->isIdle()) {
        if(!internal) {
          // This frame is coming from a remote. We are potentially setting
          // the my position.
          this->lastFrame.await = curTime + 500;
        }
        else {
          if(this->lastFrame.processed) return;
          Serial.println("Moving to My target");
          this->lastFrame.processed = true;
          if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
          if(this->myPos >= 0.0f && this->myPos <= 100.0f && this->tiltType != tilt_types::tiltonly) this->p_target(this->myPos);
          this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        }
      }
      else {
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        if(!internal) {
          if(this->tiltType != tilt_types::tiltonly) this->p_target(this->currentPos);
          this->p_tiltTarget(this->currentTiltPos);
        }
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      }
      break;
    case somfy_commands::StepUp:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      dir = 0;
      // With the step commands and integrated shades
      // the motor must tilt in the direction first then move
      // so we have to calculate the target with this in mind.
      if(this->stepSize == 0) return; // Avoid divide by 0.
      if(this->lastFrame.stepSize == 0) this->lastFrame.stepSize = 1;
      if(this->tiltType == tilt_types::integrated) {
        // With integrated tilt this is more involved than ne would think because the step command can be moving not just the tilt
        // but the lift.  So a determination needs to be made as to whether we are currently moving and it should stop.
        // Conditions:
        // 1. If both the tilt and lift are at 0% do nothing
        // 2. If the tilt position is not currently at the top then shift the tilt.
        // 3. If the tilt position is not currently at the top then shift the lift.
        if(this->currentTiltPos <= 0.0f && this->currentPos <= 0.0f) return; // Do nothing
        else if(this->currentTiltPos > 0.0f) {
          // Set the tilt position.  This should stop the lift movement.
          this->p_target(this->currentPos);
          if(this->tiltTime == 0) return; // Avoid divide by 0.
          this->p_tiltTarget(max(0.0f, this->currentTiltPos - (100.0f/(static_cast<float>(this->tiltTime/static_cast<float>(this->stepSize * this->lastFrame.stepSize))))));
        }
        else {
          // We only have the lift to move.
          if(this->upTime == 0) return; // Avoid divide by 0.
          this->p_tiltTarget(this->currentTiltPos);
          this->p_target(max(0.0f, this->currentPos - (100.0f/(static_cast<float>(this->upTime/static_cast<float>(this->stepSize * this->lastFrame.stepSize))))));
        }
      }
      else if(this->tiltType == tilt_types::tiltonly) {
        if(this->tiltTime == 0 || this->stepSize == 0) return;
        this->p_tiltTarget(max(0.0f, this->currentTiltPos - (100.0f/(static_cast<float>(this->tiltTime/static_cast<float>(this->stepSize * this->lastFrame.stepSize))))));
      }
      else if(this->currentPos > 0.0f) {
        if(this->downTime == 0 || this->stepSize == 0) return;
        this->p_target(max(0.0f, this->currentPos - (100.0f/(static_cast<float>(this->upTime/static_cast<float>(this->stepSize * this->lastFrame.stepSize))))));
      }
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
    case somfy_commands::StepDown:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      dir = 1;
      // With the step commands and integrated shades
      // the motor must tilt in the direction first then move
      // so we have to calculate the target with this in mind.
      if(this->stepSize == 0) return; // Avoid divide by 0.
      if(this->lastFrame.stepSize == 0) this->lastFrame.stepSize = 1;
      
      if(this->tiltType == tilt_types::integrated) {
        // With integrated tilt this is more involved than ne would think because the step command can be moving not just the tilt
        // but the lift.  So a determination needs to be made as to whether we are currently moving and it should stop.
        // Conditions:
        // 1. If both the tilt and lift are at 100% do nothing
        // 2. If the tilt position is not currently at the bottom then shift the tilt.
        // 3. If the tilt position is add the bottom then shift the lift.
        if(this->currentTiltPos >= 100.0f && this->currentPos >= 100.0f) return; // Do nothing
        else if(this->currentTiltPos < 100.0f) {
          // Set the tilt position.  This should stop the lift movement.
          this->p_target(this->currentPos);
          if(this->tiltTime == 0) return; // Avoid divide by 0.
          this->p_tiltTarget(min(100.0f, this->currentTiltPos + (100.0f/(static_cast<float>(this->tiltTime/static_cast<float>(this->stepSize * this->lastFrame.stepSize))))));
        }
        else {
          // We only have the lift to move.
          this->p_tiltTarget(this->currentTiltPos);
          if(this->downTime == 0) return; // Avoid divide by 0.
          this->p_target(min(100.0f, this->currentPos + (100.0f/(static_cast<float>(this->downTime/static_cast<float>(this->stepSize* this->lastFrame.stepSize))))));
        }
      }
      else if(this->tiltType == tilt_types::tiltonly) {
        if(this->tiltTime == 0 || this->stepSize == 0) return;
        this->p_target(min(100.0f, this->currentTiltPos + (100.0f/(static_cast<float>(this->tiltTime/static_cast<float>(this->stepSize * this->lastFrame.stepSize))))));
      }
      else if(this->currentPos < 100.0f) {
        if(this->downTime == 0 || this->stepSize == 0) return;
        this->p_target(min(100.0f, this->currentPos + (100.0f/(static_cast<float>(this->downTime/static_cast<float>(this->stepSize * this->lastFrame.stepSize))))));
      }
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
    case somfy_commands::Toggle:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      if(!this->isIdle()) this->p_target(this->currentPos);
      else if(this->currentPos == 100.0f) this->p_target(0);
      else if(this->currentPos == 0.0f) this->p_target(100);
      else this->p_target(this->lastMovement == -1 ? 100 : 0);
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
    case somfy_commands::Stop:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      this->p_target(this->currentPos);
      this->p_tiltTarget(this->currentTiltPos);      
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
    case somfy_commands::Favorite:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      if(this->simMy()) {
        this->moveToMyPosition();
      }
      else {
        if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
        if(this->myPos >= 0.0f && this->myPos <= 100.0f && this->tiltType != tilt_types::tiltonly) this->p_target(this->myPos);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      }
      break;
    default:
      dir = 0;
      break;
  }
  //if(dir == 0 && this->tiltType == tilt_types::tiltmotor && this->tiltDirection != 0) this->setTiltMovement(0);
  this->setMovement(dir);
}
void SomfyShade::processInternalCommand(somfy_commands cmd, uint8_t repeat) {
  // The reason why we are processing all frames here is so
  // any linked remotes that may happen to be on the same ESPSomfy RTS
  // device can trigger the appropriate actions.
  if(this->shadeId == 255) return; 
  const uint64_t curTime = millis();
  int8_t dir = 0;
  this->moveStart = this->tiltStart = curTime;
  this->startPos = this->currentPos;
  this->startTiltPos = this->currentTiltPos;
  // If the command is coming from a remote then we are aborting all these positioning operations.
  switch(cmd) {
    case somfy_commands::Up:
      if(this->tiltType == tilt_types::tiltmotor) {
        if(repeat >= TILT_REPEATS)
          this->p_tiltTarget(0.0f);
        else
          this->p_target(0.0f);
      }
      else if(this->tiltType == tilt_types::tiltonly) {
        this->p_target(100.0f);
        this->p_currentPos(100.0f);
        this->p_tiltTarget(0.0f);
      }
      else {
        this->p_target(0.0f);
        this->p_tiltTarget(0.0f);
      }
      break;
    case somfy_commands::Down:
      if (!this->windLast || (curTime - this->windLast) >= SOMFY_NO_WIND_REMOTE_TIMEOUT) {
        if(this->tiltType == tilt_types::tiltmotor) {
          if(repeat >= TILT_REPEATS)
            this->p_tiltTarget(100.0f);
          else
            this->p_target(100.0f);
        }
        else if(this->tiltType == tilt_types::tiltonly) {
          this->p_target(100.0f);
          this->p_currentPos(100.0f);
          this->p_tiltTarget(100.0f);
        }
        else {
            this->p_target(100.0f);
            if(this->tiltType != tilt_types::none) this->p_tiltTarget(100.0f);
        }
      }
      break;
    case somfy_commands::My:
      if(this->isIdle()) {
        Serial.printf("Shade #%d is idle\n", this->getShadeId());
        if(this->simMy()) {
          this->moveToMyPosition();
        }
        else {
          if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
          if(this->myPos >= 0.0f && this->myPos <= 100.0f && this->tiltType != tilt_types::tiltonly) this->p_target(this->myPos);
        }
      }
      else {
        if(this->tiltType == tilt_types::tiltonly) {
          this->p_target(100.0f);
        }
        else this->p_target(this->currentPos);
        this->p_tiltTarget(this->currentTiltPos);
      }
      break;
    case somfy_commands::StepUp:
      // With the step commands and integrated shades
      // the motor must tilt in the direction first then move
      // so we have to calculate the target with this in mind.
      if(this->stepSize == 0) return; // Avoid divide by 0.
      if(this->tiltType == tilt_types::integrated) {
        // With integrated tilt this is more involved than ne would think because the step command can be moving not just the tilt
        // but the lift.  So a determination needs to be made as to whether we are currently moving and it should stop.
        // Conditions:
        // 1. If both the tilt and lift are at 0% do nothing
        // 2. If the tilt position is not currently at the top then shift the tilt.
        // 3. If the tilt position is not currently at the top then shift the lift.
        if(this->currentTiltPos <= 0.0f && this->currentPos <= 0.0f) return; // Do nothing
        else if(this->currentTiltPos > 0.0f) {
          // Set the tilt position.  This should stop the lift movement.
          this->p_target(this->currentPos);
          if(this->tiltTime == 0) return; // Avoid divide by 0.
          this->p_tiltTarget(max(0.0f, this->currentTiltPos - (100.0f/(static_cast<float>(this->tiltTime/static_cast<float>(this->stepSize))))));
        }
        else {
          // We only have the lift to move.
          if(this->upTime == 0) return; // Avoid divide by 0.
          this->p_tiltTarget(this->currentTiltPos);
          this->p_target(max(0.0f, this->currentPos - (100.0f/(static_cast<float>(this->upTime/static_cast<float>(this->stepSize))))));
        }
      }
      else if(this->tiltType == tilt_types::tiltonly) {
        if(this->tiltTime == 0 || this->currentTiltPos <= 0.0f) return;
        this->p_tiltTarget(max(0.0f, this->currentTiltPos - (100.0f/(static_cast<float>(this->tiltTime/static_cast<float>(this->stepSize))))));
      }
      else if(this->currentPos > 0.0f) {
        if(this->upTime == 0) return;
        this->p_target(max(0.0f, this->currentPos - (100.0f/(static_cast<float>(this->upTime/static_cast<float>(this->stepSize))))));
      }
      break;
    case somfy_commands::StepDown:
      dir = 1;
      // With the step commands and integrated shades
      // the motor must tilt in the direction first then move
      // so we have to calculate the target with this in mind.
      if(this->stepSize == 0) return; // Avoid divide by 0.
      if(this->tiltType == tilt_types::integrated) {
        // With integrated tilt this is more involved than ne would think because the step command can be moving not just the tilt
        // but the lift.  So a determination needs to be made as to whether we are currently moving and it should stop.
        // Conditions:
        // 1. If both the tilt and lift are at 100% do nothing
        // 2. If the tilt position is not currently at the bottom then shift the tilt.
        // 3. If the tilt position is add the bottom then shift the lift.
        if(this->currentTiltPos >= 100.0f && this->currentPos >= 100.0f) return; // Do nothing
        else if(this->currentTiltPos < 100.0f) {
          // Set the tilt position.  This should stop the lift movement.
          this->p_target(this->currentPos);
          if(this->tiltTime == 0) return; // Avoid divide by 0.
          this->p_tiltTarget(min(100.0f, this->currentTiltPos + (100.0f/(static_cast<float>(this->tiltTime/static_cast<float>(this->stepSize))))));
        }
        else {
          // We only have the lift to move.
          if(this->downTime == 0) return; // Avoid divide by 0.
          this->p_tiltTarget(this->currentTiltPos);
          this->p_target(min(100.0f, this->currentPos + (100.0f/(static_cast<float>(this->downTime/static_cast<float>(this->stepSize))))));
        }
      }
      else if(this->tiltType == tilt_types::tiltonly) {
        if(this->tiltTime == 0 || this->stepSize == 0 || this->currentTiltPos >= 100.0f) return;
        this->p_tiltTarget(min(100.0f, this->currentTiltPos + (100.0f/(static_cast<float>(this->tiltTime/static_cast<float>(this->stepSize))))));
      }
      else if(this->currentPos < 100.0f) {
        if(this->downTime == 0 || this->stepSize == 0) return;
        this->p_target(min(100.0f, this->currentPos + (100.0f/(static_cast<float>(this->downTime/static_cast<float>(this->stepSize))))));
      }
      break;
    case somfy_commands::Flag:
      this->p_sunFlag(false);
      if(this->hasSunSensor()) {
        somfy.isDirty = true;
        this->emitState();
      }
      else {
        Serial.printf("Shade does not have sensor %d\n", this->flags);
      }
      break;    
    case somfy_commands::SunFlag:
      if(this->hasSunSensor()) {
        const bool isWindy = this->flags & static_cast<uint8_t>(somfy_flags_t::Windy);
        this->p_sunFlag(true);
        //this->flags |= static_cast<uint8_t>(somfy_flags_t::SunFlag);
        if (!isWindy)
        {
          const bool isSunny = this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny);
          if (isSunny && this->sunDone)
            this->p_target(this->myPos >= 0 ? this->myPos : 100.0f);
          else if (!isSunny && this->noSunDone)
            this->p_target(0.0f);
        }
        somfy.isDirty = true;
        this->emitState();
      }
      else
        Serial.printf("Shade does not have sensor %d\n", this->flags);
      break;
    default:
      dir = 0;
      break;
  }
  this->setMovement(dir);
}
void SomfyShade::setTiltMovement(int8_t dir) {
  int8_t currDir = this->tiltDirection;
  if(dir == 0) {
    // The shade tilt is stopped.
    this->startTiltPos = this->currentTiltPos;
    this->tiltStart = 0;
    this->p_tiltDirection(dir);
    if(currDir != dir) {
      this->commitTiltPosition();
    }
  }
  else if(this->tiltDirection != dir) {
    this->tiltStart = millis();
    this->startTiltPos = this->currentTiltPos;
    this->p_tiltDirection(dir);
  }
  if(this->tiltDirection != currDir) {
    this->emitState();
  }
}
void SomfyShade::setMovement(int8_t dir) {
  int8_t currDir = this->direction;
  int8_t currTiltDir = this->tiltDirection;
  if(dir == 0) {
    if(currDir != dir || currTiltDir != dir) this->commitShadePosition();
  }
  else {
    this->tiltStart = this->moveStart = millis();
    this->startPos = this->currentPos;
    this->startTiltPos = this->currentTiltPos;
  }
  if(this->direction != currDir || currTiltDir != this->tiltDirection) {
    this->emitState();
  }
}
void SomfyShade::setMyPosition(int8_t pos, int8_t tilt) {
  if(!this->isIdle()) return; // Don't do this if it is moving.
  if(this->tiltType == tilt_types::tiltonly) {
    this->p_myPos(-1.0f);    
    if(tilt != floor(this->currentTiltPos)) {
      this->settingMyPos = true;
      if(tilt == floor(this->myTiltPos))
        this->moveToMyPosition();
      else 
        this->moveToTarget(100, tilt);
    }
    else if(tilt == floor(this->myTiltPos)) {
      // Of so we need to clear the my position. These motors are finicky so send
      // a my command to ensure we are actually at the my position then send the clear
      // command.  There really is no other way to do this.
      if(this->currentTiltPos != this->myTiltPos) {
        this->settingMyPos = true;
        this->moveToMyPosition();      
      }
      else {
        SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
        this->settingPos = false;
        this->settingMyPos = true;
      }
    }
    else {
      SomfyRemote::sendCommand(somfy_commands::My, SETMY_REPEATS);
      this->p_myTiltPos(this->currentTiltPos);
    }
    this->commitMyPosition();
    this->emitState();
  }
  else if(this->tiltType != tilt_types::none) {
      if(tilt < 0) tilt = 0;
      if(pos != floor(this->currentPos) || tilt != floor(this->currentTiltPos)) {
        this->settingMyPos = true;
        if(pos == floor(this->myPos) && tilt == floor(this->myTiltPos))
          this->moveToMyPosition();
        else
          this->moveToTarget(pos, tilt);
      }
      else if(pos == floor(this->myPos) && tilt == floor(this->myTiltPos)) {
        // Of so we need to clear the my position. These motors are finicky so send
        // a my command to ensure we are actually at the my position then send the clear
        // command.  There really is no other way to do this.
        if(this->currentPos != this->myPos || this->currentTiltPos != this->myTiltPos) {
          this->settingMyPos = true;
          this->moveToMyPosition();      
        }
        else {
          SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
          this->settingPos = false;
          this->settingMyPos = true;
        }
      }
      else {
        SomfyRemote::sendCommand(somfy_commands::My, SETMY_REPEATS);
        this->p_myPos(this->currentPos);
        this->p_myTiltPos(this->currentTiltPos);
      }
      this->commitMyPosition();
      this->emitState();
  }
  else {
    if(pos != floor(this->currentPos)) {
      this->settingMyPos = true;
      if(pos == floor(this->myPos))
        this->moveToMyPosition();
      else
        this->moveToTarget(pos);
    }
    else if(pos == floor(this->myPos)) {
      // Of so we need to clear the my position. These motors are finicky so send
      // a my command to ensure we are actually at the my position then send the clear
      // command.  There really is no other way to do this.
      if(this->myPos != this->currentPos) {
        this->settingMyPos = true;
        this->moveToMyPosition();      
      }
      else {
        SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
        this->settingPos = false;
        this->settingMyPos = true;
      }
    }
    else {
      SomfyRemote::sendCommand(somfy_commands::My, SETMY_REPEATS);
      this->p_myPos(currentPos);
      this->p_myTiltPos(-1);
      this->commitMyPosition();
      this->emitState();
    }
  }
}
void SomfyShade::moveToMyPosition() {
  if(!this->isIdle()) return;
  Serial.println("Moving to My Position");
  if(this->tiltType == tilt_types::tiltonly) {
    this->p_currentPos(100.0f);
    this->p_myPos(-1.0f);
  }
  if(this->currentPos == this->myPos) {
    if(this->tiltType != tilt_types::none) {
      if(this->currentTiltPos == this->myTiltPos) return; // Nothing to see here since we are already here.
    }
    else
      return;
  }
  if(this->myPos == -1 && (this->tiltType == tilt_types::none || this->myTiltPos == -1)) return;
  if(this->tiltType != tilt_types::tiltonly && this->myPos >= 0.0f && this->myPos <= 100.0f) this->p_target(this->myPos);
  if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
  this->settingPos = false;
  if(this->simMy()) {
    Serial.print("Moving to simulated favorite\n");
    this->moveToTarget(this->myPos, this->myTiltPos);
  }
  else
    SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
}
void SomfyShade::sendCommand(somfy_commands cmd) { this->sendCommand(cmd, this->repeats); }
void SomfyShade::sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize) {
  // This sendCommand function will always be called externally. sendCommand at the remote level
  // is expected to be called internally when the motor needs commanded.
  if(this->bitLength == 0) this->bitLength = somfy.transceiver.config.type;
  if(cmd == somfy_commands::Up) {
    if(this->tiltType == tilt_types::euromode) {
      // In euromode we need to long press for 2 seconds on the
      // up command.
      SomfyRemote::sendCommand(cmd, TILT_REPEATS);
      this->p_target(0.0f);     
    }
    else {
      SomfyRemote::sendCommand(cmd, repeat);
      if(this->tiltType == tilt_types::tiltonly) {
        this->p_target(100.0f);
        this->p_tiltTarget(0.0f);
        this->p_currentPos(100.0f);
      }
      else this->p_target(0.0f);
      if(this->tiltType == tilt_types::integrated) this->p_tiltTarget(0.0f);
    }
  }
  else if(cmd == somfy_commands::Down) {
    if(this->tiltType == tilt_types::euromode) {
      // In euromode we need to long press for 2 seconds on the
      // down command.
      SomfyRemote::sendCommand(cmd, TILT_REPEATS);
      this->p_target(100.0f);     
    }
    else {
      SomfyRemote::sendCommand(cmd, repeat);
      if(this->tiltType == tilt_types::tiltonly) {
        this->p_target(100.0f);
        this->p_tiltTarget(100.0f);
        this->p_currentPos(100.0f);
      }
      else this->p_target(100.0f);
      if(this->tiltType == tilt_types::integrated) this->p_tiltTarget(100.0f);
    }
  }
  else if(cmd == somfy_commands::My) {
    if(this->isToggle() || this->shadeType == shade_types::drycontact)
      SomfyRemote::sendCommand(cmd, repeat);
    else if(this->shadeType == shade_types::drycontact2) return;   
    else if(this->isIdle()) {
      this->moveToMyPosition();      
      return;
    }
    else {
      SomfyRemote::sendCommand(cmd, repeat);
      if(this->tiltType != tilt_types::tiltonly) this->p_target(this->currentPos);
      this->p_tiltTarget(this->currentTiltPos);
    }
  }
  else if(cmd == somfy_commands::Toggle) {
    if(this->bitLength != 80) SomfyRemote::sendCommand(somfy_commands::My, repeat, stepSize);
    else SomfyRemote::sendCommand(somfy_commands::Toggle, repeat);
  }
  else if(this->isToggle() && cmd == somfy_commands::Prog) {
    SomfyRemote::sendCommand(somfy_commands::Toggle, repeat, stepSize);
  }
  else {
    SomfyRemote::sendCommand(cmd, repeat, stepSize);
  }
}
void SomfyGroup::sendCommand(somfy_commands cmd) { this->sendCommand(cmd, this->repeats); }
void SomfyGroup::sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize) {
  // This sendCommand function will always be called externally. sendCommand at the remote level
  // is expected to be called internally when the motor needs commanded.
  if(this->bitLength == 0) this->bitLength = somfy.transceiver.config.type;
  SomfyRemote::sendCommand(cmd, repeat, stepSize);
  
  switch(cmd) {
    case somfy_commands::My:
      this->p_direction(0);
      break;
    case somfy_commands::Up:
      this->p_direction(-1);
      break;
    case somfy_commands::Down:
      this->p_direction(1);
      break;
    default:
      break;
  }
  
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] != 0) {
      SomfyShade *shade = somfy.getShadeById(this->linkedShades[i]);
      if(shade) {
        shade->processInternalCommand(cmd, repeat);
        shade->emitCommand(cmd, "group", this->getRemoteAddress());
      }
    }
  }
  this->updateFlags();
  this->emitState();
  
}  
void SomfyShade::sendTiltCommand(somfy_commands cmd) {
  if(cmd == somfy_commands::Up) {
    SomfyRemote::sendCommand(cmd, this->tiltType == tilt_types::tiltmotor ? TILT_REPEATS : this->repeats);
    this->p_tiltTarget(0.0f);
  }
  else if(cmd == somfy_commands::Down) {
    SomfyRemote::sendCommand(cmd, this->tiltType == tilt_types::tiltmotor ? TILT_REPEATS : this->repeats);
    this->p_tiltTarget(100.0f);
  }
  else if(cmd == somfy_commands::My) {
    SomfyRemote::sendCommand(cmd, this->tiltType == tilt_types::tiltmotor ? TILT_REPEATS : this->repeats);
    this->p_tiltTarget(this->currentTiltPos);
  }
}
void SomfyShade::moveToTiltTarget(float target) {
  somfy_commands cmd = somfy_commands::My;
  if(target < this->currentTiltPos)
    cmd = somfy_commands::Up;
  else if(target > this->currentTiltPos)
    cmd = somfy_commands::Down;
  if(target >= 0.0f && target <= 100.0f) {
    // Only send a command if the lift is not moving.
    if(this->currentPos == this->target || this->tiltType == tilt_types::tiltmotor) {
      if(cmd != somfy_commands::My) {
        Serial.print("Moving Tilt to ");
        Serial.print(target);
        Serial.print("% from ");
        Serial.print(this->currentTiltPos);
        Serial.print("% using ");
        Serial.println(translateSomfyCommand(cmd));
        SomfyRemote::sendCommand(cmd, this->tiltType == tilt_types::tiltmotor ? TILT_REPEATS : this->repeats);
      }
      // If the blind is currently moving then the command to stop it
      // will occur on its own when the tilt target is set.
    }
    this->p_tiltTarget(target);
  }
  if(cmd != somfy_commands::My) this->settingTiltPos = true;
}
void SomfyShade::moveToTarget(float pos, float tilt) {
  somfy_commands cmd = somfy_commands::My;
  if(this->isToggle()) {
    // Overload this as we cannot seek a position on a garage door or single button device.
    this->p_target(pos);
    this->p_currentPos(pos);
    this->emitState();
    return;
  }
  if(this->tiltType == tilt_types::tiltonly) {
    this->p_target(100.0f);
    this->p_myPos(-1.0f);
    this->p_currentPos(100.0f);
    pos = 100;
    if(tilt < this->currentTiltPos) cmd = somfy_commands::Up;
    else if(tilt > this->currentTiltPos) cmd = somfy_commands::Down;
  }
  else {
    if(pos < this->currentPos)
      cmd = somfy_commands::Up;
    else if(pos > this->currentPos)
      cmd = somfy_commands::Down;
    else if(tilt >= 0 && tilt < this->currentTiltPos)
      cmd = somfy_commands::Up;
    else if(tilt >= 0 && tilt > this->currentTiltPos)
      cmd = somfy_commands::Down;
  }
  if(cmd != somfy_commands::My) {
    Serial.print("Moving to ");
    Serial.print(pos);
    Serial.print("% from ");
    Serial.print(this->currentPos);
    if(tilt >= 0) {
      Serial.print(" tilt ");
      Serial.print(tilt);
      Serial.print("% from ");
      Serial.print(this->currentTiltPos);
    }
    Serial.print("% using ");
    Serial.println(translateSomfyCommand(cmd));
    SomfyRemote::sendCommand(cmd, this->tiltType == tilt_types::euromode ? TILT_REPEATS : this->repeats);
    this->settingPos = true;
    this->p_target(pos);
    if(tilt >= 0) {
      this->p_tiltTarget(tilt);
      this->settingTiltPos = true;
    }
  }
}
bool SomfyShade::save() {
  #ifdef USE_NVS
  if(somfy.useNVS()) {
    char shadeKey[15];
    snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", this->getShadeId());
    pref.begin(shadeKey);
    pref.clear();
    pref.putChar("shadeType", static_cast<uint8_t>(this->shadeType));
    pref.putUInt("remoteAddress", this->getRemoteAddress());
    pref.putString("name", this->name);
    pref.putBool("hasTilt", this->tiltType != tilt_types::none);
    pref.putBool("paired", this->paired);
    pref.putUInt("upTime", this->upTime);
    pref.putUInt("downTime", this->downTime);
    pref.putUInt("tiltTime", this->tiltTime);
    pref.putFloat("currentPos", this->currentPos);
    pref.putFloat("currentTiltPos", this->currentTiltPos);
    pref.putUShort("myPos", this->myPos);
    uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
    memset(linkedAddresses, 0x00, sizeof(linkedAddresses));
    uint8_t j = 0;
    for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
      SomfyLinkedRemote lremote = this->linkedRemotes[i];
      if(lremote.getRemoteAddress() != 0) linkedAddresses[j++] = lremote.getRemoteAddress();
    }
    pref.remove("linkedAddr");
    pref.putBytes("linkedAddr", linkedAddresses, sizeof(uint32_t) * SOMFY_MAX_LINKED_REMOTES);
    pref.end();
  }
  #endif
  this->commit();
  this->publish();
  return true;
}
bool SomfyRoom::save() { somfy.commit(); return true; }
bool SomfyGroup::save() { somfy.commit(); return true; }
bool SomfyShade::isToggle() {
  switch(this->shadeType) {
    case shade_types::garage1:
    case shade_types::lgate1:
    case shade_types::cgate1:
    case shade_types::rgate1:
      return true;
    default:
      break;
  }
  return false;
}
bool SomfyShade::usesPin(uint8_t pin) {
  if(this->proto != radio_proto::GP_Remote && this->proto != radio_proto::GP_Relay) return false;
  if(this->gpioDown == pin) return true;
  else if(this->shadeType == shade_types::drycontact)
    return this->gpioDown == pin;
  else if(this->isToggle()) {
    if(this->proto == radio_proto::GP_Relay && this->gpioUp == pin) return true;    
  }
  else if(this->shadeType == shade_types::drycontact2) {
    if(this->proto == radio_proto::GP_Relay && (this->gpioUp == pin || this->gpioDown == pin)) return true;
  }
  else {
    if(this->gpioUp == pin) return true;
    else if(this->proto == radio_proto::GP_Remote && this->gpioMy == pin) return true;    
  }
  return false;
}
int8_t SomfyShade::validateJSON(JsonObject &obj) {
  int8_t ret = 0;
  shade_types type = this->shadeType;
  if(obj.containsKey("shadeType")) {
    if(obj["shadeType"].is<const char *>()) {
      if(strncmp(obj["shadeType"].as<const char *>(), "roller", 7) == 0)
        type = shade_types::roller;
      else if(strncmp(obj["shadeType"].as<const char *>(), "ldrapery", 9) == 0)
        type = shade_types::ldrapery;
      else if(strncmp(obj["shadeType"].as<const char *>(), "rdrapery", 9) == 0)
        type = shade_types::rdrapery;
      else if(strncmp(obj["shadeType"].as<const char *>(), "cdrapery", 9) == 0)
        type = shade_types::cdrapery;
      else if(strncmp(obj["shadeType"].as<const char *>(), "garage1", 7) == 0)
        type = shade_types::garage1;
      else if(strncmp(obj["shadeType"].as<const char *>(), "garage3", 7) == 0)
        type = shade_types::garage3;
      else if(strncmp(obj["shadeType"].as<const char *>(), "blind", 5) == 0)
        type = shade_types::blind;
      else if(strncmp(obj["shadeType"].as<const char *>(), "awning", 7) == 0)
        type = shade_types::awning;
      else if(strncmp(obj["shadeType"].as<const char *>(), "shutter", 8) == 0)
        type = shade_types::shutter;
      else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact2", 12) == 0)
        type = shade_types::drycontact2;
      else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact", 11) == 0)
        type = shade_types::drycontact;
    }
    else {
      this->shadeType = static_cast<shade_types>(obj["shadeType"].as<uint8_t>());
    }
  }
  if(obj.containsKey("proto")) {
    radio_proto proto = this->proto;
    if(proto == radio_proto::GP_Relay || proto == radio_proto::GP_Remote) {
      // Check to see if we are using the up and or down
      // GPIOs anywhere else.
      uint8_t upPin = obj.containsKey("gpioUp") ? obj["gpioUp"].as<uint8_t>() : this->gpioUp;
      uint8_t downPin = obj.containsKey("gpioDown") ? obj["gpioDown"].as<uint8_t>() : this->gpioDown;
      uint8_t myPin = obj.containsKey("gpioMy") ? obj["gpioMy"].as<uint8_t>() : this->gpioMy;
      if(type == shade_types::drycontact || 
        ((type == shade_types::garage1 || type == shade_types::lgate1 || type == shade_types::cgate1 || type == shade_types::rgate1) 
        && proto == radio_proto::GP_Remote)) upPin = myPin = 255;
      else if(type == shade_types::drycontact2) myPin = 255;
      if(proto == radio_proto::GP_Relay) myPin = 255;
      if(somfy.transceiver.config.enabled) {
        if((upPin != 255 && somfy.transceiver.usesPin(upPin)) ||
          (downPin != 255 && somfy.transceiver.usesPin(downPin)) ||
          (myPin != 255 && somfy.transceiver.usesPin(myPin)))
          ret = -10;
      }
      if(settings.connType == conn_types_t::ethernet || settings.connType == conn_types_t::ethernetpref) {
        if((upPin != 255 && settings.Ethernet.usesPin(upPin)) ||
          (downPin != 255 && somfy.transceiver.usesPin(downPin)) ||
          (myPin != 255 && somfy.transceiver.usesPin(myPin)))
          ret = -11;
      }
      if(ret == 0) {
        for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
          SomfyShade *shade = &somfy.shades[i];
          if(shade->getShadeId() == this->getShadeId() || shade->getShadeId() == 255) continue;
          if((upPin != 255 && shade->usesPin(upPin)) ||
            (downPin != 255 && shade->usesPin(downPin)) ||
            (myPin != 255 && shade->usesPin(myPin))){
            ret = -12;
            break;
          }
        }
      }
    }
  }
  return ret;
}
int8_t SomfyShade::fromJSON(JsonObject &obj) {
  int8_t err = this->validateJSON(obj);
  if(err == 0) {
    if(obj.containsKey("name")) strlcpy(this->name, obj["name"], sizeof(this->name));
    if(obj.containsKey("roomId")) this->roomId = obj["roomId"];
    if(obj.containsKey("upTime")) this->upTime = obj["upTime"];
    if(obj.containsKey("downTime")) this->downTime = obj["downTime"];
    if(obj.containsKey("remoteAddress")) this->setRemoteAddress(obj["remoteAddress"]);
    if(obj.containsKey("tiltTime")) this->tiltTime = obj["tiltTime"];
    if(obj.containsKey("stepSize")) this->stepSize = obj["stepSize"];
    if(obj.containsKey("hasTilt")) this->tiltType = static_cast<bool>(obj["hasTilt"]) ? tilt_types::none : tilt_types::tiltmotor;
    if(obj.containsKey("bitLength")) this->bitLength = obj["bitLength"];
    if(obj.containsKey("proto")) this->proto = static_cast<radio_proto>(obj["proto"].as<uint8_t>());
    if(obj.containsKey("sunSensor")) this->setSunSensor(obj["sunSensor"]);
    if(obj.containsKey("simMy")) this->setSimMy(obj["simMy"]);
    if(obj.containsKey("light")) this->setLight(obj["light"]);
    if(obj.containsKey("gpioFlags")) this->gpioFlags = obj["gpioFlags"];
    if(obj.containsKey("gpioLLTrigger")) {
      if(obj["gpioLLTrigger"].as<bool>())
        this->gpioFlags |= (uint8_t)gpio_flags_t::LowLevelTrigger;
      else
        this->gpioFlags &= ~(uint8_t)gpio_flags_t::LowLevelTrigger;
    }
    
    if(obj.containsKey("shadeType")) {
      if(obj["shadeType"].is<const char *>()) {
        if(strncmp(obj["shadeType"].as<const char *>(), "roller", 7) == 0)
          this->shadeType = shade_types::roller;
        else if(strncmp(obj["shadeType"].as<const char *>(), "ldrapery", 9) == 0)
          this->shadeType = shade_types::ldrapery;
        else if(strncmp(obj["shadeType"].as<const char *>(), "rdrapery", 9) == 0)
          this->shadeType = shade_types::rdrapery;
        else if(strncmp(obj["shadeType"].as<const char *>(), "cdrapery", 9) == 0)
          this->shadeType = shade_types::cdrapery;
        else if(strncmp(obj["shadeType"].as<const char *>(), "garage1", 7) == 0)
          this->shadeType = shade_types::garage1;
        else if(strncmp(obj["shadeType"].as<const char *>(), "garage3", 7) == 0)
          this->shadeType = shade_types::garage3;
        else if(strncmp(obj["shadeType"].as<const char *>(), "blind", 5) == 0)
          this->shadeType = shade_types::blind;
        else if(strncmp(obj["shadeType"].as<const char *>(), "awning", 7) == 0)
          this->shadeType = shade_types::awning;
        else if(strncmp(obj["shadeType"].as<const char *>(), "shutter", 8) == 0)
          this->shadeType = shade_types::shutter;
        else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact2", 12) == 0)
          this->shadeType = shade_types::drycontact2;
        else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact", 11) == 0)
          this->shadeType = shade_types::drycontact;
      }
      else {
        this->shadeType = static_cast<shade_types>(obj["shadeType"].as<uint8_t>());
      }
    }
    if(obj.containsKey("flipCommands")) this->flipCommands = obj["flipCommands"].as<bool>();
    if(obj.containsKey("flipPosition")) this->flipPosition = obj["flipPosition"].as<bool>();
    if(obj.containsKey("repeats")) this->repeats = obj["repeats"];
    if(obj.containsKey("tiltType")) {
      if(obj["tiltType"].is<const char *>()) {
        if(strncmp(obj["tiltType"].as<const char *>(), "none", 4) == 0)
          this->tiltType = tilt_types::none;
        else if(strncmp(obj["tiltType"].as<const char *>(), "tiltmotor", 9) == 0)
          this->tiltType = tilt_types::tiltmotor;
        else if(strncmp(obj["tiltType"].as<const char *>(), "integ", 5) == 0)
          this->tiltType = tilt_types::integrated;
        else if(strncmp(obj["tiltType"].as<const char *>(), "tiltonly", 8) == 0)
          this->tiltType = tilt_types::tiltonly;
      }
      else {
        this->tiltType = static_cast<tilt_types>(obj["tiltType"].as<uint8_t>());
      }
    }
    if(obj.containsKey("linkedAddresses")) {
      uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
      memset(linkedAddresses, 0x00, sizeof(linkedAddresses));
      JsonArray arr = obj["linkedAddresses"];
      uint8_t i = 0;
      for(uint32_t addr : arr) {
        linkedAddresses[i++] = addr;
      }
      for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
        this->linkedRemotes[j].setRemoteAddress(linkedAddresses[j]);
      }
    }
    if(obj.containsKey("flags")) this->flags = obj["flags"];
    if(this->proto == radio_proto::GP_Remote || this->proto == radio_proto::GP_Relay) {
      if(obj.containsKey("gpioUp")) this->gpioUp = obj["gpioUp"];
      if(obj.containsKey("gpioDown")) this->gpioDown = obj["gpioDown"];
      pinMode(this->gpioUp, OUTPUT);
      pinMode(this->gpioDown, OUTPUT);
    }
    if(this->proto == radio_proto::GP_Remote) {
      if(obj.containsKey("gpioMy")) this->gpioMy = obj["gpioMy"];
      pinMode(this->gpioMy, OUTPUT);
    }
  }
  return err;
}
void SomfyShade::toJSONRef(JsonResponse &json) {
  json.addElem("shadeId", this->getShadeId());
  json.addElem("roomId", this->roomId);
  json.addElem("name", this->name);
  json.addElem("remoteAddress", (uint32_t)this->m_remoteAddress);
  json.addElem("paired", this->paired);
  json.addElem("shadeType", static_cast<uint8_t>(this->shadeType));
  json.addElem("flipCommands", this->flipCommands);
  json.addElem("flipPosition", this->flipCommands);
  json.addElem("bitLength", this->bitLength);
  json.addElem("proto", static_cast<uint8_t>(this->proto));
  json.addElem("flags", this->flags);
  json.addElem("sunSensor", this->hasSunSensor());
  json.addElem("hasLight", this->hasLight());
  json.addElem("repeats", this->repeats);
  //SomfyRemote::toJSON(json);
}

void SomfyShade::toJSON(JsonResponse &json) {
  json.addElem("shadeId", this->getShadeId());
  json.addElem("roomId", this->roomId);
  json.addElem("name", this->name);
  json.addElem("remoteAddress", (uint32_t)this->m_remoteAddress);
  json.addElem("upTime", (uint32_t)this->upTime);
  json.addElem("downTime", (uint32_t)this->downTime);
  json.addElem("paired", this->paired);
  json.addElem("lastRollingCode", (uint32_t)this->lastRollingCode);
  json.addElem("position", this->transformPosition(this->currentPos));
  json.addElem("tiltType", static_cast<uint8_t>(this->tiltType));
  json.addElem("tiltPosition", this->transformPosition(this->currentTiltPos));
  json.addElem("tiltDirection", this->tiltDirection);
  json.addElem("tiltTime", (uint32_t)this->tiltTime);
  json.addElem("stepSize", (uint32_t)this->stepSize);
  json.addElem("tiltTarget", this->transformPosition(this->tiltTarget));
  json.addElem("target", this->transformPosition(this->target));
  json.addElem("myPos", this->transformPosition(this->myPos));
  json.addElem("myTiltPos", this->transformPosition(this->myTiltPos));
  json.addElem("direction", this->direction);
  json.addElem("shadeType", static_cast<uint8_t>(this->shadeType));
  json.addElem("bitLength", this->bitLength);
  json.addElem("proto", static_cast<uint8_t>(this->proto));
  json.addElem("flags", this->flags);
  json.addElem("flipCommands", this->flipCommands);
  json.addElem("flipPosition", this->flipPosition);
  json.addElem("inGroup", this->isInGroup());
  json.addElem("sunSensor", this->hasSunSensor());
  json.addElem("light", this->hasLight());
  json.addElem("repeats", this->repeats);
  json.addElem("sortOrder", this->sortOrder);  
  json.addElem("gpioUp", this->gpioUp);
  json.addElem("gpioDown", this->gpioDown);
  json.addElem("gpioMy", this->gpioMy);
  json.addElem("gpioLLTrigger", ((this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0) ? false : true);
  json.addElem("simMy", this->simMy());
  json.beginArray("linkedRemotes");
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    SomfyLinkedRemote &lremote = this->linkedRemotes[i];
    if(lremote.getRemoteAddress() != 0) {
      json.beginObject();
      lremote.toJSON(json);
      json.endObject();
    }
  }
  json.endArray();
}

/*
bool SomfyShade::toJSON(JsonObject &obj) {
  //Serial.print("Serializing Shade:");
  //Serial.print(this->getShadeId());
  //Serial.print("  ");
  //Serial.println(this->name);
  obj["shadeId"] = this->getShadeId();
  obj["roomId"] = this->roomId;
  obj["name"] = this->name;
  obj["remoteAddress"] = this->m_remoteAddress;
  obj["upTime"] = this->upTime;
  obj["downTime"] = this->downTime;
  obj["paired"] = this->paired;
  //obj["remotePrefId"] = this->getRemotePrefId();
  obj["lastRollingCode"] = this->lastRollingCode;
  obj["position"] = this->transformPosition(this->currentPos);
  obj["tiltPosition"] = this->transformPosition(this->currentTiltPos);
  obj["tiltDirection"] = this->tiltDirection;
  obj["tiltTime"] = this->tiltTime;
  obj["stepSize"] = this->stepSize;
  obj["tiltTarget"] = this->transformPosition(this->tiltTarget);
  obj["target"] = this->transformPosition(this->target);
  obj["myPos"] = this->transformPosition(this->myPos);
  obj["myTiltPos"] = this->transformPosition(this->myTiltPos);
  obj["direction"] = this->direction;
  obj["tiltType"] = static_cast<uint8_t>(this->tiltType);
  obj["tiltTime"] = this->tiltTime;
  obj["shadeType"] = static_cast<uint8_t>(this->shadeType);
  obj["bitLength"] = this->bitLength;
  obj["proto"] = static_cast<uint8_t>(this->proto);
  obj["flags"] = this->flags;
  obj["flipCommands"] = this->flipCommands;
  obj["flipPosition"] = this->flipPosition;
  obj["inGroup"] = this->isInGroup();
  obj["sunSensor"] = this->hasSunSensor();
  obj["light"] = this->hasLight();
  obj["repeats"] = this->repeats;
  obj["sortOrder"] = this->sortOrder;  
  obj["gpioUp"] = this->gpioUp;
  obj["gpioDown"] = this->gpioDown;
  obj["gpioMy"] = this->gpioMy;
  obj["gpioLLTrigger"] = ((this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0) ? false : true;
  SomfyRemote::toJSON(obj);
  JsonArray arr = obj.createNestedArray("linkedRemotes");
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    SomfyLinkedRemote &lremote = this->linkedRemotes[i];
    if(lremote.getRemoteAddress() != 0) {
      JsonObject lro = arr.createNestedObject();
      lremote.toJSON(lro);
    }
  }
  return true;
}
*/
bool SomfyRoom::fromJSON(JsonObject &obj) {
  if(obj.containsKey("name")) strlcpy(this->name, obj["name"], sizeof(this->name));
  if(obj.containsKey("sortOrder")) this->sortOrder = obj["sortOrder"];
  return true;
}
/*
bool SomfyRoom::toJSON(JsonObject &obj) {
  obj["roomId"] = this->roomId;
  obj["name"] = this->name;
  obj["sortOrder"] = this->sortOrder;
  return true;
}
*/
void SomfyRoom::toJSON(JsonResponse &json) {
  json.addElem("roomId", this->roomId);
  json.addElem("name", this->name);
  json.addElem("sortOrder", this->sortOrder);
}

bool SomfyGroup::fromJSON(JsonObject &obj) {
  if(obj.containsKey("name")) strlcpy(this->name, obj["name"], sizeof(this->name));
  if(obj.containsKey("roomId")) this->roomId = obj["roomId"];
  if(obj.containsKey("remoteAddress")) this->setRemoteAddress(obj["remoteAddress"]);
  if(obj.containsKey("bitLength")) this->bitLength = obj["bitLength"];
  if(obj.containsKey("proto")) this->proto = static_cast<radio_proto>(obj["proto"].as<uint8_t>());
  if(obj.containsKey("flipCommands")) this->flipCommands = obj["flipCommands"].as<bool>();
  
  //if(obj.containsKey("sunSensor")) this->hasSunSensor() = obj["sunSensor"];  This is calculated
  if(obj.containsKey("repeats")) this->repeats = obj["repeats"];
  if(obj.containsKey("linkedShades")) {
    uint8_t linkedShades[SOMFY_MAX_GROUPED_SHADES];
    memset(linkedShades, 0x00, sizeof(linkedShades));
    JsonArray arr = obj["linkedShades"];
    uint8_t i = 0;
    for(uint8_t shadeId : arr) {
      linkedShades[i++] = shadeId;
    }
  }
  return true;
}
void SomfyGroup::toJSON(JsonResponse &json) {
  this->updateFlags();
  json.addElem("groupId", this->getGroupId());
  json.addElem("roomId", this->roomId);
  json.addElem("name", this->name);
  json.addElem("remoteAddress", (uint32_t)this->m_remoteAddress);
  json.addElem("lastRollingCode", (uint32_t)this->lastRollingCode);
  json.addElem("bitLength", this->bitLength);
  json.addElem("proto", static_cast<uint8_t>(this->proto));
  json.addElem("sunSensor", this->hasSunSensor());
  json.addElem("flipCommands", this->flipCommands);
  json.addElem("flags", this->flags);
  json.addElem("repeats", this->repeats);
  json.addElem("sortOrder", this->sortOrder);
  json.beginArray("linkedShades");
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    uint8_t shadeId = this->linkedShades[i];
    if(shadeId > 0 && shadeId < 255) {
      SomfyShade *shade = somfy.getShadeById(shadeId);
      if(shade) {
        json.beginObject();
        shade->toJSONRef(json);
        json.endObject();
      }
    }
  }
  json.endArray();
}
void SomfyGroup::toJSONRef(JsonResponse &json) {
  this->updateFlags();
  json.addElem("groupId", this->getGroupId());
  json.addElem("roomId", this->roomId);
  json.addElem("name", this->name);
  json.addElem("remoteAddress", (uint32_t)this->m_remoteAddress);
  json.addElem("lastRollingCode", (uint32_t)this->lastRollingCode);
  json.addElem("bitLength", this->bitLength);
  json.addElem("proto", static_cast<uint8_t>(this->proto));
  json.addElem("sunSensor", this->hasSunSensor());
  json.addElem("flipCommands", this->flipCommands);
  json.addElem("flags", this->flags);
  json.addElem("repeats", this->repeats);
  json.addElem("sortOrder", this->sortOrder);
}

/*
bool SomfyGroup::toJSON(JsonObject &obj) {
  this->updateFlags();
  obj["groupId"] = this->getGroupId();
  obj["roomId"] = this->roomId;
  obj["name"] = this->name;
  obj["remoteAddress"] = this->m_remoteAddress;
  obj["lastRollingCode"] = this->lastRollingCode;
  obj["bitLength"] = this->bitLength;
  obj["proto"] = static_cast<uint8_t>(this->proto);
  obj["sunSensor"] = this->hasSunSensor();
  obj["flipCommands"] = this->flipCommands;
  obj["flags"] = this->flags;
  obj["repeats"] = this->repeats;
  obj["sortOrder"] = this->sortOrder;
  SomfyRemote::toJSON(obj);
  JsonArray arr = obj.createNestedArray("linkedShades");
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    uint8_t shadeId = this->linkedShades[i];
    if(shadeId > 0 && shadeId < 255) {
      SomfyShade *shade = somfy.getShadeById(shadeId);
      if(shade) {
        JsonObject lsd = arr.createNestedObject();
        shade->toJSONRef(lsd);
      }
    }
  }
  return true;
}
*/

void SomfyRemote::toJSON(JsonResponse &json) {
  json.addElem("remoteAddress", (uint32_t)this->getRemoteAddress());
  json.addElem("lastRollingCode", (uint32_t)this->lastRollingCode);
}
/*
bool SomfyRemote::toJSON(JsonObject &obj) {
  //obj["remotePrefId"] = this->getRemotePrefId();
  obj["remoteAddress"] = this->getRemoteAddress();
  obj["lastRollingCode"] = this->lastRollingCode;
  return true;  
}
*/
void SomfyRemote::setRemoteAddress(uint32_t address) { this->m_remoteAddress = address; snprintf(this->m_remotePrefId, sizeof(this->m_remotePrefId), "_%lu", (unsigned long)this->m_remoteAddress); }
uint32_t SomfyRemote::getRemoteAddress() { return this->m_remoteAddress; }
void SomfyShadeController::processFrame(somfy_frame_t &frame, bool internal) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() != 255) this->shades[i].processFrame(frame, internal);
  }
}
void SomfyShadeController::processWaitingFrame() {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(this->shades[i].getShadeId() != 255) this->shades[i].processWaitingFrame();
}
void SomfyShadeController::emitState(uint8_t num) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() == 255) continue;
    shade->emitState(num);
  }
}
void SomfyShadeController::publish() {
  this->updateGroupFlags();
  char arrIds[128] = "[";
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() == 255) continue;
    if(strlen(arrIds) > 1) strcat(arrIds, ",");
    itoa(shade->getShadeId(), &arrIds[strlen(arrIds)], 10);
    shade->publish();
  }
  strcat(arrIds, "]");
  mqtt.publish("shades", arrIds, true);
  for(uint8_t i = 1; i <= SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = this->getShadeById(i);
    if(shade) continue;
    else {
      SomfyShade::unpublish(i);
    }
  }
  strcpy(arrIds, "[");
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &this->groups[i];
    if(group->getGroupId() == 255) continue;
    if(strlen(arrIds) > 1) strcat(arrIds, ",");
    itoa(group->getGroupId(), &arrIds[strlen(arrIds)], 10);
    group->publish();
  }
  strcat(arrIds, "]");
  mqtt.publish("groups", arrIds, true);
  for(uint8_t i = 1; i <= SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = this->getGroupById(i);
    if(group) continue;
    else SomfyGroup::unpublish(i);
  }
}
uint8_t SomfyShadeController::getNextShadeId() {
  // There is no shortcut for this since the deletion of
  // a shade in the middle makes all of this very difficult.
  for(uint8_t i = 1; i < SOMFY_MAX_SHADES - 1; i++) {
    bool id_exists = false;
    for(uint8_t j = 0; j < SOMFY_MAX_SHADES; j++) {
      SomfyShade *shade = &this->shades[j];
      if(shade->getShadeId() == i) {
        id_exists = true;
        break;
      }
    }
    if(!id_exists) {
      Serial.print("Got next Shade Id:");
      Serial.print(i);
      return i;
    }
  }
  return 255;
}
int8_t SomfyShadeController::getMaxShadeOrder() {
  int8_t order = -1;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() == 255) continue;
    if(order < shade->sortOrder) order = shade->sortOrder;
  }
  return order;
}
int8_t SomfyShadeController::getMaxGroupOrder() {
  int8_t order = -1;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &this->groups[i];
    if(group->getGroupId() == 255) continue;
    if(order < group->sortOrder) order = group->sortOrder;
  }
  return order;
}
uint8_t SomfyShadeController::getNextGroupId() {
  // There is no shortcut for this since the deletion of
  // a group in the middle makes all of this very difficult.
  for(uint8_t i = 1; i < SOMFY_MAX_GROUPS - 1; i++) {
    bool id_exists = false;
    for(uint8_t j = 0; j < SOMFY_MAX_GROUPS; j++) {
      SomfyGroup *group = &this->groups[j];
      if(group->getGroupId() == i) {
        id_exists = true;
        break;
      }
    }
    if(!id_exists) {
      Serial.print("Got next Group Id:");
      Serial.print(i);
      return i;
    }
  }
  return 255;
}
uint8_t SomfyShadeController::getNextRoomId() {
  // There is no shortcut for this since the deletion of
  // a room in the middle makes all of this very difficult.
  for(uint8_t i = 1; i < SOMFY_MAX_ROOMS - 1; i++) {
    bool id_exists = false;
    for(uint8_t j = 0; j < SOMFY_MAX_ROOMS; j++) {
      SomfyRoom *room = &this->rooms[j];
      if(room->roomId == i) {
        id_exists = true;
        break;
      }
    }
    if(!id_exists) {
      Serial.print("Got next room Id:");
      Serial.print(i);
      return i;
    }
  }
  return 0;
}
int8_t SomfyShadeController::getMaxRoomOrder() {
  int8_t order = -1;
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    SomfyRoom *room = &this->rooms[i];
    if(room->roomId == 0) continue;
    if(order < room->sortOrder) order = room->sortOrder;
  }
  return order;
}
uint8_t SomfyShadeController::repeaterCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(this->repeaters[i] != 0) count++;
  }
  return count;
}
uint8_t SomfyShadeController::roomCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    if(this->rooms[i].roomId != 0) count++;
  }
  return count;
}
uint8_t SomfyShadeController::shadeCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() != 255) count++;
  }
  return count;
}
uint8_t SomfyShadeController::groupCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() != 255) count++;
  }
  return count;
}
uint32_t SomfyShadeController::getNextRemoteAddress(uint8_t id) {
  uint32_t address = this->startingAddress + id;
  uint8_t i = 0;
  // The assumption here is that the max number of groups will
  // always be less than or equal to the max number of shades.
  while(i < SOMFY_MAX_SHADES) {
    if((i < SOMFY_MAX_SHADES && this->shades[i].getShadeId() != 255 && this->shades[i].getRemoteAddress() == address) ||
      (i < SOMFY_MAX_GROUPS && this->groups[i].getGroupId() != 255 && this->groups[i].getRemoteAddress() == address)) {
      address++;
      i = 0; // Start over we cannot share addresses.
    }
    else i++;
  }
  i = 0;
  return address;
}
SomfyShade *SomfyShadeController::addShade(JsonObject &obj) {
  SomfyShade *shade = this->addShade();
  if(shade) {
    shade->fromJSON(obj);
    shade->save();
    shade->emitState("shadeAdded");
  }
  return shade;
}
SomfyShade *SomfyShadeController::addShade() {
  uint8_t shadeId = this->getNextShadeId();
  // So the next shade id will be the first one we run into with an id of 255 so
  // if it gets deleted in the middle then it will get the first slot that is empty.
  // There is no apparent way around this.  In the future we might actually add an indexer
  // to it for sorting later.  The time has come so the sort order is set below.
  if(shadeId == 255) return nullptr;
  SomfyShade *shade = &this->shades[shadeId - 1];
  if(shade) {
    shade->setShadeId(shadeId);
    shade->sortOrder = this->getMaxShadeOrder() + 1;
    Serial.printf("Sort order set to %d\n", shade->sortOrder);
    this->isDirty = true;
    #ifdef USE_NVS
    if(this->useNVS()) {
      for(uint8_t i = 0; i < sizeof(this->m_shadeIds); i++) {
        this->m_shadeIds[i] = this->shades[i].getShadeId();
      }
      sortArray<uint8_t>(this->m_shadeIds, sizeof(this->m_shadeIds));
      uint8_t id = 0;
      // This little diddy is about a bug I had previously that left duplicates in the
      // sorted array.  So we will walk the sorted array until we hit a duplicate where the previous
      // value == the current value.  Set it to 255 then sort the array again.
      // 1,1,2,2,3,3,255...
      bool hadDups = false;
      for(uint8_t i = 0; i < sizeof(this->m_shadeIds); i++) {
        if(this->m_shadeIds[i] == 255) break;
        if(id == this->m_shadeIds[i]) {
          id = this->m_shadeIds[i];
          this->m_shadeIds[i] = 255;
          hadDups = true;
        }
        else {
          id = this->m_shadeIds[i];
        }
      }
      if(hadDups) sortArray<uint8_t>(this->m_shadeIds, sizeof(this->m_shadeIds));
      pref.begin("Shades");
      pref.remove("shadeIds");
      int x = pref.putBytes("shadeIds", this->m_shadeIds, sizeof(this->m_shadeIds));
      Serial.printf("WROTE %d bytes to shadeIds\n", x);
      pref.end();
      for(uint8_t i = 0; i < sizeof(this->m_shadeIds); i++) {
        if(i != 0) Serial.print(",");
        else Serial.print("Shade Ids: ");
        Serial.print(this->m_shadeIds[i]);
      }
      Serial.println();
      pref.begin("Shades");
      pref.getBytes("shadeIds", this->m_shadeIds, sizeof(this->m_shadeIds));
      Serial.print("LENGTH:");
      Serial.println(pref.getBytesLength("shadeIds"));
      pref.end();
      for(uint8_t i = 0; i < sizeof(this->m_shadeIds); i++) {
        if(i != 0) Serial.print(",");
        else Serial.print("Shade Ids: ");
        Serial.print(this->m_shadeIds[i]);
      }
      Serial.println();
    }
    #endif
  }
  return shade;
}
bool SomfyShadeController::unlinkRepeater(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(this->repeaters[i] == address) this->repeaters[i] = 0;
  }
  this->compressRepeaters();
  this->isDirty = true;
  return true;  
}
bool SomfyShadeController::linkRepeater(uint32_t address) {
  bool bSet = false;
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(!bSet && this->repeaters[i] == address) bSet = true;
    else if(bSet && this->repeaters[i] == address) this->repeaters[i] = 0;
  }
  if(!bSet) {
    for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
      if(this->repeaters[i] == 0) {
        this->repeaters[i] = address;
        return true;
      }
    }
  }
  return true;
}
SomfyRoom *SomfyShadeController::addRoom(JsonObject &obj) {
  SomfyRoom *room = this->addRoom();
  if(room) {
    room->fromJSON(obj);
    room->save();
    room->emitState("roomAdded");
  }
  return room;
}
SomfyRoom *SomfyShadeController::addRoom() {
  uint8_t roomId = this->getNextRoomId();
  // So the next room id will be the first one we run into with an id of 0 so
  if(roomId == 0) return nullptr;
  SomfyRoom *room = &this->rooms[roomId - 1];
  if(room) {
    room->roomId = roomId;
    room->sortOrder = this->getMaxRoomOrder() + 1;
    this->isDirty = true;
  }
  return room;
}

SomfyGroup *SomfyShadeController::addGroup(JsonObject &obj) {
  SomfyGroup *group = this->addGroup();
  if(group) {
    group->fromJSON(obj);
    group->save();
    group->emitState("groupAdded");
  }
  return group;
}
SomfyGroup *SomfyShadeController::addGroup() {
  uint8_t groupId = this->getNextGroupId();
  // So the next shade id will be the first one we run into with an id of 255 so
  // if it gets deleted in the middle then it will get the first slot that is empty.
  // There is no apparent way around this.  In the future we might actually add an indexer
  // to it for sorting later.
  if(groupId == 255) return nullptr;
  SomfyGroup *group = &this->groups[groupId - 1];
  if(group) {
    group->setGroupId(groupId);
    group->sortOrder = this->getMaxGroupOrder() + 1;
    this->isDirty = true;
  }
  return group;
}
somfy_commands SomfyRemote::transformCommand(somfy_commands cmd) {
  if(this->flipCommands) {
    switch(cmd) {
      case somfy_commands::Up:
        return somfy_commands::Down;
      case somfy_commands::MyUp:
        return somfy_commands::MyDown;
      case somfy_commands::Down:
        return somfy_commands::Up;
      case somfy_commands::MyDown:
        return somfy_commands::MyUp;
      case somfy_commands::StepUp:
        return somfy_commands::StepDown;
      case somfy_commands::StepDown:
        return somfy_commands::StepUp;
      default:
        break;
    }
  }
  return cmd;
}
void SomfyRemote::sendSensorCommand(int8_t isWindy, int8_t isSunny, uint8_t repeat) {
  uint8_t flags = (this->flags >> 4) & 0x0F;
  if(isWindy > 0) flags |= 0x01;
  if(isSunny > 0) flags |= 0x02;
  if(isWindy == 0) flags &= ~0x01;
  if(isSunny == 0) flags &= ~0x02;

  // Now ship this off as an 80 bit command.
  this->lastFrame.remoteAddress = this->getRemoteAddress();
  this->lastFrame.repeats = repeat;
  this->lastFrame.bitLength = this->bitLength;
  this->lastFrame.rollingCode = (uint16_t)flags;
  this->lastFrame.encKey = 160; // Sensor commands are always encryption code 160.
  this->lastFrame.cmd = somfy_commands::Sensor;
  this->lastFrame.processed = false;
  Serial.print("CMD:");
  Serial.print(translateSomfyCommand(this->lastFrame.cmd));
  Serial.print(" ADDR:");
  Serial.print(this->lastFrame.remoteAddress);
  Serial.print(" RCODE:");
  Serial.print(this->lastFrame.rollingCode);
  Serial.print(" REPEAT:");
  Serial.println(repeat);
  somfy.sendFrame(this->lastFrame, repeat);
  somfy.processFrame(this->lastFrame, true);
}
void SomfyRemote::sendCommand(somfy_commands cmd) { this->sendCommand(cmd, this->repeats); }
void SomfyRemote::sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize) {
  this->lastFrame.rollingCode = this->getNextRollingCode();
  this->lastFrame.remoteAddress = this->getRemoteAddress();
  this->lastFrame.cmd = this->transformCommand(cmd);
  this->lastFrame.repeats = repeat;
  this->lastFrame.bitLength = this->bitLength;
  this->lastFrame.stepSize = stepSize;
  this->lastFrame.valid = true;
  // Match the encKey to the rolling code.  These keys range from 160 to 175.
  this->lastFrame.encKey = 0xA0 | static_cast<uint8_t>(this->lastFrame.rollingCode & 0x000F);
  this->lastFrame.proto = this->proto;
  if(this->lastFrame.bitLength == 0) this->lastFrame.bitLength = bit_length;
  if(this->lastFrame.rollingCode == 0) Serial.println("ERROR: Setting rcode to 0");
  this->p_lastRollingCode(this->lastFrame.rollingCode);
  // We have to set the processed to clear this if we are sending
  // another command.
  this->lastFrame.processed = false;
  if(this->proto == radio_proto::GP_Relay) {
    Serial.print("CMD:");
    Serial.print(translateSomfyCommand(this->lastFrame.cmd));
    Serial.print(" ADDR:");
    Serial.print(this->lastFrame.remoteAddress);
    Serial.print(" RCODE:");
    Serial.print(this->lastFrame.rollingCode);
    Serial.println(" SETTING GPIO");
  }
  else if(this->proto == radio_proto::GP_Remote) {
    Serial.print("CMD:");
    Serial.print(translateSomfyCommand(this->lastFrame.cmd));
    Serial.print(" ADDR:");
    Serial.print(this->lastFrame.remoteAddress);
    Serial.print(" RCODE:");
    Serial.print(this->lastFrame.rollingCode);
    Serial.println(" TRIGGER GPIO");
    this->triggerGPIOs(this->lastFrame);
  }
  else {
    Serial.print("CMD:");
    Serial.print(translateSomfyCommand(this->lastFrame.cmd));
    Serial.print(" ADDR:");
    Serial.print(this->lastFrame.remoteAddress);
    Serial.print(" RCODE:");
    Serial.print(this->lastFrame.rollingCode);
    Serial.print(" REPEAT:");
    Serial.println(repeat);
    somfy.sendFrame(this->lastFrame, repeat);
  }
  somfy.processFrame(this->lastFrame, true);
}
bool SomfyRemote::isLastCommand(somfy_commands cmd) {
  if(this->lastFrame.cmd != cmd || this->lastFrame.rollingCode != this->lastRollingCode) {
    Serial.printf("Not the last command %d: %d - %d\n", static_cast<uint8_t>(this->lastFrame.cmd), this->lastFrame.rollingCode, this->lastRollingCode);
    return false;
  }
  return true;
}
void SomfyRemote::repeatFrame(uint8_t repeat) {
  if(this->proto == radio_proto::GP_Relay)
    return;
  else if(this->proto == radio_proto::GP_Remote) {
    this->triggerGPIOs(this->lastFrame);
    return;
  }
  somfy.transceiver.beginTransmit();
  byte frm[10];
  this->lastFrame.encodeFrame(frm);
  this->lastFrame.repeats++;
  somfy.transceiver.sendFrame(frm, this->bitLength == 56 ? 2 : 12, this->bitLength);
  for(uint8_t i = 0; i < repeat; i++) {
    this->lastFrame.repeats++;
    if(this->lastFrame.bitLength == 80) this->lastFrame.encode80BitFrame(&frm[0], this->lastFrame.repeats);
    somfy.transceiver.sendFrame(frm, this->bitLength == 56 ? 7 : 6, this->bitLength);
    esp_task_wdt_reset();
  }
  somfy.transceiver.endTransmit();
  //somfy.processFrame(this->lastFrame, true);
}
void SomfyShadeController::sendFrame(somfy_frame_t &frame, uint8_t repeat) {
  somfy.transceiver.beginTransmit();
  byte frm[10];
  frame.encodeFrame(frm);
  this->transceiver.sendFrame(frm, frame.bitLength == 56 ? 2 : 12, frame.bitLength);
  for(uint8_t i = 0; i < repeat; i++) {
    // For each 80-bit frame we need to adjust the byte encoding for the
    // silence.
    if(frame.bitLength == 80) frame.encode80BitFrame(&frm[0], i + 1);
    this->transceiver.sendFrame(frm, frame.bitLength == 56 ? 7 : 6, frame.bitLength);
    esp_task_wdt_reset();
  }
  this->transceiver.endTransmit();
}
bool SomfyShadeController::deleteShade(uint8_t shadeId) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() == shadeId) {
      shades[i].emitState("shadeRemoved");
      shades[i].unpublish();
      this->shades[i].clear();
    }
  }
  #ifdef USE_NVS
  if(this->useNVS()) {
    for(uint8_t i = 0; i < sizeof(this->m_shadeIds) - 1; i++) {
      if(this->m_shadeIds[i] == shadeId) {
        this->m_shadeIds[i] = 255;
      }
    }
    
    //qsort(this->m_shadeIds, sizeof(this->m_shadeIds)/sizeof(this->m_shadeIds[0]), sizeof(this->m_shadeIds[0]), sort_asc);
    sortArray<uint8_t>(this->m_shadeIds, sizeof(this->m_shadeIds));
    
    pref.begin("Shades");
    pref.putBytes("shadeIds", this->m_shadeIds, sizeof(this->m_shadeIds));
    pref.end();
  }
  #endif
  this->commit();
  return true;
}
bool SomfyShadeController::deleteRoom(uint8_t roomId) {
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    if(this->rooms[i].roomId == roomId) {
      rooms[i].unpublish();
      for(uint8_t j = 0; j < SOMFY_MAX_SHADES; j++) {
        if(shades[j].roomId == roomId) {
          shades[j].roomId = 0;
          shades[j].emitState();
        }
      }
      for(uint8_t j = 0; j < SOMFY_MAX_GROUPS; j++) {
        if(groups[j].roomId == roomId) {
          groups[j].roomId = 0;
          groups[j].emitState();
        }
      }
      rooms[i].emitState("roomRemoved");
      this->rooms[i].clear();
    }
  }
  this->commit();
  return true;
}

bool SomfyShadeController::deleteGroup(uint8_t groupId) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() == groupId) {
      groups[i].emitState("groupRemoved");
      groups[i].unpublish();
      this->groups[i].clear();
    }
  }
  this->commit();
  return true;
}

bool SomfyShadeController::loadShadesFile(const char *filename) { return ShadeConfigFile::load(this, filename); }
uint16_t SomfyRemote::getNextRollingCode() {
  pref.begin("ShadeCodes");
  uint16_t code = pref.getUShort(this->m_remotePrefId, 0);
  code++;
  pref.putUShort(this->m_remotePrefId, code);
  pref.end();
  this->p_lastRollingCode(code);
  //Serial.printf("Getting Next Rolling code %d\n", this->lastRollingCode);
  return code;
}
uint16_t SomfyRemote::p_lastRollingCode(uint16_t code) { 
  uint16_t old = this->lastRollingCode;
  this->lastRollingCode = code; 
  return old;
}
uint16_t SomfyRemote::setRollingCode(uint16_t code) {
  if(this->lastRollingCode != code) {
    pref.begin("ShadeCodes");
    pref.putUShort(this->m_remotePrefId, code);
    pref.end();  
    this->lastRollingCode = code;
    Serial.printf("Setting Last Rolling code %d\n", this->lastRollingCode);
  }
  return code;
}
void SomfyShadeController::toJSONRooms(JsonResponse &json) {
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    SomfyRoom *room = &this->rooms[i];
    if(room->roomId != 0) {
      json.beginObject();
      room->toJSON(json);
      json.endObject();
    }
  }
}
void SomfyShadeController::toJSONShades(JsonResponse &json) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade &shade = this->shades[i];
    if(shade.getShadeId() != 255) {
      json.beginObject();
      shade.toJSON(json);
      json.endObject();
    }
  }
}

/*
bool SomfyShadeController::toJSON(DynamicJsonDocument &doc) {
  doc["maxRooms"] = SOMFY_MAX_ROOMS;
  doc["maxShades"] = SOMFY_MAX_SHADES;
  doc["maxGroups"] = SOMFY_MAX_GROUPS;
  doc["maxGroupedShades"] = SOMFY_MAX_GROUPED_SHADES;
  doc["maxLinkedRemotes"] = SOMFY_MAX_LINKED_REMOTES;
  doc["startingAddress"] = this->startingAddress;
  JsonObject objRadio = doc.createNestedObject("transceiver");
  this->transceiver.toJSON(objRadio);
  JsonArray arrRooms = doc.createNestedArray("rooms");
  this->toJSONRooms(arrRooms);
  JsonArray arrShades = doc.createNestedArray("shades");
  this->toJSONShades(arrShades);
  JsonArray arrGroups = doc.createNestedArray("groups");
  this->toJSONGroups(arrGroups);
  return true;
}
bool SomfyShadeController::toJSON(JsonObject &obj) {
  obj["maxShades"] = SOMFY_MAX_SHADES;
  obj["maxLinkedRemotes"] = SOMFY_MAX_LINKED_REMOTES;
  obj["startingAddress"] = this->startingAddress;
  JsonObject oradio = obj.createNestedObject("transceiver");
  this->transceiver.toJSON(oradio);
  JsonArray arrShades = obj.createNestedArray("shades");
  this->toJSONShades(arrShades);
  JsonArray arrGroups = obj.createNestedArray("groups");
  this->toJSONGroups(arrGroups);
  return true;
}


bool SomfyShadeController::toJSONShades(JsonArray &arr) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade &shade = this->shades[i];
    if(shade.getShadeId() != 255) {
      JsonObject oshade = arr.createNestedObject();
      shade.toJSON(oshade);
    }
  }
  return true;
}
bool SomfyShadeController::toJSONGroups(JsonArray &arr) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup &group = this->groups[i];
    if(group.getGroupId() != 255) {
      JsonObject ogroup = arr.createNestedObject();
      group.toJSON(ogroup);
    }
  }
  return true;
}
*/
void SomfyShadeController::toJSONGroups(JsonResponse &json) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup &group = this->groups[i];
    if(group.getGroupId() != 255) {
      json.beginObject();
      group.toJSON(json);
      json.endObject();
    }
  }
}
void SomfyShadeController::toJSONRepeaters(JsonResponse &json) {
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(somfy.repeaters[i] != 0) json.addElem((uint32_t)somfy.repeaters[i]);
  }
}
void SomfyShadeController::loop() { 
  this->transceiver.loop(); 
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() != 255) {
      this->shades[i].checkMovement();
      this->shades[i].setGPIOs();
    }
  }
  // Only commit the file once per second.
  if(this->isDirty && millis() - this->lastCommit > 1000) {
    this->commit();
  }
}
SomfyLinkedRemote::SomfyLinkedRemote() {}

// Transceiver Implementation
#define TOLERANCE_MIN 0.7
#define TOLERANCE_MAX 1.3

static const uint32_t tempo_wakeup_pulse = 9415;
static const uint32_t tempo_wakeup_min = 9415 * TOLERANCE_MIN;
static const uint32_t tempo_wakeup_max = 9415 * TOLERANCE_MAX;
static const uint32_t tempo_wakeup_silence = 89565;
static const uint32_t tempo_wakeup_silence_min = 89565 * TOLERANCE_MIN;
static const uint32_t tempo_wakeup_silence_max = 89565 * TOLERANCE_MAX;
static const uint32_t tempo_synchro_hw_min = SYMBOL * 4 * TOLERANCE_MIN;
static const uint32_t tempo_synchro_hw_max = SYMBOL * 4 * TOLERANCE_MAX;
static const uint32_t tempo_synchro_sw_min = 4850 * TOLERANCE_MIN;
static const uint32_t tempo_synchro_sw_max = 4850 * TOLERANCE_MAX;
static const uint32_t tempo_half_symbol_min = SYMBOL * TOLERANCE_MIN;
static const uint32_t tempo_half_symbol_max = SYMBOL * TOLERANCE_MAX;
static const uint32_t tempo_symbol_min = SYMBOL * 2 * TOLERANCE_MIN;
static const uint32_t tempo_symbol_max = SYMBOL * 2 * TOLERANCE_MAX;
static const uint32_t tempo_if_gap = 30415;  // Gap between frames


static int16_t  bitMin = SYMBOL * TOLERANCE_MIN;
static somfy_rx_t somfy_rx;
static somfy_rx_queue_t rx_queue;
static somfy_tx_queue_t tx_queue;
bool somfy_tx_queue_t::pop(somfy_tx_t *tx) {
  // Read the oldest index.
  for(int8_t i = MAX_TX_BUFFER - 1; i >= 0; i--) {
    if(this->index[i] < MAX_TX_BUFFER) {
      uint8_t ndx = this->index[i];
      memcpy(tx, &this->items[ndx], sizeof(somfy_tx_t));
      this->items[ndx].clear();
      if(this->length > 0) this->length--;
      this->index[i] = 255;
      return true;
    }
  }
  return false;
}
void somfy_tx_queue_t::push(somfy_rx_t *rx) { this->push(rx->cpt_synchro_hw, rx->payload, rx->bit_length); }
void somfy_tx_queue_t::push(uint8_t hwsync, uint8_t *payload, uint8_t bit_length) {
  if(this->length >= MAX_TX_BUFFER) {
    // We have overflowed the buffer simply empty the last item
    // in this instance we will simply throw it away.
    uint8_t ndx = this->index[MAX_TX_BUFFER - 1];
    if(ndx < MAX_TX_BUFFER) this->items[ndx].clear();
    this->index[MAX_TX_BUFFER - 1] = 255;
    this->length--;
  }
  uint8_t first = 0;
  // Place this record in the first empty slot.  There will
  // be one since we cleared a space above should there
  // be an overflow.
  for(uint8_t i = 0; i < MAX_TX_BUFFER; i++) {
    if(this->items[i].bit_length == 0) {
      first = i;
      this->items[i].bit_length = bit_length;
      this->items[i].hwsync = hwsync;
      memcpy(&this->items[i].payload, payload, sizeof(this->items[i].payload));
      break;
    }
  }
  // Move the index so that it is the at position 0.  The oldest item will fall off.
  for(uint8_t i = MAX_TX_BUFFER - 1; i > 0; i--) {
    this->index[i] = this->index[i - 1];
  }
  this->length++;
  // When popping from the queue we always pull from the end
  this->index[0] = first;
  this->delay_time = millis() + TX_QUEUE_DELAY; // We do not want to process this frame until a full frame beat has passed.
}
void somfy_rx_queue_t::init() { 
  Serial.println("Initializing RX Queue");
  for (uint8_t i = 0; i < MAX_RX_BUFFER; i++)
    this->items[i].clear();
  memset(&this->index[0], 0xFF, MAX_RX_BUFFER);
  this->length = 0;
}
bool somfy_rx_queue_t::pop(somfy_rx_t *rx) {
  // Read off the data from the oldest index.
  //Serial.println("Popping RX Queue");
  for(int8_t i = MAX_RX_BUFFER - 1; i >= 0; i--) {
    if(this->index[i] < MAX_RX_BUFFER) {
      uint8_t ndx = this->index[i];
      memcpy(rx, &this->items[this->index[i]], sizeof(somfy_rx_t));
      this->items[ndx].clear();
      if(this->length > 0) this->length--;
      this->index[i] = 255;
      return true;      
    }
  }
  return false;
}

void Transceiver::sendFrame(byte *frame, uint8_t sync, uint8_t bitLength) {
  if(!this->config.enabled) return;
  uint32_t pin = 1 << this->config.TXPin;
  if (sync == 2 || sync == 12) {  // Only with the first frame.  Repeats do not get a wakeup pulse.
    // All information online for the wakeup pulse appears to be incorrect.  While there is a wakeup
    // pulse it only sends an initial pulse.  There is no further delay after this.
    
    // Wake-up pulse
    //Serial.printf("Sending wakeup pulse: %d\n", sync);
    REG_WRITE(GPIO_OUT_W1TS_REG, pin);
    delayMicroseconds(10920);
    //delayMicroseconds(9415);
    
    // There is no silence after the wakeup pulse.  I tested this with Telis and no silence
    // was detected.  I suspect that for some battery powered shades the shade would go back
    // to sleep from the time of the initial pulse while the silence was occurring.
    REG_WRITE(GPIO_OUT_W1TC_REG, pin);
    delayMicroseconds(7357);
    //delayMicroseconds(9565);
    //delay(80);
  }
  // Depending on the bitness of the protocol we will be sending a different hwsync.
  // 56-bit 2 pulses for the first frame and 7 for the repeats
  // 80-bit 24 pulses for the first frame and 14 pulses for the repeats
  for (int i = 0; i < sync; i++) {
    REG_WRITE(GPIO_OUT_W1TS_REG, pin);
    delayMicroseconds(4 * SYMBOL);
    REG_WRITE(GPIO_OUT_W1TC_REG, pin);
    delayMicroseconds(4 * SYMBOL);
  }
  // Software sync
  REG_WRITE(GPIO_OUT_W1TS_REG, pin);
  //delayMicroseconds(4450); -- Initial timing.
  delayMicroseconds(4850);
  // Start 0
  REG_WRITE(GPIO_OUT_W1TC_REG, pin);
  delayMicroseconds(SYMBOL);
  // Payload starting with the most significant bit.  The frame is always supplied in 80 bits
  // but if the protocol is calling for 56 bits it will only send 56 bits of the frame.
  uint8_t last_bit = 0;
  for (byte i = 0; i < bitLength; i++) {
    if (((frame[i / 8] >> (7 - (i % 8))) & 1) == 1) {
      REG_WRITE(GPIO_OUT_W1TC_REG, pin);
      delayMicroseconds(SYMBOL);
      REG_WRITE(GPIO_OUT_W1TS_REG, pin);
      delayMicroseconds(SYMBOL);
      last_bit = 1;
    } else {
      REG_WRITE(GPIO_OUT_W1TS_REG, pin);
      delayMicroseconds(SYMBOL);
      REG_WRITE(GPIO_OUT_W1TC_REG, pin);
      delayMicroseconds(SYMBOL);
      last_bit = 0;
    }
  }
  // End with a 0 no matter what.  This accommodates the 56-bit protocol by telling the
  // motor that there are no more follow on bits.
  if(last_bit == 0) {
    REG_WRITE(GPIO_OUT_W1TS_REG, pin);
    //delayMicroseconds(SYMBOL);
  }
    
  // Inter-frame silence for 56-bit protocols are around 34ms.  However, an 80 bit protocol should
  // reduce this by the transmission of SYMBOL * 24 or 15,360us
  REG_WRITE(GPIO_OUT_W1TC_REG, pin);
  // Below are the original calculations for inter-frame silence.  However, when actually inspecting this from
  // the remote it appears to be closer to 27500us.  The delayMicoseconds call cannot be called with
  // values larger than 16383.
  if(bitLength != 80) {
    delayMicroseconds(13717);
    delayMicroseconds(13717);
  }
}
void RECEIVE_ATTR Transceiver::handleReceive() {
    static unsigned long last_time = 0;
    const long time = micros();
    const unsigned int duration = time - last_time;
    if (duration < bitMin) {
        // The incoming bit is < 448us so it is probably a glitch so blow it off.
        // We need to ignore this bit.
        // REMOVE THIS AFTER WE DETERMINE THAT THE out-of-bounds stuff isn't a problem.  If there are bits
        // from the previous frame then we will capture this data here.
        if(somfy_rx.pulseCount < MAX_TIMINGS && somfy_rx.cpt_synchro_hw > 0) somfy_rx.pulses[somfy_rx.pulseCount++] = duration;
        return;
    }
    last_time = time;
    switch (somfy_rx.status) {
    case waiting_synchro:
        if(somfy_rx.pulseCount < MAX_TIMINGS) somfy_rx.pulses[somfy_rx.pulseCount++] = duration;
        if (duration > tempo_synchro_hw_min && duration < tempo_synchro_hw_max) {
            // We have found a hardware sync bit.  There should be at least 4 of these.
            ++somfy_rx.cpt_synchro_hw;
        }
        else if (duration > tempo_synchro_sw_min && duration < tempo_synchro_sw_max && somfy_rx.cpt_synchro_hw >= 4) {
            // If we have a full hardware sync then we should look for the software sync.  If we have a software sync
            // bit and enough hardware sync bits then we should start receiving data.  It turns out that a 56 bit packet
            // with give 4 or 14 bits of hardware sync.  An 80 bit packet gives 12, 13 or 24 bits of hw sync.  Early on
            // I had some shorter and longer hw syncs but I can no longer repeat this.
            memset(somfy_rx.payload, 0x00, sizeof(somfy_rx.payload));
            somfy_rx.previous_bit = 0x00;
            somfy_rx.waiting_half_symbol = false;
            somfy_rx.cpt_bits = 0;
            // Keep an eye on this as it is possible that we might get fewer or more synchro bits.
            if (somfy_rx.cpt_synchro_hw <= 7) somfy_rx.bit_length = 56;
            else if (somfy_rx.cpt_synchro_hw == 14) somfy_rx.bit_length = 56;
            else if (somfy_rx.cpt_synchro_hw == 13) somfy_rx.bit_length = 80; // The RS485 device sends this sync.
            else if (somfy_rx.cpt_synchro_hw == 12) somfy_rx.bit_length = 80;
            else if (somfy_rx.cpt_synchro_hw > 17) somfy_rx.bit_length = 80;
            else somfy_rx.bit_length = 56;
            //somfy_rx.bit_length = 80;
            somfy_rx.status = receiving_data;
        }
        else {
            // Reset and start looking for hardware sync again.
            somfy_rx.cpt_synchro_hw = 0;
            // Try to capture the wakeup pulse.
            if(duration > tempo_wakeup_min && duration < tempo_wakeup_max)
            {
                memset(&somfy_rx.payload, 0x00, sizeof(somfy_rx.payload));
                somfy_rx.previous_bit = 0x00;
                somfy_rx.waiting_half_symbol = false;
                somfy_rx.cpt_bits = 0;
                somfy_rx.bit_length = 56;
            }
            else if((somfy_rx.pulseCount > 20 && somfy_rx.cpt_synchro_hw == 0) || duration > 250000) {
              somfy_rx.pulseCount = 0;
            }
        }
        break;
    case receiving_data:
        if(somfy_rx.pulseCount < MAX_TIMINGS) somfy_rx.pulses[somfy_rx.pulseCount++] = duration;
        // We should be receiving data at this point.
        if (duration > tempo_symbol_min && duration < tempo_symbol_max && !somfy_rx.waiting_half_symbol) {
            somfy_rx.previous_bit = 1 - somfy_rx.previous_bit;
            // Bits come in high order bit first.
            somfy_rx.payload[somfy_rx.cpt_bits / 8] += somfy_rx.previous_bit << (7 - somfy_rx.cpt_bits % 8);
            ++somfy_rx.cpt_bits;
        }
        else if (duration > tempo_half_symbol_min && duration < tempo_half_symbol_max) {
            if (somfy_rx.waiting_half_symbol) {
                somfy_rx.waiting_half_symbol = false;
                somfy_rx.payload[somfy_rx.cpt_bits / 8] += somfy_rx.previous_bit << (7 - somfy_rx.cpt_bits % 8);
                ++somfy_rx.cpt_bits;
            }
            else {
                somfy_rx.waiting_half_symbol = true;
            }
        }
        else {
            //++somfy_rx.cpt_bits;
            // Start over we are not within our parameters for bit timing.
            memset(&somfy_rx.payload, 0x00, sizeof(somfy_rx.payload));
            somfy_rx.pulseCount = 1;
            somfy_rx.cpt_synchro_hw = 0;
            somfy_rx.previous_bit = 0x00;
            somfy_rx.waiting_half_symbol = false;
            somfy_rx.cpt_bits = 0;
            somfy_rx.bit_length = 56;
            somfy_rx.status = waiting_synchro;
            somfy_rx.pulses[0] = duration;
        }
        break;
    default:
        break;
    }
    if (somfy_rx.status == receiving_data && somfy_rx.cpt_bits >= somfy_rx.bit_length) {
        // Since we are operating within the interrupt all data really needs to be static
        // for the handoff to the frame decoder.  For this reason we are buffering up to
        // 3 total frames.  Althought it may not matter considering the length of a packet
        // will likely not push over the loop timing.  For now lets assume that there
        // may be some pressure on the loop for features.
        if(rx_queue.length >= MAX_RX_BUFFER) {
          // We have overflowed the buffer simply empty the last item
          // in this instance we will simply throw it away.
          uint8_t ndx = rx_queue.index[MAX_RX_BUFFER - 1];
          if(ndx < MAX_RX_BUFFER) rx_queue.items[ndx].pulseCount = 0;
          //memset(&this->items[ndx], 0x00, sizeof(somfy_rx_t));
          rx_queue.index[MAX_RX_BUFFER - 1] = 255;
          rx_queue.length--;
        }
        uint8_t first = 0;
        // Place this record in the first empty slot.  There will
        // be one since we cleared a space above should there
        // be an overflow.
        for(uint8_t i = 0; i < MAX_RX_BUFFER; i++) {
          if(rx_queue.items[i].pulseCount == 0) {
            first = i;
            memcpy(&rx_queue.items[i], &somfy_rx, sizeof(somfy_rx_t));
            break;
          }
        }
        // Move the index so that it is the at position 0.  The oldest item will fall off.
        for(uint8_t i = MAX_RX_BUFFER - 1; i > 0; i--) {
          rx_queue.index[i] = rx_queue.index[i - 1];
        }
        rx_queue.length++;
        rx_queue.index[0] = first;
        memset(&somfy_rx.payload, 0x00, sizeof(somfy_rx.payload));
        somfy_rx.cpt_synchro_hw = 0;
        somfy_rx.previous_bit = 0x00;
        somfy_rx.waiting_half_symbol = false;
        somfy_rx.cpt_bits = 0;
        somfy_rx.pulseCount = 0;
        somfy_rx.status = waiting_synchro;
    }
}
float currFreq = 433.0f;
int currRSSI = -100;
float markFreq = 433.0f;
int markRSSI = -100;
uint32_t lastScan = 0;
void Transceiver::beginFrequencyScan() {
  if(this->config.enabled) {
    this->disableReceive();
    rxmode = 3;
    pinMode(this->config.RXPin, INPUT);
    interruptPin = digitalPinToInterrupt(this->config.RXPin);
    ELECHOUSE_cc1101.setRxBW(this->config.rxBandwidth);              // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
    ELECHOUSE_cc1101.SetRx();
    markFreq = currFreq = 433.0f;
    markRSSI = -100;
    ELECHOUSE_cc1101.setMHZ(currFreq);
    Serial.printf("Begin frequency scan on Pin #%d\n", this->config.RXPin);
    attachInterrupt(interruptPin, handleReceive, CHANGE);
    this->emitFrequencyScan();
  }
}
void Transceiver::processFrequencyScan(bool received) {
  if(this->config.enabled && rxmode == 3) {
    if(received) {
      currRSSI = ELECHOUSE_cc1101.getRssi();
      if((long)(markFreq * 100) == (long)(currFreq * 100)) {
        markRSSI = currRSSI;
      }
      else if(currRSSI >-75) {
        if(currRSSI > markRSSI) {
          markRSSI = currRSSI;
          markFreq = currFreq;
        }
      }
    }
    else {
      currRSSI = -100;
    }
    
    if(millis() - lastScan > 100 && somfy_rx.status == waiting_synchro) {
      lastScan = millis();
      this->emitFrequencyScan();
      currFreq += 0.01f;
      if(currFreq > 434.0f) currFreq = 433.0f;
      ELECHOUSE_cc1101.setMHZ(currFreq);
    }
  }
}
void Transceiver::endFrequencyScan() {
  if(rxmode == 3) {
    rxmode = 0;
    if(interruptPin > 0) detachInterrupt(interruptPin); 
    interruptPin = 0;
    this->config.apply();
    this->emitFrequencyScan();
  }
}
void Transceiver::emitFrequencyScan(uint8_t num) {
  JsonSockEvent *json = sockEmit.beginEmit("frequencyScan");
  json->beginObject();
  json->addElem("scanning", rxmode == 3);
  json->addElem("testFreq", currFreq);
  json->addElem("testRSSI", (int32_t)currRSSI);
  json->addElem("frequency", markFreq);
  json->addElem("RSSI", (int32_t)markRSSI);
  json->endObject();
  sockEmit.endEmit(num);
  /*
  char buf[420];
  snprintf(buf, sizeof(buf), "{\"scanning\":%s,\"testFreq\":%f,\"testRSSI\":%d,\"frequency\":%f,\"RSSI\":%d}", rxmode == 3 ? "true" : "false", currFreq, currRSSI, markFreq, markRSSI); 
  if(num >= 255) sockEmit.sendToClients("frequencyScan", buf);
  else sockEmit.sendToClient(num, "frequencyScan", buf);
  */
}
bool Transceiver::receive(somfy_rx_t *rx) {
    // Check to see if there is anything in the buffer
    if(rx_queue.length > 0) {
      //Serial.printf("Processing receive %d\n", rx_queue.length);
      rx_queue.pop(rx);
      this->frame.decodeFrame(rx);
      this->emitFrame(&this->frame, rx);
      return this->frame.valid;
    }
    return false;
}
void Transceiver::emitFrame(somfy_frame_t *frame, somfy_rx_t *rx) {
  if(sockEmit.activeClients(ROOM_EMIT_FRAME) > 0) {
    JsonSockEvent *json = sockEmit.beginEmit("remoteFrame");
    json->beginObject();
    json->addElem("encKey", frame->encKey);
    json->addElem("address", (uint32_t)frame->remoteAddress);
    json->addElem("rcode", (uint32_t)frame->rollingCode);
    json->addElem("command", translateSomfyCommand(frame->cmd).c_str());
    json->addElem("rssi", (int32_t)frame->rssi);
    json->addElem("bits", rx->bit_length);
    json->addElem("proto", static_cast<uint8_t>(frame->proto));
    json->addElem("valid", frame->valid);
    json->addElem("sync", frame->hwsync);
    if(frame->cmd == somfy_commands::StepUp || frame->cmd == somfy_commands::StepDown)
      json->addElem("stepSize", frame->stepSize);
    json->beginArray("pulses");
    if(rx) {
      for(uint16_t i = 0; i < rx->pulseCount; i++) {
        json->addElem((uint32_t)rx->pulses[i]);
      }
    }
    json->endArray();
    json->endObject();
    sockEmit.endEmitRoom(ROOM_EMIT_FRAME);
    /*
    ClientSocketEvent evt("remoteFrame");
    char buf[30];
    snprintf(buf, sizeof(buf), "{\"encKey\":%d,", frame->encKey);
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"address\":%d,", frame->remoteAddress);
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"rcode\":%d,", frame->rollingCode);
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"command\":\"%s\",", translateSomfyCommand(frame->cmd).c_str());
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"rssi\":%d,", frame->rssi);
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"bits\":%d,", rx->bit_length);
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"proto\":%d,", static_cast<uint8_t>(frame->proto));
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"valid\":%s,", frame->valid ? "true" : "false");
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"sync\":%d,\"pulses\":[", frame->hwsync);
    evt.appendMessage(buf);
    
    if(rx) {
      for(uint16_t i = 0; i < rx->pulseCount; i++) {
        snprintf(buf, sizeof(buf), "%s%d", i != 0 ? "," : "", rx->pulses[i]);
        evt.appendMessage(buf);
      }
    }
    evt.appendMessage("]}");
    sockEmit.sendToRoom(ROOM_EMIT_FRAME, &evt);
    */
  }
}
void Transceiver::clearReceived(void) {
    //packet_received = false;
    //memset(receive_buffer, 0x00, sizeof(receive_buffer));
    if(this->config.enabled)
      //attachInterrupt(interruptPin, handleReceive, FALLING);
      attachInterrupt(interruptPin, handleReceive, CHANGE);
}
void Transceiver::enableReceive(void) {
    uint32_t timing = millis();
    if(rxmode > 0) return;
    if(this->config.enabled) {
      rxmode = 1;
      pinMode(this->config.RXPin, INPUT);
      interruptPin = digitalPinToInterrupt(this->config.RXPin);
      ELECHOUSE_cc1101.SetRx();
      //attachInterrupt(interruptPin, handleReceive, FALLING);
      attachInterrupt(interruptPin, handleReceive, CHANGE);
      Serial.printf("Enabled receive on Pin #%d Timing: %ld\n", this->config.RXPin, millis() - timing);
    }
}
void Transceiver::disableReceive(void) { 
  rxmode = 0;
  if(interruptPin > 0) detachInterrupt(interruptPin); 
  interruptPin = 0;
  
}
void Transceiver::toJSON(JsonResponse& json) {
    json.beginObject("config");
    this->config.toJSON(json);
    json.endObject();
}
/*
bool Transceiver::toJSON(JsonObject& obj) {
    //Serial.println("Setting Transceiver Json");
    JsonObject objConfig = obj.createNestedObject("config");
    this->config.toJSON(objConfig);
    return true;
}
*/
bool Transceiver::fromJSON(JsonObject& obj) {
    if (obj.containsKey("config")) {
      JsonObject objConfig = obj["config"];
      this->config.fromJSON(objConfig);
    }
    return true;
}
bool Transceiver::usesPin(uint8_t pin) {
  if(this->config.enabled) {
    if(this->config.SCKPin == pin ||
      this->config.TXPin == pin ||
      this->config.RXPin == pin ||
      this->config.MOSIPin == pin ||
      this->config.MISOPin == pin ||
      this->config.CSNPin == pin)
      return true;
  }
  return false;  
}
bool Transceiver::save() {
    this->config.save();
    this->config.apply();
    return true;
}
bool Transceiver::end() {
    this->disableReceive();
    return true;
}
void transceiver_config_t::fromJSON(JsonObject& obj) {
    //Serial.print("Deserialize Radio JSON ");
    if(obj.containsKey("type")) this->type = obj["type"];
    if(obj.containsKey("CSNPin")) this->CSNPin = obj["CSNPin"];
    if(obj.containsKey("MISOPin")) this->MISOPin = obj["MISOPin"];
    if(obj.containsKey("MOSIPin")) this->MOSIPin = obj["MOSIPin"];
    if(obj.containsKey("RXPin")) this->RXPin = obj["RXPin"];
    if(obj.containsKey("SCKPin")) this->SCKPin = obj["SCKPin"];
    if(obj.containsKey("TXPin")) this->TXPin = obj["TXPin"];
    if(obj.containsKey("rxBandwidth")) this->rxBandwidth = obj["rxBandwidth"]; // float
    if(obj.containsKey("frequency")) this->frequency = obj["frequency"];  // float
    if(obj.containsKey("deviation")) this->deviation = obj["deviation"];  // float
    if(obj.containsKey("enabled")) this->enabled = obj["enabled"];
    if(obj.containsKey("txPower")) this->txPower = obj["txPower"];
    if(obj.containsKey("proto")) this->proto = static_cast<radio_proto>(obj["proto"].as<uint8_t>());
    /*
    if (obj.containsKey("internalCCMode")) this->internalCCMode = obj["internalCCMode"];
    if (obj.containsKey("modulationMode")) this->modulationMode = obj["modulationMode"];
    if (obj.containsKey("channel")) this->channel = obj["channel"];
    if (obj.containsKey("channelSpacing")) this->channelSpacing = obj["channelSpacing"]; // float
    if (obj.containsKey("dataRate")) this->dataRate = obj["dataRate"]; // float
    if (obj.containsKey("syncMode")) this->syncMode = obj["syncMode"];
    if (obj.containsKey("syncWordHigh")) this->syncWordHigh = obj["syncWordHigh"];
    if (obj.containsKey("syncWordLow")) this->syncWordLow = obj["syncWordLow"];
    if (obj.containsKey("addrCheckMode")) this->addrCheckMode = obj["addrCheckMode"];
    if (obj.containsKey("checkAddr")) this->checkAddr = obj["checkAddr"];
    if (obj.containsKey("dataWhitening")) this->dataWhitening = obj["dataWhitening"];
    if (obj.containsKey("pktFormat")) this->pktFormat = obj["pktFormat"];
    if (obj.containsKey("pktLengthMode")) this->pktLengthMode = obj["pktLengthMode"];
    if (obj.containsKey("pktLength")) this->pktLength = obj["pktLength"];
    if (obj.containsKey("useCRC")) this->useCRC = obj["useCRC"];
    if (obj.containsKey("autoFlushCRC")) this->autoFlushCRC = obj["autoFlushCRC"];
    if (obj.containsKey("disableDCFilter")) this->disableDCFilter = obj["disableCRCFilter"];
    if (obj.containsKey("enableManchester")) this->enableManchester = obj["enableManchester"];
    if (obj.containsKey("enableFEC")) this->enableFEC = obj["enableFEC"];
    if (obj.containsKey("minPreambleBytes")) this->minPreambleBytes = obj["minPreambleBytes"];
    if (obj.containsKey("pqtThreshold")) this->pqtThreshold = obj["pqtThreshold"];
    if (obj.containsKey("appendStatus")) this->appendStatus = obj["appendStatus"];
    if (obj.containsKey("printBuffer")) this->printBuffer = obj["printBuffer"];
    */
    Serial.printf("SCK:%u MISO:%u MOSI:%u CSN:%u RX:%u TX:%u\n", this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin, this->RXPin, this->TXPin);
}
void transceiver_config_t::toJSON(JsonResponse &json) {
    json.addElem("type", this->type);
    json.addElem("TXPin", this->TXPin);
    json.addElem("RXPin", this->RXPin);
    json.addElem("SCKPin", this->SCKPin);
    json.addElem("MOSIPin", this->MOSIPin);
    json.addElem("MISOPin", this->MISOPin);
    json.addElem("CSNPin", this->CSNPin);
    json.addElem("rxBandwidth", this->rxBandwidth); // float
    json.addElem("frequency", this->frequency);  // float
    json.addElem("deviation", this->deviation);  // float
    json.addElem("txPower", this->txPower);
    json.addElem("proto", static_cast<uint8_t>(this->proto));
    json.addElem("enabled", this->enabled);
    json.addElem("radioInit", this->radioInit);
}
/*
void transceiver_config_t::toJSON(JsonObject& obj) {
    obj["type"] = this->type;
    obj["TXPin"] = this->TXPin;
    obj["RXPin"] = this->RXPin;
    obj["SCKPin"] = this->SCKPin;
    obj["MOSIPin"] = this->MOSIPin;
    obj["MISOPin"] = this->MISOPin;
    obj["CSNPin"] = this->CSNPin;
    obj["rxBandwidth"] = this->rxBandwidth; // float
    obj["frequency"] = this->frequency;  // float
    obj["deviation"] = this->deviation;  // float
    obj["txPower"] = this->txPower;
    obj["proto"] = static_cast<uint8_t>(this->proto);
    //obj["internalCCMode"] = this->internalCCMode;
    //obj["modulationMode"] = this->modulationMode;
    //obj["channel"] = this->channel;
    //obj["channelSpacing"] = this->channelSpacing; // float
    //obj["dataRate"] = this->dataRate; // float
    //obj["syncMode"] = this->syncMode;
    //obj["syncWordHigh"] = this->syncWordHigh;
    //obj["syncWordLow"] = this->syncWordLow;
    //obj["addrCheckMode"] = this->addrCheckMode;
    //obj["checkAddr"] = this->checkAddr;
    //obj["dataWhitening"] = this->dataWhitening;
    //obj["pktFormat"] = this->pktFormat;
    //obj["pktLengthMode"] = this->pktLengthMode;
    //obj["pktLength"] = this->pktLength;
    //obj["useCRC"] = this->useCRC;
    //obj["autoFlushCRC"] = this->autoFlushCRC;
    //obj["disableDCFilter"] = this->disableDCFilter;
    //obj["enableManchester"] = this->enableManchester;
    //obj["enableFEC"] = this->enableFEC;
    //obj["minPreambleBytes"] = this->minPreambleBytes;
    //obj["pqtThreshold"] = this->pqtThreshold;
    //obj["appendStatus"] = this->appendStatus;
    //obj["printBuffer"] = somfy.transceiver.printBuffer;
    obj["enabled"] = this->enabled;
    obj["radioInit"] = this->radioInit;
    //Serial.print("Serialize Radio JSON ");
    //Serial.printf("SCK:%u MISO:%u MOSI:%u CSN:%u RX:%u TX:%u\n", this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin, this->RXPin, this->TXPin);
}
*/
void transceiver_config_t::save() {
    pref.begin("CC1101");
    pref.clear();
    pref.putUChar("type", this->type);
    pref.putUChar("TXPin", this->TXPin);
    pref.putUChar("RXPin", this->RXPin);
    pref.putUChar("SCKPin", this->SCKPin);
    pref.putUChar("MOSIPin", this->MOSIPin);
    pref.putUChar("MISOPin", this->MISOPin);
    pref.putUChar("CSNPin", this->CSNPin);
    pref.putFloat("frequency", this->frequency);  // float
    pref.putFloat("deviation", this->deviation);  // float
    pref.putFloat("rxBandwidth", this->rxBandwidth); // float
    pref.putBool("enabled", this->enabled);
    pref.putBool("radioInit", true);
    pref.putChar("txPower", this->txPower);
    pref.putChar("proto", static_cast<uint8_t>(this->proto));
    
    /*
    pref.putBool("internalCCMode", this->internalCCMode);
    pref.putUChar("modulationMode", this->modulationMode);
    pref.putUChar("channel", this->channel);
    pref.putFloat("channelSpacing", this->channelSpacing); // float
    pref.putFloat("rxBandwidth", this->rxBandwidth); // float
    pref.putFloat("dataRate", this->dataRate); // float
    pref.putChar("txPower", this->txPower);
    pref.putUChar("syncMode", this->syncMode);
    pref.putUShort("syncWordHigh", this->syncWordHigh);
    pref.putUShort("syncWordLow", this->syncWordLow);
    pref.putUChar("addrCheckMode", this->addrCheckMode);
    pref.putUChar("checkAddr", this->checkAddr);
    pref.putBool("dataWhitening", this->dataWhitening);
    pref.putUChar("pktFormat", this->pktFormat);
    pref.putUChar("pktLengthMode", this->pktLengthMode);
    pref.putUChar("pktLength", this->pktLength);
    pref.putBool("useCRC", this->useCRC);
    pref.putBool("autoFlushCRC", this->autoFlushCRC);
    pref.putBool("disableDCFilter", this->disableDCFilter);
    pref.putBool("enableManchester", this->enableManchester);
    pref.putBool("enableFEC", this->enableFEC);
    pref.putUChar("minPreambleBytes", this->minPreambleBytes);
    pref.putUChar("pqtThreshold", this->pqtThreshold);
    pref.putBool("appendStatus", this->appendStatus);
    */
    pref.end();
   
    Serial.print("Save Radio Settings ");
    Serial.printf("SCK:%u MISO:%u MOSI:%u CSN:%u RX:%u TX:%u\n", this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin, this->RXPin, this->TXPin);
}
void transceiver_config_t::removeNVSKey(const char *key) {
  if(pref.isKey(key)) {
    Serial.printf("Removing NVS Key: CC1101.%s\n", key);
    pref.remove(key);
  }
}
void transceiver_config_t::load() {
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    switch(ci.model) {
      case esp_chip_model_t::CHIP_ESP32S3:
        Serial.println("Setting S3 Transceiver Defaults...");
        this->TXPin = 15;
        this->RXPin = 14;
        this->MOSIPin = 11;
        this->MISOPin = 13;
        this->SCKPin = 12;
        this->CSNPin = 10;
        break;
      case esp_chip_model_t::CHIP_ESP32S2:
        this->TXPin = 15;
        this->RXPin = 14;
        this->MOSIPin = 35;
        this->MISOPin = 37;
        this->SCKPin = 36;
        this->CSNPin = 34;
        break;
      case esp_chip_model_t::CHIP_ESP32C3:
        this->TXPin = 13;
        this->RXPin = 12;
        this->MOSIPin = 16;
        this->MISOPin = 17;
        this->SCKPin = 15;
        this->CSNPin = 14;
        break;
      default:
        this->TXPin = 13;
        this->RXPin = 12;
        this->MOSIPin = 23;
        this->MISOPin = 19;
        this->SCKPin = 18;
        this->CSNPin = 5;
        break;
    }
    pref.begin("CC1101");
    this->type = pref.getUChar("type", 56);
    this->TXPin = pref.getUChar("TXPin", this->TXPin);
    this->RXPin = pref.getUChar("RXPin", this->RXPin);
    this->SCKPin = pref.getUChar("SCKPin", this->SCKPin);
    this->MOSIPin = pref.getUChar("MOSIPin", this->MOSIPin);
    this->MISOPin = pref.getUChar("MISOPin", this->MISOPin);
    this->CSNPin = pref.getUChar("CSNPin", this->CSNPin);
    this->frequency = pref.getFloat("frequency", this->frequency);  // float
    this->deviation = pref.getFloat("deviation", this->deviation);  // float
    this->enabled = pref.getBool("enabled", this->enabled);
    this->txPower = pref.getChar("txPower", this->txPower);
    this->rxBandwidth = pref.getFloat("rxBandwidth", this->rxBandwidth);
    this->proto = static_cast<radio_proto>(pref.getChar("proto", static_cast<uint8_t>(this->proto)));
    this->removeNVSKey("internalCCMode");
    this->removeNVSKey("modulationMode");
    this->removeNVSKey("channel");
    this->removeNVSKey("channelSpacing");
    this->removeNVSKey("dataRate");
    this->removeNVSKey("syncMode");
    this->removeNVSKey("syncWordHigh");
    this->removeNVSKey("syncWordLow");
    this->removeNVSKey("addrCheckMode");
    this->removeNVSKey("checkAddr");
    this->removeNVSKey("dataWhitening");
    this->removeNVSKey("pktFormat");
    this->removeNVSKey("pktLengthMode");
    this->removeNVSKey("pktLength");
    this->removeNVSKey("useCRC");
    this->removeNVSKey("autoFlushCRC");
    this->removeNVSKey("disableDCFilter");
    this->removeNVSKey("enableManchester");
    this->removeNVSKey("enableFEC");
    this->removeNVSKey("minPreambleBytes");
    this->removeNVSKey("pqtThreshold");
    this->removeNVSKey("appendStatus");
    pref.end();
    //this->printBuffer = somfy.transceiver.printBuffer;
}
void transceiver_config_t::apply() {
    somfy.transceiver.disableReceive();
    bit_length = this->type;    
    if(this->enabled) {
      bool radioInit = true;
      pref.begin("CC1101");
      radioInit = pref.getBool("radioInit", true);
      // If the radio locks up then we can simply reboot and re-enable the radio.
      pref.putBool("radioInit", false);
      this->radioInit = false;
      pref.end();
      if(!radioInit) return;
      Serial.print("Applying radio settings ");
      Serial.printf("Setting Data Pins RX:%u TX:%u\n", this->RXPin, this->TXPin);
      //if(this->TXPin != this->RXPin)
      //  pinMode(this->TXPin, OUTPUT);
      //pinMode(this->RXPin, INPUT);
      // Essentially these call only preform the two functions above.
      if(this->TXPin == this->RXPin)
        ELECHOUSE_cc1101.setGDO0(this->TXPin); // This pin may be shared.
      else
        ELECHOUSE_cc1101.setGDO(this->TXPin, this->RXPin); // GDO0, GDO2
      Serial.printf("Setting SPI Pins SCK:%u MISO:%u MOSI:%u CSN:%u\n", this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin);
      ELECHOUSE_cc1101.setSpiPin(this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin);
      Serial.println("Radio Pins Configured!");
      ELECHOUSE_cc1101.Init();
      ELECHOUSE_cc1101.setCCMode(0);                            // set config for internal transmission mode.
      ELECHOUSE_cc1101.setMHZ(this->frequency);                 // Here you can set your basic frequency. The lib calculates the frequency automatically (default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ. Read More info from datasheet.
      ELECHOUSE_cc1101.setRxBW(this->rxBandwidth);              // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
      ELECHOUSE_cc1101.setDeviation(this->deviation);           // Set the Frequency deviation in kHz. Value from 1.58 to 380.85. Default is 47.60 kHz.
      ELECHOUSE_cc1101.setPA(this->txPower);                    // Set TxPower. The following settings are possible depending on the frequency band.  (-30  -20  -15  -10  -6    0    5    7    10   11   12) Default is max!
      ELECHOUSE_cc1101.setModulation(2);                        // Set modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
      ELECHOUSE_cc1101.setManchester(1);                        // Enables Manchester encoding/decoding. 0 = Disable. 1 = Enable.
      ELECHOUSE_cc1101.setPktFormat(3);                         // Format of RX and TX data. 
                                                                // 0 = Normal mode, use FIFOs for RX and TX. 
                                                                // 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins. 
                                                                // 2 = Random TX mode; sends random data using PN9 generator. Used for test. Works as normal mode, setting 0 (00), in RX. 
                                                                // 3 = Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
      ELECHOUSE_cc1101.setDcFilterOff(0);                       // Disable digital DC blocking filter before demodulator. Only for data rates â‰¤ 250 kBaud The recommended IF frequency changes when the DC blocking is disabled. 
                                                                // 1 = Disable (current optimized). 
                                                                // 0 = Enable (better sensitivity).
      ELECHOUSE_cc1101.setCrc(0);                               // 1 = CRC calculation in TX and CRC check in RX enabled. 0 = CRC disabled for TX and RX.
      ELECHOUSE_cc1101.setCRC_AF(0);                            // Enable automatic flush of RX FIFO when CRC is not OK. This requires that only one packet is in the RXIFIFO and that packet length is limited to the RX FIFO size.
      ELECHOUSE_cc1101.setSyncMode(4);                          // Combined sync-word qualifier mode. 
                                                                // 0 = No preamble/sync. 
                                                                // 1 = 16 sync word bits detected. 
                                                                // 2 = 16/16 sync word bits detected. 
                                                                // 3 = 30/32 sync word bits detected. 
                                                                // 4 = No preamble/sync, carrier-sense above threshold. 
                                                                // 5 = 15/16 + carrier-sense above threshold. 
                                                                // 6 = 16/16 + carrier-sense above threshold. 
                                                                // 7 = 30/32 + carrier-sense above threshold.
      ELECHOUSE_cc1101.setAdrChk(0);                            // Controls address check configuration of received packages. 
                                                                // 0 = No address check. 
                                                                // 1 = Address check, no broadcast. 
                                                                // 2 = Address check and 0 (0x00) broadcast. 
                                                                // 3 = Address check and 0 (0x00) and 255 (0xFF) broadcast.
    
      
      if (!ELECHOUSE_cc1101.getCC1101()) {
          Serial.println("Error setting up the radio");
          this->radioInit = false;
      }
      else {
          Serial.println("Successfully set up the radio");
          somfy.transceiver.enableReceive();
          this->radioInit = true;
      }
      pref.begin("CC1101");
      pref.putBool("radioInit", true);
      pref.end();
      
    }
    else {
      if(this->radioInit) ELECHOUSE_cc1101.setSidle();
      somfy.transceiver.disableReceive();
      this->radioInit = false;
    }
    /*
    ELECHOUSE_cc1101.setChannel(this->channel);               // Set the Channelnumber from 0 to 255. Default is cahnnel 0.
    ELECHOUSE_cc1101.setChsp(this->channelSpacing);           // The channel spacing is multiplied by the channel number CHAN and added to the base frequency in kHz. Value from 25.39 to 405.45. Default is 199.95 kHz.
    ELECHOUSE_cc1101.setDRate(this->dataRate);                // Set the Data Rate in kBaud. Value from 0.02 to 1621.83. Default is 99.97 kBaud!
    ELECHOUSE_cc1101.setSyncMode(this->syncMode);             // Combined sync-word qualifier mode. 0 = No preamble/sync. 1 = 16 sync word bits detected. 2 = 16/16 sync word bits detected. 3 = 30/32 sync word bits detected. 4 = No preamble/sync, carrier-sense above threshold. 5 = 15/16 + carrier-sense above threshold. 6 = 16/16 + carrier-sense above threshold. 7 = 30/32 + carrier-sense above threshold.
    ELECHOUSE_cc1101.setSyncWord(this->syncWordHigh, this->syncWordLow); // Set sync word. Must be the same for the transmitter and receiver. (Syncword high, Syncword low)
    ELECHOUSE_cc1101.setAdrChk(this->addrCheckMode);          // Controls address check configuration of received packages. 0 = No address check. 1 = Address check, no broadcast. 2 = Address check and 0 (0x00) broadcast. 3 = Address check and 0 (0x00) and 255 (0xFF) broadcast.
    ELECHOUSE_cc1101.setAddr(this->checkAddr);                // Address used for packet filtration. Optional broadcast addresses are 0 (0x00) and 255 (0xFF).
    ELECHOUSE_cc1101.setWhiteData(this->dataWhitening);       // Turn data whitening on / off. 0 = Whitening off. 1 = Whitening on.
    ELECHOUSE_cc1101.setPktFormat(this->pktFormat);           // Format of RX and TX data. 0 = Normal mode, use FIFOs for RX and TX. 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins. 2 = Random TX mode; sends random data using PN9 generator. Used for test. Works as normal mode, setting 0 (00), in RX. 3 = Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
    ELECHOUSE_cc1101.setLengthConfig(this->pktLengthMode);    // 0 = Fixed packet length mode. 1 = Variable packet length mode. 2 = Infinite packet length mode. 3 = Reserved
    ELECHOUSE_cc1101.setPacketLength(this->pktLength);        // Indicates the packet length when fixed packet length mode is enabled. If variable packet length mode is used, this value indicates the maximum packet length allowed.
    ELECHOUSE_cc1101.setCrc(this->useCRC);                    // 1 = CRC calculation in TX and CRC check in RX enabled. 0 = CRC disabled for TX and RX.
    ELECHOUSE_cc1101.setCRC_AF(this->autoFlushCRC);           // Enable automatic flush of RX FIFO when CRC is not OK. This requires that only one packet is in the RXIFIFO and that packet length is limited to the RX FIFO size.
    ELECHOUSE_cc1101.setDcFilterOff(this->disableDCFilter);   // Disable digital DC blocking filter before demodulator. Only for data rates â‰¤ 250 kBaud The recommended IF frequency changes when the DC blocking is disabled. 1 = Disable (current optimized). 0 = Enable (better sensitivity).
    ELECHOUSE_cc1101.setManchester(this->enableManchester);   // Enables Manchester encoding/decoding. 0 = Disable. 1 = Enable.
    ELECHOUSE_cc1101.setFEC(this->enableFEC);                 // Enable Forward Error Correction (FEC) with interleaving for packet payload (Only supported for fixed packet length mode. 0 = Disable. 1 = Enable.
    ELECHOUSE_cc1101.setPRE(this->minPreambleBytes);          // Sets the minimum number of preamble bytes to be transmitted. Values: 0 : 2, 1 : 3, 2 : 4, 3 : 6, 4 : 8, 5 : 12, 6 : 16, 7 : 24
    ELECHOUSE_cc1101.setPQT(this->pqtThreshold);              // Preamble quality estimator threshold. The preamble quality estimator increases an internal counter by one each time a bit is received that is different from the previous bit, and decreases the counter by 8 each time a bit is received that is the same as the last bit. A threshold of 4âˆ™PQT for this counter is used to gate sync word detection. When PQT=0 a sync word is always accepted.
    ELECHOUSE_cc1101.setAppendStatus(this->appendStatus);     // When enabled, two status bytes will be appended to the payload of the packet. The status bytes contain RSSI and LQI values, as well as CRC OK.
    */
    //somfy.transceiver.printBuffer = this->printBuffer;
}
bool Transceiver::begin() {
    this->config.load();
    this->config.apply();
    rx_queue.init();
    return true;
}
void Transceiver::loop() {
  somfy_rx_t rx;
  if(rxmode == 3) {
    if(this->receive(&rx))
      this->processFrequencyScan(true);
    else
      this->processFrequencyScan(false);
  }
  else if (this->receive(&rx)) {
    for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
      if(somfy.repeaters[i] == frame.remoteAddress) {
        tx_queue.push(&rx);
        Serial.println("Queued repeater frame...");
        break;
      }
    }
    somfy.processFrame(this->frame, false);
  }
  else {
    somfy.processWaitingFrame();
    // Check to see if there is anything in the buffer
    if(tx_queue.length > 0 && millis() > tx_queue.delay_time && somfy_rx.cpt_synchro_hw == 0) {
      this->beginTransmit();
      somfy_tx_t tx;
      
      tx_queue.pop(&tx);
      Serial.printf("Sending frame %d - %d-BIT [", tx.hwsync, tx.bit_length);
      for(uint8_t j = 0; j < 10; j++) {
        Serial.print(tx.payload[j]);
        if(j < 9) Serial.print(", ");
      }
      Serial.println("]");
      this->sendFrame(tx.payload, tx.hwsync, tx.bit_length);
      tx_queue.delay_time = millis() + TX_QUEUE_DELAY;
      
      /*
      while(tx_queue.length > 0 && tx_queue.pop(&tx)) {
        Serial.printf("Sending frame %d - %d-BIT [", tx.hwsync, tx.bit_length);
        for(uint8_t j = 0; j < 10; j++) {
          Serial.print(tx.payload[j]);
          if(j < 9) Serial.print(", ");
        }
        Serial.println("]");
        this->sendFrame(tx.payload, tx.hwsync, tx.bit_length);
      }
      */
      this->endTransmit();
    }
  }
}
somfy_frame_t& Transceiver::lastFrame() { return this->frame; }
void Transceiver::beginTransmit() {
    if(this->config.enabled) {
      this->disableReceive();
      pinMode(this->config.TXPin, OUTPUT);
      digitalWrite(this->config.TXPin, 0);
      ELECHOUSE_cc1101.SetTx();
    }
}
void Transceiver::endTransmit() {
    if(this->config.enabled) {
      ELECHOUSE_cc1101.setSidle();
      //delay(100);
      this->enableReceive();
    }
}
