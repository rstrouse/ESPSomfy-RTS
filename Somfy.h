#ifndef SOMFY_H
#define SOMFY_H
#include "ConfigSettings.h"
#include "WResp.h"

#define SOMFY_MAX_SHADES 32
#define SOMFY_MAX_GROUPS 16
#define SOMFY_MAX_LINKED_REMOTES 7
#define SOMFY_MAX_GROUPED_SHADES 32
#define SOMFY_MAX_ROOMS 16
#define SOMFY_MAX_REPEATERS 7

#define SECS_TO_MILLIS(x) ((x) * 1000)
#define MINS_TO_MILLIS(x) SECS_TO_MILLIS((x) * 60)

#define SOMFY_SUN_TIMEOUT MINS_TO_MILLIS(2)
#define SOMFY_NO_SUN_TIMEOUT MINS_TO_MILLIS(20)

#define SOMFY_WIND_TIMEOUT SECS_TO_MILLIS(2)
#define SOMFY_NO_WIND_TIMEOUT MINS_TO_MILLIS(12)
#define SOMFY_NO_WIND_REMOTE_TIMEOUT SECS_TO_MILLIS(30)


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
enum class group_types : byte {
  channel = 0x00
};
enum class shade_types : byte {
  roller = 0x00,
  blind = 0x01,
  ldrapery = 0x02,
  awning = 0x03,
  shutter = 0x04,
  garage1 = 0x05,
  garage3 = 0x06,
  rdrapery = 0x07,
  cdrapery = 0x08,
  drycontact = 0x09,
  drycontact2 = 0x0A,
  lgate = 0x0B,
  cgate = 0x0C,
  rgate = 0x0D,
  lgate1 = 0x0E,
  cgate1 = 0x0F,
  rgate1 = 0x10
};
enum class tilt_types : byte {
  none = 0x00,
  tiltmotor = 0x01,
  integrated = 0x02,
  tiltonly = 0x03,
  euromode = 0x04
};
String translateSomfyCommand(const somfy_commands cmd);
somfy_commands translateSomfyCommand(const String& string);

#define MAX_TIMINGS 300
#define MAX_RX_BUFFER 3
#define MAX_TX_BUFFER 5

typedef enum {
    waiting_synchro = 0,
    receiving_data = 1,
    complete = 2
} t_status;

struct somfy_rx_t {
    void clear() {
      this->status = t_status::waiting_synchro;
      this->bit_length = 56;
      this->cpt_synchro_hw = 0;
      this->cpt_bits = 0;
      this->previous_bit = 0;
      this->waiting_half_symbol = false;
      memset(this->payload, 0, sizeof(this->payload));
      memset(this->pulses, 0, sizeof(this->pulses));
      this->pulseCount = 0;
    }
    t_status status;
    uint8_t bit_length = 56;
    uint8_t cpt_synchro_hw = 0;
    uint8_t cpt_bits = 0;
    uint8_t previous_bit = 0;
    bool waiting_half_symbol;
    uint8_t payload[10];
    unsigned int pulses[MAX_TIMINGS];
    uint16_t pulseCount = 0;
};
// A simple FIFO queue to hold rx buffers.  We are using
// a byte index to make it so we don't have to reorganize
// the storage each time we push or pop.
struct somfy_rx_queue_t {
  void init();
  uint8_t length = 0;
  uint8_t index[MAX_RX_BUFFER];
  somfy_rx_t items[MAX_RX_BUFFER];
  void push(somfy_rx_t *rx);
  bool pop(somfy_rx_t *rx);
};
struct somfy_tx_t {
  void clear() {
    this->hwsync = 0;
    this->bit_length = 0;
    memset(this->payload, 0x00, sizeof(this->payload));
  }
  uint8_t hwsync = 0;
  uint8_t bit_length = 0;
  uint8_t payload[10] = {};
};
struct somfy_tx_queue_t {
  somfy_tx_queue_t() { this->clear(); }
  void clear() {
    for (uint8_t i = 0; i < MAX_TX_BUFFER; i++) {
      this->index[i] = 255;
      this->items[i].clear();
    }
    this->length = 0;
  }
  unsigned long delay_time = 0;
  uint8_t length = 0;
  uint8_t index[MAX_TX_BUFFER] = {255};
  somfy_tx_t items[MAX_TX_BUFFER];
  bool pop(somfy_tx_t *tx);
  void push(somfy_rx_t *rx); // Used for repeats
  void push(uint8_t hwsync, byte *payload, uint8_t bit_length);
};

