#ifndef SHADE_BEHAVIOUR_H
#define SHADE_BEHAVIOUR_H
#include <Arduino.h>
#include "SomfyFrame.h"
#include "ShadeSettings.h"
#include "ShadeMotion.h"
#include "ShadeSensors.h"
#include "ShadeChanges.h"

// What a shade does with frames, commands and radio events, and where that
// leaves it.  Kept free of the shade model, radio and network so it can be
// unit tested on the PC (pio test -e native).  Only loop() calls it.

// What sending a command did.  ticket is the radio job carrying it, 0 when the
// command went out over GPIO instead.
struct send_result_t {
  bool sent = false;
  uint32_t ticket = 0;
  explicit operator bool() const { return this->sent; }
};

// How a shade's commands reach its motor, over the radio or GPIO.  When it
// sends, the frame as sent is in the shade's lastFrame and every other shade
// has heard it; the shade hears its own frame itself.
class MotorLink {
  public:
    // sent is false when the radio could not take the command.
    virtual send_result_t send(somfy_commands cmd, uint8_t repeat, uint8_t stepSize, uint16_t delayMs, bool priority) = 0;
};

// What a shade's behaviour tells the rest of the firmware, as it happens.
class ShadeEvents {
  public:
    // SHADE_CHANGED_* bits for the change fan-out.
    virtual void onChanged(uint16_t bits) = 0;
    // A command reached the shade.  source is "remote", "internal" and so on.
    virtual void onCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress) = 0;
    // A position, the favorite or the sun flag changed and is worth saving.
    virtual void onSaveWanted() = 0;
    // A sensor, sun flag or flag frame arrived, so the groups work their
    // flags out again.
    virtual void onSensorFlags() = 0;
};

class ShadeBehaviour {
  public:
    // Where the shade is and where it is going.  Positions run 0-100, the
    // favorite is -1 when there is none.
    struct State {
      float currentPos = 0.0f;
      float currentTiltPos = 0.0f;
      float target = 0.0f;
      float tiltTarget = 0.0f;
      int8_t direction = 0;      // 0 = stopped, 1 = down, -1 = up.
      int8_t tiltDirection = 0;  // 0 = stopped, 1 = clockwise, -1 = counter clockwise.
      int8_t lastMovement = 0;
      float myPos = -1.0f;
      float myTiltPos = -1.0f;
      // The flags bits that are state: sun flag, sunny, windy, demo mode and
      // the unused lighted.  The setting bits are in ShadeSettings.
      uint8_t stateFlags = 0;
    };
    // lastFrame is the shade's last frame, shared with the send path that
    // builds the next one from it.  shadeId and name are for the log.
    ShadeBehaviour(const ShadeSettings &settings, const RemoteSettings &remote, somfy_frame_t &lastFrame,
        MotorLink &link, ShadeEvents &events, const uint8_t &shadeId, const char *name);
    ShadeBehaviour(const ShadeBehaviour &) = delete;
    ShadeBehaviour &operator=(const ShadeBehaviour &) = delete;
    const State &state() const { return this->s; }
    bool isIdle() const;
    bool isAtTarget() const;

    // A frame from a remote, sensor or linked remote, or one the shade or
    // another shade sent (internal).
    void onFrame(somfy_frame_t &frame, bool internal, uint32_t now);
    // A remote button may still be held: acts on lastFrame once it is clear
    // whether the press was short or long.
    void onWaitingFrame(uint32_t now);
    // A group the shade belongs to sent cmd.
    void onGroupCommand(somfy_commands cmd, uint8_t repeat, uint32_t now);
    // Every loop() pass: moves the simulated position, stops at the target and
    // stores the favorite.
    void tick(uint32_t now);
    // A command that may set the shade moving was queued as radio job ticket.
    void onTxQueued(uint32_t ticket, uint32_t now);
    // A radio job started, finished, was aborted or dropped.
    void onTxEvent(const somfy_tx_event_t &evt);

    // Commands from the web, MQTT and Home Assistant.  False when the radio
    // refused a command they sent.
    bool sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize, uint32_t now);
    bool sendTiltCommand(somfy_commands cmd, uint32_t now);
    bool moveToTarget(float pos, float tilt, uint32_t now);
    bool moveToTiltTarget(float target, uint32_t now);
    bool setMyPosition(int8_t pos, int8_t tilt, uint32_t now);
    bool moveToMyPosition(uint32_t now);

    // Puts the shade at pos and tilt (-1 leaves one alone) without moving it.
    void setPosition(float pos, float tilt);
    // Where the config file says the shade was.  The targets are where it is.
    void restore(float pos, float tiltPos, float myPos, float myTiltPos);
    // Forgets the tilt of a shade that has none.
    void clearTilt();
    void setStateFlags(uint8_t flags) { this->s.stateFlags = flags; }
    // Back to a stopped shade at 0 with no favorite, for an empty slot.
    void clear();

  private:
    const ShadeSettings &settings;
    const RemoteSettings &remote;
    somfy_frame_t &lastFrame;
    MotorLink &link;
    ShadeEvents &events;
    const uint8_t &shadeId;
    const char *name;
    State s;
    uint32_t moveStart = 0;
    uint32_t tiltStart = 0;
    ShadeSensors sensors;
    float startPos = 0.0f;
    float startTiltPos = 0.0f;
    bool settingMyPos = false;
    bool settingPos = false;
    bool settingTiltPos = false;
    uint32_t awaitMy = 0;
    // Radio jobs whose outcome decides what the motor really did.
    ShadeTxState tx;

    // Sends through the link, then hears the frame as every other shade has
    // and waits for it to go on air if it sets the shade moving.
    send_result_t transmit(somfy_commands cmd, uint8_t repeat, uint8_t stepSize, uint16_t delayMs, bool priority, uint32_t now);
    bool sendStop(int8_t dir, bool tilt, uint32_t now);
    bool sendSetMy(uint16_t delayMs, float oldPos, float oldTiltPos, uint32_t now);
    // Makes pos/tiltPos the favorite and tells the motor to store it.
    bool storeMyPosition(float pos, float tiltPos, uint32_t now);
    // Repeats for a tilt command, more for a separate tilt motor.
    uint8_t tiltRepeats() const;
    // A tilt-only shade has no lift to move: it stays all the way down.
    void pinTiltOnlyLift();
    void runToLimit(int8_t dir, bool tilt, uint32_t since);
    // Sets the targets the sun or wind sends the shade to.
    void applySensorMove(SensorMove move);
    // Sets the targets for a step in dir.  False when the step is dropped.
    bool applyStep(int8_t dir, uint8_t frameStepSize);
    void setTiltMovement(int8_t dir, uint32_t now);
    void setMovement(int8_t dir, uint32_t now);
    bool isToggle() const { return isToggleShade(this->settings.shadeType); }
    somfy_commands transformCommand(somfy_commands cmd) const;
    int8_t transformPosition(float fpos) const { return displayPosition(fpos, this->settings.flipPosition); }
    void emitState() { this->events.onChanged(SHADE_CHANGED_STATE); }
    void emitCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress) { this->events.onCommand(cmd, source, sourceAddress); }
    void commitShadePosition() { this->events.onSaveWanted(); }
    void commitTiltPosition() { this->events.onSaveWanted(); }
    void commitMyPosition() { this->events.onSaveWanted(); }
    // State setters.  Each marks what clients see as changed when it does change.
    float p_currentPos(float pos);
    float p_currentTiltPos(float pos);
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
};
#endif
