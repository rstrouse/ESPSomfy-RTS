#ifndef SOMFY_H
#define SOMFY_H

#define SOMFY_MAX_SHADES 32
#define SOMFY_MAX_LINKED_REMOTES 5

enum class somfy_commands : byte {
    My = 0x1,
    Up = 0x2,
    MyUp = 0x3,
    Down = 0x4,
    MyDown = 0x5,
    UpDown = 0x6,
    Prog = 0x8,
    SunFlag = 0x9,
    Flag = 0xA
};

String translateSomfyCommand(const somfy_commands cmd);
somfy_commands translateSomfyCommand(const String& string);

typedef struct somfy_frame_t {
    bool valid = false;
    bool processed = false;
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
    void print();
    void encodeFrame(byte *frame);
    void decodeFrame(byte* frame);
    bool isRepeat(somfy_frame_t &f);
    void copy(somfy_frame_t &f);
};

class SomfyRemote {
  // These sizes for the data have been
  // confirmed.  The address is actually 24bits
  // and the rolling code is 16 bits.
  protected:
    char m_remotePrefId[10] = "";
    uint32_t m_remoteAddress = 0;
  public:
    char *getRemotePrefId() {return m_remotePrefId;}
    virtual bool toJSON(JsonObject &obj);
    virtual void setRemoteAddress(uint32_t address);
    virtual uint32_t getRemoteAddress();
    virtual uint16_t getNextRollingCode();
    virtual uint16_t setRollingCode(uint16_t code);
    uint16_t lastRollingCode = 0;
    virtual void sendCommand(somfy_commands cmd, uint8_t repeat = 1);
};
class SomfyLinkedRemote : public SomfyRemote {
  public:
    SomfyLinkedRemote();    
};
class SomfyShade : public SomfyRemote {
  protected:
    uint8_t shadeId = 255;
    uint64_t moveStart = 0;
    float startPos = 0.0;
    bool seekingPos = false;
    bool seekingMyPos = false;
    bool settingMyPos = false;
    uint32_t awaitMy = 0;
  public:
    void load();
    somfy_frame_t lastFrame;
    float currentPos = 0.0;
    //uint16_t movement = 0;
    int8_t direction = 0; // 0 = stopped, 1=down, -1=up.
    uint8_t position = 0;
    uint8_t target = 0;
    uint8_t myPos = 255;
    SomfyLinkedRemote linkedRemotes[SOMFY_MAX_LINKED_REMOTES];
    bool paired = false;
    bool fromJSON(JsonObject &obj);
    bool toJSON(JsonObject &obj) override;
    char name[21] = "";
    void setShadeId(uint8_t id) { shadeId = id; }
    uint8_t getShadeId() { return shadeId; }
    uint16_t upTime = 10000;
    uint16_t downTime = 1000;
    bool save();
    void checkMovement();
    void processFrame(somfy_frame_t &frame, bool internal = false);
    void setMovement(int8_t dir);
    void setTarget(uint8_t target);
    void moveToTarget(uint8_t target);
    void sendCommand(somfy_commands cmd, uint8_t repeat = 1);
    bool linkRemote(uint32_t remoteAddress, uint16_t rollingCode = 0);
    bool unlinkRemote(uint32_t remoteAddress);
    void emitState(const char *evt = "shadeState");
    void emitState(uint8_t num, const char *evt = "shadeState");
    void setMyPosition(uint8_t target);
    void moveToMyPosition();
    void processWaitingFrame();
    void publish();
};

