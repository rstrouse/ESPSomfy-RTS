#ifndef SOMFY_H
#define SOMFY_H
#include <Preferences.h>
#include "ConfigSettings.h"

#include "SomfyFrame.h"
#include "TxBuffer.h"
#include "RadioScheduler.h"
#include "ShadeTypes.h"
#include "ShadeSettings.h"
#include "ShadeBehaviour.h"
#include "ShadeMotion.h"
#include "ShadeSensors.h"
#include "ShadeChanges.h"
#include "Transceiver.h"

enum class gpio_flags_t : byte {
  LowLevelTrigger = 0x01
};
struct somfy_relay_t {
  uint32_t remoteAddress = 0;
  uint8_t sync = 0;
  byte frame[10] = {0};
};

class SomfyRoom {
  public:
    uint8_t roomId = 0;
    char name[21] = "";
    int8_t sortOrder = 0;
    void clear();
    bool save();
    bool fromJSON(JsonObject &obj);
    void publish();
    void unpublish();
};

class SomfyRemote : public RemoteSettings {
  // These sizes for the data have been
  // confirmed.  The address is actually 24bits
  // and the rolling code is 16 bits.
  protected:
    char m_remotePrefId[11] = "";
    uint32_t m_remoteAddress = 0;
    // lastRollingCode is known to match NVS for this address, so the next code
    // can be worked out without opening NVS.
    bool m_rollingCodeSynced = false;
  public:
    radio_proto proto = radio_proto::RTS;
    uint8_t gpioFlags = 0;
    int8_t gpioDir = 0;
    uint8_t gpioUp = 0;
    uint8_t gpioDown = 0;
    uint8_t gpioMy = 0;
    uint32_t gpioRelease = 0;
    somfy_frame_t lastFrame;
    uint16_t lastRollingCode = 0;
    virtual bool isLastCommand(somfy_commands cmd);
    char *getRemotePrefId() {return m_remotePrefId;}
    virtual void setRemoteAddress(uint32_t address);
    virtual uint32_t getRemoteAddress();
    uint16_t peekNextRollingCode();
    void commitRollingCode(uint16_t code);
    virtual uint16_t setRollingCode(uint16_t code);
    // The flags byte (somfy_flags_t) as it is saved and sent.
    virtual uint8_t getFlags() { return 0; }
    // The send functions return false only when the radio could not take the
    // command.  Nothing about the remote or shade state has changed in that case.
    virtual bool sendCommand(somfy_commands cmd);
    virtual bool sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    // hearsItself leaves this remote out of the echo, for a shade whose
    // ShadeBehaviour hears its own frame.
    send_result_t sendCommandAfter(somfy_commands cmd, uint8_t repeat, uint8_t stepSize, uint16_t delayMs, bool priority = false, bool hearsItself = false);
    bool sendSensorCommand(int8_t isWindy, int8_t isSunny, uint8_t repeat);
    bool repeatFrame(uint8_t repeat);
    virtual uint16_t p_lastRollingCode(uint16_t code);
    somfy_commands transformCommand(somfy_commands cmd);
    virtual void triggerGPIOs(somfy_frame_t &frame);
   
};
class SomfyLinkedRemote : public SomfyRemote {
  public:
    SomfyLinkedRemote();    
};
// A shade: its identity, settings, remotes and outputs.  What it does with
// frames and commands is its ShadeBehaviour; the shade sends for it
// (MotorLink) and passes on what it reports (ShadeEvents).
class SomfyShade : public SomfyRemote, public ShadeSettings, private MotorLink, private ShadeEvents {
  protected:
    uint8_t shadeId = 255;
  public:
    uint8_t roomId = 0;
    int8_t sortOrder = 0;
    bool light = false;
    SomfyLinkedRemote linkedRemotes[SOMFY_MAX_LINKED_REMOTES];
    bool paired = false;
    int8_t validateJSON(JsonObject &obj);
    int8_t fromJSON(JsonObject &obj);
    
