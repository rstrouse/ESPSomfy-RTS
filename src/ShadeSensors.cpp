#include "ShadeSensors.h"

// Whether a timer started at start has run timeoutMs by now.  The unsigned
// subtraction keeps working across the millis() rollover.
static bool due(uint32_t start, uint32_t now, uint32_t timeoutMs) {
  return start != 0 && now - start >= timeoutMs;
}
void ShadeSensors::clear() { *this = ShadeSensors(); }
void ShadeSensors::onSensorFrame(bool wasSunny, bool wasWindy, bool sunny, bool windy, uint32_t now) {
  if(sunny) {
    this->noSunStart = 0;
    this->noSunDone = true;
  }
  else {
    this->sunStart = 0;
    this->sunDone = true;
  }
  if(windy) {
    this->noWindStart = 0;
    this->noWindDone = true;
    this->windLast = now;
  }
  else {
    this->windStart = 0;
    this->windDone = true;
  }
  if(sunny && !wasSunny) {
    this->sunStart = now;
    this->sunDone = false;
  }
  else if(!sunny && wasSunny) {
    this->noSunStart = now;
    this->noSunDone = false;
  }
  if(windy && !wasWindy) {
    this->windStart = now;
    this->windDone = false;
  }
  else if(!windy && wasWindy) {
    this->noWindStart = now;
    this->noWindDone = false;
  }
}
SensorMove ShadeSensors::check(bool sunFlag, bool sunny, bool windy, uint32_t now) {
  SensorMove move = SensorMove::none;
  if(sunFlag) {
    if(sunny && !windy) {
      // Sunny and calm, so the shade should be extended.
      if(this->noWindDone && !this->sunDone && due(this->sunStart, now, SOMFY_SUN_TIMEOUT)) {
        this->sunDone = true;
        move = SensorMove::extend;
      }
      if(!this->noWindDone && due(this->noWindStart, now, SOMFY_NO_WIND_TIMEOUT)) {
        this->noWindDone = true;
        move = SensorMove::extend;
      }
    }
    if(!sunny && !this->noSunDone && due(this->noSunStart, now, SOMFY_NO_SUN_TIMEOUT)) {
      this->noSunDone = true;
      move = SensorMove::retract;
    }
  }
  if(windy && !this->windDone && due(this->windStart, now, SOMFY_WIND_TIMEOUT)) {
    this->windDone = true;
    move = SensorMove::retract;
  }
  return move;
}
SensorMove ShadeSensors::onSunFlag(bool sunny, bool windy) const {
  if(windy) return SensorMove::none;
  if(sunny && this->sunDone) return SensorMove::extend;
  if(!sunny && this->noSunDone) return SensorMove::retract;
  return SensorMove::none;
}
SensorTargets sensorTargets(SensorMove move, tilt_types tilt, float myPos, float myTiltPos) {
  SensorTargets t;
  if(move == SensorMove::none) return t;
  const bool extend = move == SensorMove::extend;
  if(tilt == tilt_types::tiltonly) {
    t.setTilt = true;
    t.tilt = extend ? (myTiltPos >= 0 ? myTiltPos : 100.0f) : 0.0f;
  }
  else {
    t.setLift = true;
    t.lift = extend ? (myPos >= 0 ? myPos : 100.0f) : 0.0f;
  }
  return t;
}
bool ShadeSensors::windBlocksDown(uint32_t now) const {
  return this->windLast != 0 && now - this->windLast < SOMFY_NO_WIND_REMOTE_TIMEOUT;
}