enum class somfy_flags_t : byte {
    SunFlag = 0x01,
    SunSensor = 0x02,
    DemoMode = 0x04,
    Light = 0x08,
    Windy = 0x10,
    Sunny = 0x20,
    Lighted = 0x40,
    SimMy = 0x80
};
enum class gpio_flags_t : byte {
  LowLevelTrigger = 0x01
};
struct somfy_relay_t {
  uint32_t remoteAddress = 0;
  uint8_t sync = 0;
  byte frame[10] = {0};
};
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

class SomfyRoom {
  public:
    uint8_t roomId = 0;
    char name[21] = "";
    int8_t sortOrder = 0;
    void clear();
    bool save();
    bool fromJSON(JsonObject &obj);
    void toJSON(JsonResponse &json);
    void emitState(const char *evt = "roomState");
    void emitState(uint8_t num, const char *evt = "roomState");
    void publish();
    void unpublish();
};

class SomfyRemote {
  // These sizes for the data have been
  // confirmed.  The address is actually 24bits
  // and the rolling code is 16 bits.
  protected:
    char m_remotePrefId[11] = "";
    uint32_t m_remoteAddress = 0;
  public:
    radio_proto proto = radio_proto::RTS;
    uint8_t gpioFlags = 0;
    int8_t gpioDir = 0;
    uint8_t gpioUp = 0;
    uint8_t gpioDown = 0;
    uint8_t gpioMy = 0;
    uint32_t gpioRelease = 0;
    somfy_frame_t lastFrame;
    bool flipCommands = false;
    uint16_t lastRollingCode = 0;
    uint8_t flags = 0;
    uint8_t bitLength = 0;
    uint8_t repeats = 1;
    virtual bool isLastCommand(somfy_commands cmd);
    char *getRemotePrefId() {return m_remotePrefId;}
    virtual void toJSON(JsonResponse &json);
    virtual void setRemoteAddress(uint32_t address);
    virtual uint32_t getRemoteAddress();
    virtual uint16_t getNextRollingCode();
    virtual uint16_t setRollingCode(uint16_t code);
    bool hasSunSensor();
    bool hasLight();
    bool simMy();
    void setSunSensor(bool bHasSensor);
    void setLight(bool bHasLight);
    void setSimMy(bool bSimMy);
    virtual void sendCommand(somfy_commands cmd);
    virtual void sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    void sendSensorCommand(int8_t isWindy, int8_t isSunny, uint8_t repeat);
    void repeatFrame(uint8_t repeat);
    virtual uint16_t p_lastRollingCode(uint16_t code);
    somfy_commands transformCommand(somfy_commands cmd);
    virtual void triggerGPIOs(somfy_frame_t &frame);
   
};
class SomfyLinkedRemote : public SomfyRemote {
  public:
    SomfyLinkedRemote();    
};
class SomfyShade : public SomfyRemote {
  protected:
    uint8_t shadeId = 255;
    uint64_t moveStart = 0;
    uint64_t tiltStart = 0;
    uint64_t noSunStart = 0;
    uint64_t sunStart = 0;
    uint64_t windStart = 0;
    uint64_t windLast = 0;
    uint64_t noWindStart = 0;
    bool noSunDone = true;
    bool sunDone = true;
    bool windDone = true;
    bool noWindDone = true;
    float startPos = 0.0f;
    float startTiltPos = 0.0f;
    bool settingMyPos = false;
    bool settingPos = false;
    bool settingTiltPos = false;
    uint32_t awaitMy = 0;
  public:
    uint8_t roomId = 0;
    int8_t sortOrder = 0;
    bool flipPosition = false;
    shade_types shadeType = shade_types::roller;
    tilt_types tiltType = tilt_types::none;
    #ifdef USE_NVS
    void load();
    #endif
    float currentPos = 0.0f;
    float currentTiltPos = 0.0f;
    int8_t lastMovement = 0;
    int8_t direction = 0; // 0 = stopped, 1=down, -1=up.
    int8_t tiltDirection = 0; // 0=stopped, 1=clockwise, -1=counter clockwise
    float target = 0.0f;
    float tiltTarget = 0.0f;
    float myPos = -1.0f;
    float myTiltPos = -1.0f;
    SomfyLinkedRemote linkedRemotes[SOMFY_MAX_LINKED_REMOTES];
    bool paired = false;
    int8_t validateJSON(JsonObject &obj);
    void toJSONRef(JsonResponse &json);
    int8_t fromJSON(JsonObject &obj);
    void toJSON(JsonResponse &json) override;
    