    char name[21] = "";
    void setShadeId(uint8_t id) { shadeId = id; }
    uint8_t getShadeId() { return shadeId; }
    // Where the shade is and where it is going.
    const ShadeBehaviour::State &state() const { return this->behaviour.state(); }
    bool isIdle() const { return this->behaviour.isIdle(); }
    uint8_t getFlags() override;
    // Sets the setting and state bits from a saved or sent flags byte.
    void setFlags(uint8_t flags);
    bool save();
    bool isInGroup();
    void checkMovement();
    void processFrame(somfy_frame_t &frame, bool internal = false);
    // The shade itself or the linked remote with this address, else nullptr.
    SomfyRemote *findRemote(uint32_t address);
    void processInternalCommand(somfy_commands cmd, uint8_t repeat = 1);
    void markTxPending(uint32_t ticket);
    void processTxEvent(const somfy_tx_event_t &evt);
    bool isToggle() { return isToggleShade(this->shadeType); }
    bool moveToTarget(float pos, float tilt = -1.0f);
    // These return false when the radio refused a command they sent.
    bool moveToTiltTarget(float target);
    bool sendTiltCommand(somfy_commands cmd);
    bool sendCommand(somfy_commands cmd);
    bool sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    // Puts the shade at pos and tilt (-1 leaves one alone) without moving it.
    void setPosition(float pos, float tilt);
    // Where the config file says the shade was.
    void restore(float pos, float tiltPos, float myPos, float myTiltPos) { this->behaviour.restore(pos, tiltPos, myPos, myTiltPos); }
    // Forgets the tilt of a shade that has none.
    void clearTilt() { this->behaviour.clearTilt(); }
    bool linkRemote(uint32_t remoteAddress, uint16_t rollingCode = 0);
    bool unlinkRemote(uint32_t remoteAddress);
    // Marks the shade as changed; the change fan-out sends shadeState once per
    // loop() pass.
    void emitState();
    // What changed since the fan-out last sent the shade (SHADE_CHANGED_* bits).
    uint16_t changed = 0;
    // A setting changed: protocol 2 clients get the whole shade.
    void configChanged() { this->changed |= SHADE_CHANGED_CONFIG; }
    void emitCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress);
    // False when the radio refused a command it sent.
    bool setMyPosition(int8_t pos, int8_t tilt = -1);
    bool moveToMyPosition();
    void processWaitingFrame();
    void publish();
    void unpublish();
    static void unpublish(uint8_t id);
    static void unpublish(uint8_t id, const char *topic);
    void publishState();
    // Publishes the MQTT topics of the SHADE_CHANGED_* bits in changed.
    void publishTopics(uint16_t changed);
    void commit();
    void clear();
    int8_t transformPosition(float fpos);
    void setGPIOs();
    void triggerGPIOs(somfy_frame_t &frame);
    bool usesPin(uint8_t pin);
    uint16_t p_lastRollingCode(uint16_t code);
    bool publish(const char *topic, const char *val, bool retain = false);
    bool publish(const char *topic, uint8_t val, bool retain = false);
    bool publish(const char *topic, int8_t val, bool retain = false);
    bool publish(const char *topic, uint32_t val, bool retain = false);
    bool publish(const char *topic, uint16_t val, bool retain = false);
    bool publish(const char *topic, bool val, bool retain = false);
    void publishDisco();
    void unpublishDisco();
  private:
    // MotorLink: the radio or GPIO path every remote sends through.
    send_result_t send(somfy_commands cmd, uint8_t repeat, uint8_t stepSize, uint16_t delayMs, bool priority) override;
    // ShadeEvents
    void onChanged(uint16_t bits) override { this->changed |= bits; }
    void onCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress) override { this->emitCommand(cmd, source, sourceAddress); }
    void onSaveWanted() override;
    void onSensorFlags() override;
    ShadeBehaviour behaviour{*this, *this, this->lastFrame, *this, *this, this->shadeId, this->name};
};
class SomfyGroup : public SomfyRemote {
  protected:
    uint8_t groupId = 255;
  public:
    uint8_t roomId = 0;
    int8_t sortOrder = 0;
    group_types groupType = group_types::channel;
    int8_t direction = 0; // 0 = stopped, 1=down, -1=up.
    // Its linked shades' flags together, see updateFlags.
    uint8_t flags = 0;
    uint8_t getFlags() override { return this->flags; }
    bool hasSunSensor() { return (this->flags & static_cast<uint8_t>(somfy_flags_t::SunSensor)) > 0; }
    char name[21] = "";
    uint8_t linkedShades[SOMFY_MAX_GROUPED_SHADES];
    void setGroupId(uint8_t id) { groupId = id; }
    uint8_t getGroupId() { return groupId; }
    bool save();
    void clear();
    bool fromJSON(JsonObject &obj);
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
    // Marks the group as changed; the change fan-out sends groupState once per
    // loop() pass.
    void emitState();
    // What changed since the fan-out last sent the group (GROUP_CHANGED_* bits).
    uint8_t changed = 0;
    // A setting or its shades changed: protocol 2 clients get the whole group.
    void configChanged() { this->changed |= GROUP_CHANGED_CONFIG; }
    bool sendCommand(somfy_commands cmd);
    bool sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    int8_t p_direction(int8_t dir);
    bool publish(const char *topic, uint8_t val, bool retain = false);
    bool publish(const char *topic, int8_t val, bool retain = false);
    bool publish(const char *topic, uint32_t val, bool retain = false);
    bool publish(const char *topic, uint16_t val, bool retain = false);
    bool publish(const char *topic, bool val, bool retain = false);
};
class SomfyShadeController {
  protected:
    uint8_t m_shadeIds[SOMFY_MAX_SHADES];
    uint32_t lastCommit = 0;
  public:
    bool isDirty = false;
    uint32_t startingAddress;
    uint8_t getNextRoomId();
    uint8_t getNextShadeId();
    uint8_t getNextGroupId();
    // The remote address a new shade or group with that id is offered: one no
    // shade or group uses.
    uint32_t getNextRemoteAddress(uint8_t id);
    int8_t getMaxRoomOrder();
    int8_t getMaxShadeOrder();
    int8_t getMaxGroupOrder();
    SomfyShadeController();
    Transceiver transceiver;
    SomfyRoom *addRoom();
    SomfyShade *addShade();
    SomfyGroup *addGroup();
    bool deleteRoom(uint8_t roomId);
    bool deleteShade(uint8_t shadeId);
    bool deleteGroup(uint8_t groupId);
    bool begin();
    void loop();
    void end();
    uint32_t repeaters[SOMFY_MAX_REPEATERS] = {0};
    SomfyRoom rooms[SOMFY_MAX_ROOMS];
    SomfyShade shades[SOMFY_MAX_SHADES];
    SomfyGroup groups[SOMFY_MAX_GROUPS];
    bool linkRepeater(uint32_t address);
    bool unlinkRepeater(uint32_t address);
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
    // Returns the radio job's ticket, 0 when the radio can't take it.
    uint32_t sendFrame(somfy_frame_t &frame, uint8_t repeats = 0, uint16_t delayMs = 0, bool priority = false);
    // skip, when set, is left out: a shade that hears its own frame.
    void processFrame(somfy_frame_t &frame, bool internal = false, const SomfyRemote *skip = nullptr);
    void markTxPending(uint32_t remoteAddress, uint32_t ticket, const SomfyRemote *skip = nullptr);
    void processTxEvent(const somfy_tx_event_t &evt);
    // The change fan-out: sends every shade and group that changed since the
    // last pass, its changed MQTT topics and one state event.  loop() only.
    void sendChanges();
    void publish();
    void processWaitingFrame();
    void commit();
    void writeBackup();
    String backupData;
};

#endif
