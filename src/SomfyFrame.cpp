#include <Arduino.h>
#include "esp_log.h"
#include "SomfyFrame.h"

static const char *TAG = "Somfy";

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
    // The names translateSomfyCommand(cmd) reports so they parse back.
    else if (string.equalsIgnoreCase("Sun Flag")) return somfy_commands::SunFlag;
    else if (string.equalsIgnoreCase("Step Up")) return somfy_commands::StepUp;
    else if (string.equalsIgnoreCase("Step Down")) return somfy_commands::StepDown;
    else if (string.equalsIgnoreCase("My+Up")) return somfy_commands::MyUp;
    else if (string.equalsIgnoreCase("Up+Down")) return somfy_commands::UpDown;
    else if (string.equalsIgnoreCase("My+Down")) return somfy_commands::MyDown;
    else if (string.equalsIgnoreCase("My+Up+Down")) return somfy_commands::MyUpDown;
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
somfy_commands flipCommand(somfy_commands cmd) {
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
      return cmd;
  }
}
somfy_commands sunFlagCommand(bool on) { return on ? somfy_commands::SunFlag : somfy_commands::Flag; }
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
    this->rawCmd = decoded[1] >> 4;
    this->cmd = (somfy_commands)(this->rawCmd);
    if(this->cmd == somfy_commands::RTWProto) {
      if(this->encKey >= 160) {
        this->proto = radio_proto::RTS;
        if(this->encKey == 164) this->cmd = somfy_commands::Toggle;
      }
      else if(this->encKey > 148) {
        this->proto = radio_proto::RTV;
        this->cmd = (somfy_commands)(this->encKey - 148);
      }
      else if(this->encKey >= 133) {
        this->proto = radio_proto::RTW;
        this->cmd = (somfy_commands)(this->encKey - 132);
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
        ESP_LOGW(TAG, "INVALID FRAME KEY:%d ADDR:%d CMD:%s RCODE:%d",
            this->encKey, this->remoteAddress,
            translateSomfyCommand(this->cmd).c_str(), this->rollingCode);
        ESP_LOGD(TAG, "    KEY  1   2   3   4   5   6  ");
        ESP_LOGD(TAG, "--------------------------------");
        char enc_buf[64], dec_buf[64];
        int enc_pos = 0, dec_pos = 0;
        enc_pos += snprintf(enc_buf + enc_pos, sizeof(enc_buf) - enc_pos, "ENC ");
        dec_pos += snprintf(dec_buf + dec_pos, sizeof(dec_buf) - dec_pos, "DEC ");
        for (byte i = 0; i < 10; i++) {
            enc_pos += snprintf(enc_buf + enc_pos, sizeof(enc_buf) - enc_pos, "%3d ", frame[i]);
            dec_pos += snprintf(dec_buf + dec_pos, sizeof(dec_buf) - dec_pos, "%3d ", decoded[i]);
        }
        ESP_LOGD(TAG, "%s", enc_buf);
        ESP_LOGD(TAG, "%s", dec_buf);
    }
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
    ESP_LOGD(TAG, "----------- Receiving -------------");
    ESP_LOGD(TAG, "RSSI:%d LQI:%d", this->rssi, this->lqi);
    ESP_LOGD(TAG, "CMD:%s ADDR:%d RCODE:%d",
        translateSomfyCommand(this->cmd).c_str(), this->remoteAddress, this->rollingCode);
    ESP_LOGD(TAG, "KEY:%02X CS:%d", this->encKey, this->checksum);
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
bool startsMovement(somfy_commands cmd) {
  switch(cmd) {
    case somfy_commands::My:
    case somfy_commands::Up:
    case somfy_commands::Down:
    case somfy_commands::StepUp:
    case somfy_commands::StepDown:
    case somfy_commands::Toggle:
    case somfy_commands::Favorite:
      return true;
    default:
      return false;
  }
}
