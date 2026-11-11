#ifndef SOMFY_FRAME_H
#define SOMFY_FRAME_H
#include <Arduino.h>

// The RTS frame codec and command names.  Kept free of the radio, storage and
// network so it can be unit tested on the PC (pio test -e native).
enum class radio_proto : byte { // Ordinal byte 0-255
  RTS = 0x00,
  RTW = 0x01,
  RTV = 0x02,
  GP_Relay = 0x08,
  GP_Remote = 0x09
};
enum class somfy_commands : byte {
    Unknown0 = 0x0,
    My = 0x1,
    Up = 0x2,
    MyUp = 0x3,
    Down = 0x4,
    MyDown = 0x5,
    UpDown = 0x6,
    MyUpDown = 0x7,
    Prog = 0x8,
    SunFlag = 0x9,
    Flag = 0xA,
    StepDown = 0xB,
    Toggle = 0xC,
    UnknownD = 0xD,
    Sensor = 0xE,
    RTWProto = 0xF, // RTW Protocol
    // Command extensions for 80 bit frames
    StepUp = 0x8B,
    Favorite = 0xC1,
    Stop = 0xF1
};
String translateSomfyCommand(const somfy_commands cmd);
somfy_commands translateSomfyCommand(const String& string);
// The command with up and down swapped, for motors wired the other way round.
somfy_commands flipCommand(somfy_commands cmd);
// The command that turns the sun flag on (SunFlag) or off (Flag).
somfy_commands sunFlagCommand(bool on);
// Whether cmd can set a shade moving.
bool startsMovement(somfy_commands cmd);

struct somfy_rx_t;
struct somfy_frame_t {
    bool valid = false;
    bool processed = false;
    bool synonym = false;
    radio_proto proto = radio_proto::RTS;
    int rssi = 0;
    byte lqi = 0x0;
    somfy_commands cmd;
    uint32_t remoteAddress = 0;
    uint16_t rollingCode = 0;
    uint8_t encKey = 0xA7;
    uint8_t checksum = 0;
    uint8_t hwsync = 0;
    uint8_t repeats = 0;
    uint32_t await = 0;
    uint8_t bitLength = 56;
    uint16_t pulseCount = 0;
    uint8_t stepSize = 0;
    uint8_t rawCmd = 0;
    void print();
    void encode80BitFrame(byte *frame, uint8_t repeat);
    byte calc80Checksum(byte b0, byte b1, byte b2);
    byte encode80Byte7(byte start, uint8_t repeat);
    void encodeFrame(byte *frame);
    void decodeFrame(byte* frame);
    void decodeFrame(somfy_rx_t *rx);
    bool isRepeat(somfy_frame_t &f);
    bool isSynonym(somfy_frame_t &f);
    void copy(somfy_frame_t &f);
};
#endif