    char name[21] = "";
    void setShadeId(uint8_t id) { shadeId = id; }
    uint8_t getShadeId() { return shadeId; }
    uint32_t upTime = 10000;
    uint32_t downTime = 10000;
    uint32_t tiltTime = 7000;
    uint16_t stepSize = 100;
    bool save();
    bool isIdle();
    bool isInGroup();
    void checkMovement();
    void processFrame(somfy_frame_t &frame, bool internal = false);
    void processInternalCommand(somfy_commands cmd, uint8_t repeat = 1);
    void setTiltMovement(int8_t dir);
    void setMovement(int8_t dir);
    void setTarget(float target);
    bool isAtTarget();
    bool isToggle();
    void moveToTarget(float pos, float tilt = -1.0f);
    void moveToTiltTarget(float target);
    void sendTiltCommand(somfy_commands cmd);
    void sendCommand(somfy_commands cmd);
    void sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    bool linkRemote(uint32_t remoteAddress, uint16_t rollingCode = 0);
    bool unlinkRemote(uint32_t remoteAddress);
    void emitState(const char *evt = "shadeState");
    void emitState(uint8_t num, const char *evt = "shadeState");
    void emitCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt = "shadeCommand");
    void emitCommand(uint8_t num, somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt = "shadeCommand");
    void setMyPosition(int8_t pos, int8_t tilt = -1);
    void moveToMyPosition();
    void processWaitingFrame();
    void publish();
    void unpublish();
    static void unpublish(uint8_t id);
    static void unpublish(uint8_t id, const char *topic);
    void publishState();
    void commit();
    void commitShadePosition();
    void commitTiltPosition();
    void commitMyPosition();
    void clear();
    int8_t transformPosition(float fpos);
    void setGPIOs();
    void triggerGPIOs(somfy_frame_t &frame);
    bool usesPin(uint8_t pin);
    // State Setters
    int8_t p_direction(int8_t dir);
    int8_t p_tiltDirection(int8_t dir);
    float p_target(float target);
    float p_tiltTarget(float target);
    float p_myPos(float pos);
    float p_myTiltPos(float pos);
    bool p_flag(somfy_flags_t flag, bool val);
    bool p_sunFlag(bool val);
    bool p_sunny(bool val);
    bool p_windy(bool val);
    float p_currentPos(float pos);
    float p_currentTiltPos(float pos);
    uint16_t p_lastRollingCode(uint16_t code);
    bool publish(const char *topic, const char *val, bool retain = false);
    bool publish(const char *topic, uint8_t val, bool retain = false);
    bool publish(const char *topic, int8_t val, bool retain = false);
    bool publish(const char *topic, uint32_t val, bool retain = false);
    bool publish(const char *topic, uint16_t val, bool retain = false);
    bool publish(const char *topic, bool val, bool retain = false);
    void publishDisco();
    void unpublishDisco();
};
class SomfyGroup : public SomfyRemote {
  protected:
    uint8_t groupId = 255;
  public:
    uint8_t roomId = 0;
    int8_t sortOrder = 0;
    group_types groupType = group_types::channel;
    int8_t direction = 0; // 0 = stopped, 1=down, -1=up.
    char name[21] = "";
    uint8_t linkedShades[SOMFY_MAX_GROUPED_SHADES];
    void setGroupId(uint8_t id) { groupId = id; }
    uint8_t getGroupId() { return groupId; }
    bool save();
    void clear();
    bool fromJSON(JsonObject &obj);
    //bool toJSON(JsonObject &obj);
    void toJSON(JsonResponse &json);
    void toJSONRef(JsonResponse &json);
    