typedef struct transceiver_config_t {
    bool printBuffer = false;
    bool enabled = true;
    uint8_t type = 56;                // 56 or 80 bit protocol.
    uint8_t SCKPin = 18;
    uint8_t TXPin = 12;
    uint8_t RXPin = 13;
    uint8_t MOSIPin = 23;
    uint8_t MISOPin = 19;
    uint8_t CSNPin = 5;
    bool internalCCMode = false;      // Use internal transmission mode FIFO buffers.
    byte modulationMode = 2;          // Modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
    float frequency = 433.42;         // Basic frequency
    float deviation = 47.60;          // Set the Frequency deviation in kHz. Value from 1.58 to 380.85. Default is 47.60 kHz.
    uint8_t channel = 0;              // The channel number from 0 to 255
    float channelSpacing = 199.95;    // Channel spacing in multiplied by the channel number and added to the base frequency in kHz. 25.39 to 405.45.  Default 199.95
    float rxBandwidth = 812.5;        // Receive bandwidth in kHz.  Value from 58.03 to 812.50.  Default is 99.97kHz.
    float dataRate = 99.97;           // The data rate in kBaud.  0.02 to 1621.83 Default is 99.97.
    int8_t txPower = 10;              // Transmission power {-30, -20, -15, -10, -6, 0, 5, 7, 10, 11, 12}.  Default is 12.
    uint8_t syncMode = 0;             // 0=No preamble/sync, 
    // 1=16 sync word bits detected, 
    // 2=16/16 sync words bits detected. 
    // 3=30/32 sync word bits detected, 
    // 4=No preamble/sync carrier above threshold
    // 5=15/16 + carrier above threshold. 
    // 6=16/16 + carrier-sense above threshold
    // 7=0/32 + carrier-sense above threshold
    uint16_t syncWordHigh = 211;      // The sync word used to the sync mode.
    uint16_t syncWordLow = 145;       // The sync word used to the sync mode.
    uint8_t addrCheckMode = 0;        // 0=No address filtration
    // 1=Check address without broadcast.
    // 2=Address check with 0 as broadcast.
    // 3=Address check with 0 or 255 as broadcast.
    uint8_t checkAddr = 0;            // Packet filter address depending on addrCheck settings.
    bool dataWhitening = false;       // Indicates whether data whitening should be applied.
    uint8_t pktFormat = 0;            // 0=Use FIFO buffers form RX and TX
    // 1=Synchronous serial mode.  RX on GDO0 and TX on either GDOx pins.
    // 2=Random TX mode.  Send data using PN9 generator.
    // 3=Asynchronous serial mode.  RX on GDO0 and TX on either GDOx pins.
    uint8_t pktLengthMode = 0;        // 0=Fixed packet length
    // 1=Variable packet length
    // 2=Infinite packet length
    // 3=Reserved
    uint8_t pktLength = 0;            // Packet length
    bool useCRC = false;              // Indicates whether CRC is to be used.
    bool autoFlushCRC = false;        // Automatically flush RX FIFO when CRC fails.  If more than one packet is in the buffer it too will be flushed.
    bool disableDCFilter = false;     // Digital blocking filter for demodulator.  Only for data rates <= 250k.
    bool enableManchester = true;     // Enable/disable Manchester encoding.
    bool enableFEC = false;           // Enable/disable forward error correction.
    uint8_t minPreambleBytes = 0;     // The minimum number of preamble bytes to be transmitten.
    // 0=2bytes
    // 1=3bytes
    // 2=4bytes
    // 3=6bytes
    // 4=8bytes
    // 5=12bytes
    // 6=16bytes
    // 7=24bytes
    uint8_t pqtThreshold = 0;         // Preamble quality estimator threshold.  The preable quality estimator increase an internal counter by one each time a bit is received that is different than the prevoius bit and
    // decreases the bounter by 8 each time a bit is received that is the same as the lats bit.  A threshold of 4 PQT for this counter is used to gate sync word detection.  
    // When PQT = 0 a sync word is always accepted.
    bool appendStatus = false;        // Appends the RSSI and LQI values to the TX packed as well as the CRC.
    void fromJSON(JsonObject& obj);
    void toJSON(JsonObject& obj);
    void save();
    void load();
    void apply();
};
class Transceiver {
  private:
    static void handleReceive();
    bool _received = false;
    somfy_frame_t frame;
  public:
    transceiver_config_t config;
    bool printBuffer = false;
    bool toJSON(JsonObject& obj);
    bool fromJSON(JsonObject& obj);
    bool save();
    bool begin();
    void loop();
    bool end();
    bool receive();
    void clearReceived();
    void enableReceive();
    void disableReceive();
    somfy_frame_t& lastFrame();
    void sendFrame(byte *frame, uint8_t sync);
    void beginTransmit();
    void endTransmit();
};
class SomfyShadeController {
  protected:
    uint8_t m_shadeIds[SOMFY_MAX_SHADES];
  public:
    uint32_t startingAddress;
    uint8_t getNextShadeId();
    uint32_t getNextRemoteAddress(uint8_t shadeId);
    SomfyShadeController();
    Transceiver transceiver;
    SomfyShade *addShade();
    SomfyShade *addShade(JsonObject &obj);
    bool deleteShade(uint8_t shadeId);
    bool begin();
    void loop();
    void end();
    SomfyShade shades[SOMFY_MAX_SHADES];
    bool toJSON(DynamicJsonDocument &doc);
    bool toJSON(JsonArray &arr);
    bool toJSON(JsonObject &obj);
    uint8_t shadeCount();
    SomfyShade * getShadeById(uint8_t shadeId);
    SomfyShade * findShadeByRemoteAddress(uint32_t address);
    void sendFrame(somfy_frame_t &frame, uint8_t repeats = 0);
    void processFrame(somfy_frame_t &frame, bool internal = false);
    void emitState(uint8_t num = 255);
    void publish();
    void processWaitingFrame();
};

#endif
