#ifndef SHADE_MOTION_H
#define SHADE_MOTION_H
#include <Arduino.h>
#include "TxBuffer.h"
#include "ShadeTypes.h"

// Shade position maths.  Kept free of the shade state, radio and network so it
// can be unit tested on the PC (pio test -e native).

// Where a shade (or its tilt) is after travelling elapsedMs from startPos
// (0-100).  dir > 0 moves toward 100 and dir < 0 toward 0.  travelMs is the
// time a full travel takes in that direction, 0 meaning it gets there at once.
float travelPosition(float startPos, int8_t dir, uint32_t elapsedMs, int32_t travelMs);
// A position as reported to clients: -1 when unset, otherwise the whole
// percentage, counted from the other end when flipped.
int8_t displayPosition(float pos, bool flip);
// The tilt a request to set the favorite asks for, as a display position: the
// requested tilt, or the favorite tilt when the request leaves it out.  -1 for
// a shade without tilt or with no favorite tilt.
int8_t requestedFavoriteTilt(int8_t requestTilt, tilt_types tilt, float myTiltPos, bool flip);
// Where a shade (or its tilt) moving in dir ends up when nothing stops it.
float limitTarget(int8_t dir);
// Where a step command moves a shade (or its tilt) from pos, in out.  dir > 0
// steps toward 100 and dir < 0 toward 0.  A step is stepSize * frameStepSize
// ms of the travelMs a full travel takes in that direction.  False when
// travelMs or stepSize is 0, so the step is ignored.
bool stepPosition(float pos, int8_t dir, uint32_t travelMs, uint16_t stepSize, uint8_t frameStepSize, float &out);
// What a step command does to a shade.
enum class StepAction : uint8_t {
  ignore,  // The step can't be sized or the shade is at both ends: drop it.
  none,    // A lift shade already at the end: nothing moves.
  move     // Set the targets below.
};
struct StepTargets {
  StepAction action = StepAction::ignore;
  bool setLift = false;
  float lift = 0.0f;
  bool setTilt = false;
  float tilt = 0.0f;
};
// Where a step in dir (> 0 down, < 0 up) takes a shade at pos and tiltPos.  A
// tilt-only shade steps its tilt.  An integrated tilt turns its slats to the
// end first, holding the lift, and then steps the lift.  Every other shade
// steps its lift.
StepTargets stepTargets(tilt_types tilt, int8_t dir, float pos, float tiltPos,
    uint32_t upTime, uint32_t downTime, uint32_t tiltTime, uint16_t stepSize, uint8_t frameStepSize);

// What a held shade does on this pass of checkMovement().
enum class TxHoldCheck {
  none,     // Nothing is held.
  holding,  // Still waiting for the job to go on air.
  expired   // Waited too long, so the hold was dropped and the shade moves anyway.
};
// The radio job that set an idle shade moving.  Its simulated movement is held
// until the job goes on air, since that is when the motor starts.
struct TxHold {
  uint32_t ticket = 0; // 0 when nothing is held.
  uint32_t since = 0;  // millis() when the hold started.
  // Holds for job ticket unless the shade was already moving (its motor carries on
  // with the earlier command) or a job is already held.  The first job held is
  // the one that starts the motor.  A command sent while processing another
  // registers before it: for a simMy shade My/Favorite queues its Up/Down from
  // moveToMyPosition() first, and that Up/Down is what starts a motor with no
  // favorite of its own, so the outer My/Favorite is ignored on purpose.
  void mark(uint32_t ticket, bool moving, uint32_t now);
  // Ends the hold when ticket is the held job.  True when it was.
  bool release(uint32_t ticket);
  bool held() const { return this->ticket != 0; }
  bool timedOut(uint32_t now, uint32_t timeoutMs) const { return now - this->since >= timeoutMs; }
  void clear() { this->ticket = 0; }
  // Drops a hold on a shade the command didn't set moving, or one that has
  // waited timeoutMs for the radio.
  TxHoldCheck check(bool moving, uint32_t now, uint32_t timeoutMs);
};
// What a shade has to do about a radio event.  More than one can apply to the
// same event.
struct ShadeTxActions {
  // The held job went on air, so the motor starts at startAt.
  bool startMovement = false;
  uint32_t startAt = 0;
  // The held job never went on air, so the motor hasn't moved.
  bool cancelMovement = false;
  // The favorite went on air in full and is committed, or it didn't and the
  // previous one is put back.
  bool commitMy = false;
  bool restoreMy = false;
  float myOldPos = -1.0f;
  float myOldTiltPos = -1.0f;
  // The stop never went on air, so the motor runs on to its end limit from
  // where and when it should have stopped.
  bool runToLimit = false;
  int8_t limitDir = 0;
  bool limitTilt = false;
  uint32_t limitSince = 0;
  bool any() const { return startMovement || cancelMovement || commitMy || restoreMy || runToLimit; }
};
// The radio jobs a shade waits on to find out what its motor really did.
struct ShadeTxState {
  // Radio job that set the shade moving.  The simulated position is held
  // until it goes on air.
  TxHold hold;
  // Stop sent when the simulated target was reached.  If it never goes on
  // air the motor runs on to its end limit.  0 once an event for it arrived.
  uint32_t stopTicket = 0;
  uint32_t stopAt = 0;
  int8_t stopDir = 0;
  bool stopTilt = false;
  // Command telling the motor to store the favorite.  The favorite is only
  // committed once all of it is on air and restored if it never is.
  uint32_t myTicket = 0;
  float myOldPos = -1.0f;
  float myOldTiltPos = -1.0f;
  void stopSent(uint32_t ticket, uint32_t now, int8_t dir, bool tilt);
  void setMySent(uint32_t ticket, float oldPos, float oldTiltPos);
  // What to do about evt.  moving is whether the shade is moving now.
  ShadeTxActions onEvent(const somfy_tx_event_t &evt, bool moving);
};
#endif