    bool linkShade(uint8_t shadeId);
    bool unlinkShade(uint8_t shadeId);
    bool hasShadeId(uint8_t shadeId);
    void compressLinkedShadeIds();
    void publish();
    void unpublish();
    static void unpublish(uint8_t id);
    static void unpublish(uint8_t id, const char *topic);
    void publishState();
    void updateFlags();
    void emitState(const char *evt = "groupState");
    void emitState(uint8_t num, const char *evt = "groupState");
    void sendCommand(somfy_commands cmd);
    void sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    int8_t p_direction(int8_t dir);
    bool publish(const char *topic, uint8_t val, bool retain = false);
    bool publish(const char *topic, int8_t val, bool retain = false);
    bool publish(const char *topic, uint32_t val, bool retain = false);
    bool publish(const char *topic, uint16_t val, bool retain = false);
    bool publish(const char *topic, bool val, bool retain = false);
};
struct transceiver_config_t {
    bool printBuffer = false;
    bool enabled = false;
    uint8_t type = 56;                // 56 or 80 bit protocol.
    radio_proto proto = radio_proto::RTS;
    uint8_t SCKPin = 18;
    uint8_t TXPin = 13;
    uint8_t RXPin = 12;
    uint8_t MOSIPin = 23;
    uint8_t MISOPin = 19;
    uint8_t CSNPin = 5;
    bool radioInit = false;
    float frequency = 433.42;         // Basic frequency
    float deviation = 47.60;          // Set the Frequency deviation in kHz. Value from 1.58 to 380.85. Default is 47.60 kHz.
    float rxBandwidth = 99.97;        // Receive bandwidth in kHz.  Value from 58.03 to 812.50.  Default is 99.97kHz.
    int8_t txPower = 10;              // Transmission power {-30, -20, -15, -10, -6, 0, 5, 7, 10, 11, 12}.  Default is 12.
/*    
    bool internalCCMode = false;      // Use internal transmission mode FIFO buffers.
    byte modulationMode = 2;          // Modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
    uint8_t channel = 0;              // The channel number from 0 to 255
    float channelSpacing = 199.95;    // Channel spacing in multiplied by the channel number and added to the base frequency in kHz. 25.39 to 405.45.  Default 199.95
    float dataRate = 99.97;           // The data rate in kBaud.  0.02 to 1621.83 Default is 99.97.
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
 */
    void fromJSON(JsonObject& obj);
    //void toJSON(JsonObject& obj);
    void toJSON(JsonResponse& json);
    void save();
    void load();
    void apply();
    void removeNVSKey(const char *key);
};
class Transceiver {
  private:
    static void handleReceive();
    bool _received = false;
    somfy_frame_t frame;
  public:
    transceiver_config_t config;
    bool printBuffer = false;
    //bool toJSON(JsonObject& obj);
    void toJSON(JsonResponse& json);
    bool fromJSON(JsonObject& obj);
    bool save();
    bool begin();
    void loop();
    bool end();
    bool receive(somfy_rx_t *rx);
    void clearReceived();
    void enableReceive();
    void disableReceive();
    somfy_frame_t& lastFrame();
    void sendFrame(byte *frame, uint8_t sync, uint8_t bitLength = 56);
    void beginTransmit();
    void endTransmit();
    void emitFrame(somfy_frame_t *frame, somfy_rx_t *rx = nullptr);
    void beginFrequencyScan();
    void endFrequencyScan();
    void processFrequencyScan(bool received = false);
    void emitFrequencyScan(uint8_t num = 255);
    bool usesPin(uint8_t pin);
};
class SomfyShadeController {
  protected:
    uint8_t m_shadeIds[SOMFY_MAX_SHADES];
    uint32_t lastCommit = 0;
  public:
    bool useNVS();
    bool isDirty = false;
    uint32_t startingAddress;
    uint8_t getNextRoomId();
    uint8_t getNextShadeId();
    uint8_t getNextGroupId();
    int8_t getMaxRoomOrder();
    int8_t getMaxShadeOrder();
    int8_t getMaxGroupOrder();
    uint32_t getNextRemoteAddress(uint8_t shadeId);
    SomfyShadeController();
    Transceiver transceiver;
    SomfyRoom *addRoom();
    SomfyRoom *addRoom(JsonObject &obj);
    SomfyShade *addShade();
    SomfyShade *addShade(JsonObject &obj);
    SomfyGroup *addGroup();
    SomfyGroup *addGroup(JsonObject &obj);
    bool deleteRoom(uint8_t roomId);
    bool deleteShade(uint8_t shadeId);
    bool deleteGroup(uint8_t groupId);
    bool begin();
    void loop();
    void end();
    void compressRepeaters();
    uint32_t repeaters[SOMFY_MAX_REPEATERS] = {0};
    SomfyRoom rooms[SOMFY_MAX_ROOMS];
    SomfyShade shades[SOMFY_MAX_SHADES];
    SomfyGroup groups[SOMFY_MAX_GROUPS];
    bool linkRepeater(uint32_t address);
    bool unlinkRepeater(uint32_t address);
    void toJSONShades(JsonResponse &json);
    void toJSONRooms(JsonResponse &json);
    void toJSONGroups(JsonResponse &json);
    void toJSONRepeaters(JsonResponse &json);
    uint8_t repeaterCount();
    uint8_t roomCount();
    uint8_t shadeCount();
    uint8_t groupCount();
    void updateGroupFlags();
    SomfyShade * getShadeById(uint8_t shadeId);
    SomfyRoom * getRoomById(uint8_t roomId);
    SomfyGroup * getGroupById(uint8_t groupId);
    SomfyShade * findShadeByRemoteAddress(uint32_t address);
    SomfyGroup * findGroupByRemoteAddress(uint32_t address);
    void sendFrame(somfy_frame_t &frame, uint8_t repeats = 0);
    void processFrame(somfy_frame_t &frame, bool internal = false);
    void emitState(uint8_t num = 255);
    void publish();
    void processWaitingFrame();
    void commit();
    void writeBackup();
    bool loadShadesFile(const char *filename);
    #ifdef USE_NVS
    bool loadLegacy();
    #endif
};

#endif
