#include "esp_log.h"
#include "ShadeBehaviour.h"

static const char *TAG = "Somfy";

#define SETMY_REPEATS 35
#define TILT_REPEATS 15

// How long a shade's simulated movement waits for its command to go on air
// before it starts anyway in case the event was lost.
#define TX_PENDING_TIMEOUT 30000

ShadeBehaviour::ShadeBehaviour(const ShadeSettings &settings, const RemoteSettings &remote, somfy_frame_t &lastFrame,
    MotorLink &link, ShadeEvents &events, const uint8_t &shadeId, const char *name)
  : settings(settings), remote(remote), lastFrame(lastFrame), link(link), events(events), shadeId(shadeId), name(name) {}
void ShadeBehaviour::clear() {
  this->moveStart = 0;
  this->tiltStart = 0;
  this->sensors.clear();
  this->startPos = 0.0f;
  this->startTiltPos = 0.0f;
  this->settingMyPos = false;
  this->settingPos = false;
  this->settingTiltPos = false;
  this->awaitMy = 0;
  this->s.currentPos = 0.0f;
  this->s.currentTiltPos = 0.0f;
  this->s.direction = 0;
  this->s.tiltDirection = 0;
  this->s.target = 0.0f;
  this->s.tiltTarget = 0.0f;
  this->s.myPos = -1.0f;
  this->s.myTiltPos = -1.0f;
}
void ShadeBehaviour::setPosition(float pos, float tilt) {
  // Through the setters, so MQTT hears about it too.
  if(pos >= 0.0f) {
    this->p_target(pos);
    this->p_currentPos(pos);
  }
  if(tilt >= 0.0f) {
    this->p_tiltTarget(tilt);
    this->p_currentTiltPos(tilt);
  }
}
void ShadeBehaviour::restore(float pos, float tiltPos, float myPos, float myTiltPos) {
  this->s.currentPos = pos;
  this->s.currentTiltPos = tiltPos;
  this->s.myPos = myPos;
  this->s.myTiltPos = myTiltPos;
  this->s.target = floor(pos);
  this->s.tiltTarget = floor(tiltPos);
}
void ShadeBehaviour::clearTilt() {
  this->s.myTiltPos = -1;
  this->s.currentTiltPos = 0;
}
somfy_commands ShadeBehaviour::transformCommand(somfy_commands cmd) const { return this->remote.flipCommands ? flipCommand(cmd) : cmd; }
send_result_t ShadeBehaviour::transmit(somfy_commands cmd, uint8_t repeat, uint8_t stepSize, uint16_t delayMs, bool priority, uint32_t now) {
  const send_result_t result = this->link.send(cmd, repeat, stepSize, delayMs, priority);
  if(!result.sent) return result;
  // The link has put the frame in lastFrame and every other shade has heard
  // it.  The shade hears it too, as if a remote had sent it.
  this->onFrame(this->lastFrame, true, now);
  // An idle shade this sets moving waits for the frame to go on air.  Stops
  // are sent ahead of everything else so they don't need to.
  if(result.ticket != 0 && !priority && startsMovement(this->lastFrame.cmd)) this->onTxQueued(result.ticket, now);
  return result;
}
bool ShadeBehaviour::isAtTarget() const { 
  float epsilon = .00001;
  if(this->settings.tiltType == tilt_types::tiltonly) return fabs(this->s.currentTiltPos - this->s.tiltTarget) < epsilon;
  else if(this->settings.tiltType == tilt_types::none) return fabs(this->s.currentPos - this->s.target) < epsilon;
  return fabs(this->s.currentPos - this->s.target) < epsilon && fabs(this->s.currentTiltPos - this->s.tiltTarget) < epsilon; 
}
void ShadeBehaviour::tick(uint32_t now) {
  const uint32_t curTime = now;
  const bool sunFlag = this->s.stateFlags & static_cast<uint8_t>(somfy_flags_t::SunFlag);
  const bool isSunny = this->s.stateFlags & static_cast<uint8_t>(somfy_flags_t::Sunny);
  const bool isWindy = this->s.stateFlags & static_cast<uint8_t>(somfy_flags_t::Windy);
  // We need to first evaluate the sensor flags as these could be triggering movement from previous sensor inputs. So
  // we must check this before setting the directional items or it will not get processed until the next loop.
  int32_t downTime = (int32_t)this->settings.downTime;
  int32_t upTime = (int32_t)this->settings.upTime;
  int32_t tiltTime = (int32_t)this->settings.tiltTime;
  if(this->settings.shadeType == shade_types::drycontact || this->settings.shadeType == shade_types::drycontact2) downTime = upTime = tiltTime = 1;
  

  // We are checking movement for essentially 3 types of motors.
  // If this is an integrated tilt we need to first tilt in the direction we are moving then move.  We know 
  // what needs to be done by the tilt type.  Set a tilt first flag to indicate whether we should be tilting or
  // moving. If this is only a tilt action then the regular tilt action should operate fine.
  int8_t currDir = this->s.direction;
  int8_t currTiltDir = this->s.tiltDirection;
  this->p_direction(this->s.currentPos == this->s.target ? 0 : this->s.currentPos > this->s.target ? -1 : 1);
  bool tilt_first = this->settings.tiltType == tilt_types::integrated && ((this->s.direction == -1 && this->s.currentTiltPos != 0.0f) || (this->s.direction == 1 && this->s.currentTiltPos != 100.0f));

  this->p_tiltDirection(this->s.currentTiltPos == this->s.tiltTarget ? 0 : this->s.currentTiltPos > this->s.tiltTarget ? -1 : 1);
  if(tilt_first) this->p_tiltDirection(this->s.direction);
  else if(this->s.direction != 0) this->p_tiltDirection(0);
  uint8_t currPos = floor(this->s.currentPos);
  uint8_t currTiltPos = floor(this->s.currentTiltPos);
  if(this->s.direction != 0) this->s.lastMovement = this->s.direction;
  switch(this->tx.hold.check(this->s.direction != 0 || this->s.tiltDirection != 0, (uint32_t)curTime, TX_PENDING_TIMEOUT)) {
    case TxHoldCheck::holding:
      // The command that set us moving hasn't gone on air yet so the motor
      // hasn't started.  Hold the simulated position until it does.
      this->moveStart = this->tiltStart = curTime;
      if(currDir != this->s.direction || currTiltDir != this->s.tiltDirection) this->emitState();
      return;
    case TxHoldCheck::expired:
      ESP_LOGW(TAG, "[Shade %u] Timed out waiting for the radio, moving anyway", this->shadeId);
      break;
    case TxHoldCheck::none:
      break;
  }
  this->applySensorMove(this->sensors.check(sunFlag, isSunny, isWindy, (uint32_t)curTime));

  if(!tilt_first && this->s.direction > 0) {
    this->p_currentPos(travelPosition(this->startPos, 1, (uint32_t)(curTime - this->moveStart), downTime));
    if(this->s.currentPos >= this->s.target) {
      this->p_currentPos(this->s.target);
      //if(this->settingMyPos) Serial.printf("IsAtTarget: %d  %f=%f\n", this->isAtTarget(), this->s.currentPos, this->s.target);
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingPos) {
        // At the end limit the motor stops by itself.  If the stop doesn't go
        // out the shade is now running on to the limit so leave the tilt be.
        if(this->s.target == 100.0 || this->sendStop(1, false, now)) {
          if(!isAtTarget()) {
            ESP_LOGD(TAG, "We are not at our tilt target: %.2f", this->s.tiltTarget);
            // We now need to move the tilt to the position we requested.
            this->moveToTiltTarget(this->s.tiltTarget, now);
          }
        }
      }
      this->p_direction(0);
      this->settingPos = false;
      this->tiltStart = curTime;
      this->startTiltPos = this->s.currentTiltPos;
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  else if(!tilt_first && this->s.direction < 0) {
    // Shades often move slower up so this uses its own travel time.
    this->p_currentPos(travelPosition(this->startPos, -1, (uint32_t)(curTime - this->moveStart), upTime));
    if(this->s.currentPos <= this->s.target) {
      this->p_currentPos(this->s.target);
      //if(this->settingMyPos) Serial.printf("IsAtTarget: %d  %f=%f\n", this->isAtTarget(), this->s.currentPos, this->s.target);
      
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingPos) {
        // At the end limit the motor stops by itself.  If the stop doesn't go
        // out the shade is now running on to the limit so leave the tilt be.
        if(this->s.target == 0.0 || this->sendStop(-1, false, now)) {
          if(!isAtTarget()) {
            ESP_LOGD(TAG, "We are not at our tilt target: %.2f", this->s.tiltTarget);
            // We now need to move the tilt to the position we requested.
            this->moveToTiltTarget(this->s.tiltTarget, now);
          }
        }
      }
      this->p_direction(0);
      this->settingPos = false;
      this->tiltStart = curTime;
      this->startTiltPos = this->s.currentTiltPos;
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  if(this->s.tiltDirection > 0) {
    if(tilt_first) this->moveStart = curTime;
    this->p_currentTiltPos(travelPosition(this->startTiltPos, 1, (uint32_t)(curTime - this->tiltStart), tiltTime));
    if(tilt_first) {
      if(this->s.currentTiltPos >= 100.0f) {
        this->p_currentTiltPos(100.0f);
        this->moveStart = curTime;
        this->startPos = this->s.currentPos;
        //this->p_tiltDirection(0);
        //Serial.println("Setting tiltDirection to 0 (tilt_first)");
      }
    }
    else if(this->s.currentTiltPos >= this->s.tiltTarget) {
      this->p_currentTiltPos(this->s.tiltTarget);
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingTiltPos) {
        if(this->settings.tiltType == tilt_types::integrated) {
          // If this is an integrated tilt mechanism the we will simply let it finish.  If it is not then we will stop it.
          //Serial.printf("Sending My -- tiltTarget: %.2f, tiltDirection: %d\n", this->s.tiltTarget, this->s.tiltDirection);
          if(this->s.tiltTarget != 100.0f || this->s.currentPos != 100.0f) this->sendStop(1, true, now);
        }
        else {
          // This is a tilt motor so let it complete if it is going to 100.
          if(this->s.tiltTarget != 100.0f) this->sendStop(1, true, now);
        }
      }
      this->p_tiltDirection(0);
      this->settingTiltPos = false;
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  else if(this->s.tiltDirection < 0) {
    if(tilt_first) this->moveStart = curTime;
    if(tiltTime == 0) this->p_tiltDirection(0);
    this->p_currentTiltPos(travelPosition(this->startTiltPos, -1, (uint32_t)(curTime - this->tiltStart), tiltTime));
    if(tilt_first) {
      if(this->s.currentTiltPos <= 0.0f) {
        this->p_currentTiltPos(0.0f);
        this->moveStart = curTime;
        this->startPos = this->s.currentPos;
        //this->p_tiltDirection(0);
      }
    }
    else if(this->s.currentTiltPos <= this->s.tiltTarget) {
      this->p_currentTiltPos(this->s.tiltTarget);
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingTiltPos) {
        if(this->settings.tiltType == tilt_types::integrated) {
          // If this is an integrated tilt mechanism the we will simply let it finish.  If it is not then we will stop it.
          //Serial.printf("Sending My -- tiltTarget: %.2f, tiltDirection: %d\n", this->s.tiltTarget, this->s.tiltDirection);
          if(this->s.tiltTarget != 0.0 || this->s.currentPos != 0.0) this->sendStop(-1, true, now);
        }
        else {
          // This is a tilt motor so let it complete if it is going to 0.
          if(this->s.tiltTarget != 0.0) this->sendStop(-1, true, now);
        }
      }
      this->p_tiltDirection(0);
      this->settingTiltPos = false;
      ESP_LOGI(TAG, "Stopping at tilt position");
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  if(this->settingMyPos && this->isAtTarget()) {
    // Set this position before sending the command.  If you don't the processFrame function
    // will send the shade back to its original My position.
    const float oldMyPos = this->s.myPos;
    const float oldMyTiltPos = this->s.myTiltPos;
    if(this->settings.tiltType != tilt_types::none) {
      if(this->s.myTiltPos == this->s.currentTiltPos && this->s.myPos == this->s.currentPos) this->s.myPos = this->s.myTiltPos = -1;
      else {
        this->p_myPos(this->s.currentPos);
        this->p_myTiltPos(this->s.currentTiltPos);
      }
    }
    else {
      this->p_myTiltPos(-1);
      if(this->s.myPos == this->s.currentPos) this->p_myPos(-1);
      else this->p_myPos(this->s.currentPos);
    }
    // Give the motor time to settle at the target before it is told to store it.
    this->sendSetMy(200, oldMyPos, oldMyTiltPos, now);
    this->settingMyPos = false;
    this->emitState();
  }
  else if(currDir != this->s.direction || currPos != floor(this->s.currentPos) || currTiltDir != this->s.tiltDirection || currTiltPos != floor(this->s.currentTiltPos)) {
    // We need to emit on the socket that our state has changed.
    this->emitState();
  }
}


// State Setters.  Each marks what clients see as changed when it does change;
// the change fan-out publishes it once per loop() pass.
float ShadeBehaviour::p_currentPos(float pos) {
  float old = this->s.currentPos;
  this->s.currentPos = pos;
  if(floor(old) != floor(pos)) this->events.onChanged(SHADE_CHANGED_POSITION);
  return old;
}
float ShadeBehaviour::p_currentTiltPos(float pos) {
  float old = this->s.currentTiltPos;
  this->s.currentTiltPos = pos;
  if(floor(old) != floor(pos)) this->events.onChanged(SHADE_CHANGED_TILT_POSITION);
  return old;
}
bool ShadeBehaviour::p_flag(somfy_flags_t flag, bool val) {
  bool old = !!(this->s.stateFlags & static_cast<uint8_t>(flag));
  if(val) 
      this->s.stateFlags |= static_cast<uint8_t>(flag);
  else
      this->s.stateFlags &= ~(static_cast<uint8_t>(flag));
  return old;
}
bool ShadeBehaviour::p_sunFlag(bool val) {
  bool old = this->p_flag(somfy_flags_t::SunFlag, val);
  if(old != val) this->events.onChanged(SHADE_CHANGED_SUN_FLAG);
  return old;
}
bool ShadeBehaviour::p_windy(bool val) {
  bool old = this->p_flag(somfy_flags_t::Windy, val);
  if(old != val) this->events.onChanged(SHADE_CHANGED_WINDY);
  return old;
}
bool ShadeBehaviour::p_sunny(bool val) {
  bool old = this->p_flag(somfy_flags_t::Sunny, val);
  if(old != val) this->events.onChanged(SHADE_CHANGED_SUNNY);
  return old;
}
int8_t ShadeBehaviour::p_direction(int8_t dir) {
  int8_t old = this->s.direction;
  if(old != dir) {
    this->s.direction = dir;
    this->events.onChanged(SHADE_CHANGED_DIRECTION);
  }
  return old;
}
int8_t ShadeBehaviour::p_tiltDirection(int8_t dir) {
  int8_t old = this->s.tiltDirection;
  if(old != dir) {
    this->s.tiltDirection = dir;
    this->events.onChanged(SHADE_CHANGED_TILT_DIRECTION);
  }
  return old;
}
float ShadeBehaviour::p_target(float target) {
  float old = this->s.target;
  if(old != target) {
    this->s.target = target;
    if(this->transformPosition(old) != this->transformPosition(target))
      this->events.onChanged(SHADE_CHANGED_TARGET);
  }
  return old;
}
float ShadeBehaviour::p_tiltTarget(float target) {
  float old = this->s.tiltTarget;
  if(old != target) {
    this->s.tiltTarget = target;
    if(this->transformPosition(old) != this->transformPosition(target))
      this->events.onChanged(SHADE_CHANGED_TILT_TARGET);
  }
  return old;
}
float ShadeBehaviour::p_myPos(float pos) {
  float old = this->s.myPos;
  if(old != pos) {
    this->s.myPos = pos;
    if(this->transformPosition(old) != this->transformPosition(pos))
      this->events.onChanged(SHADE_CHANGED_MY_POS);
  }
  return old;
}
float ShadeBehaviour::p_myTiltPos(float pos) {
  float old = this->s.myTiltPos;
  if(old != pos) {
    this->s.myTiltPos = pos;
    if(this->transformPosition(old) != this->transformPosition(pos))
      this->events.onChanged(SHADE_CHANGED_MY_TILT_POS);
  }
  return old;
}
bool ShadeBehaviour::isIdle() const { 
  return this->isAtTarget() && this->s.direction == 0 && this->s.tiltDirection == 0; 
}
void ShadeBehaviour::onWaitingFrame(uint32_t now) {
  if(this->shadeId == 255) {
    this->lastFrame.await = 0; 
    return;
  }
  if(this->lastFrame.processed) return;
  // Wrap-safe: await is now + 500 when the press began.
  if(this->lastFrame.await > 0 && static_cast<int32_t>(now - this->lastFrame.await) > 0) {
    somfy_commands cmd = this->transformCommand(this->lastFrame.cmd);
    switch(cmd) {
      // A step acts as soon as it arrives (onFrame), so it is never waiting here.
      case somfy_commands::Down:
      case somfy_commands::Up:
        if(this->settings.tiltType == tilt_types::tiltmotor) { // Theoretically this should get here unless it does have a tilt motor.
          if(this->lastFrame.repeats >= TILT_REPEATS) {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_tiltTarget(dir > 0 ? 100.0f : 0.0f);
            this->setTiltMovement(dir, now);
            this->lastFrame.processed = true;
            ESP_LOGD(TAG, "%s Processing tilt %s after %d repeats",
                this->name, translateSomfyCommand(this->lastFrame.cmd).c_str(), this->lastFrame.repeats);
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          else {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_target(dir > 0 ? 100 : 0);
            this->setMovement(dir, now);
            this->lastFrame.processed = true;
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          if(this->lastFrame.repeats > TILT_REPEATS + 2) {
            this->lastFrame.processed = true;
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
        }
        else if(this->settings.tiltType == tilt_types::euromode) {
          if(this->lastFrame.repeats >= TILT_REPEATS) {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_target(dir > 0 ? 100.0f : 0.0f);
            this->setMovement(dir, now);
            this->lastFrame.processed = true;
            ESP_LOGD(TAG, "%s Processing %s after %d repeats",
                this->name, translateSomfyCommand(this->lastFrame.cmd).c_str(), this->lastFrame.repeats);
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          else {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_tiltTarget(dir > 0 ? 100 : 0);
            this->setTiltMovement(dir, now);
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
          if(floor(this->s.myPos) == floor(this->s.currentPos)) {
            // We are clearing it.
            this->p_myPos(-1);
            this->p_myTiltPos(-1);
          }
          else {
            this->p_myPos(this->s.currentPos);
            this->p_myTiltPos(this->s.currentTiltPos);
          }
          this->commitMyPosition();
          this->lastFrame.processed = true;
          this->emitState();
        }
        else if(this->isIdle()) {
          if(this->settings.simMy)
            this->moveToMyPosition(now); // Call out like this (instead of move to target) so that we don't get some of the goofy tilt only problems.
          else {
            if(this->s.myPos >= 0.0f && this->s.myPos <= 100.0f) this->p_target(this->s.myPos);
            if(this->s.myTiltPos >= 0.0f && this->s.myTiltPos <= 100.0f) this->p_tiltTarget(this->s.myTiltPos);
            this->pinTiltOnlyLift();
          }
          this->setMovement(0, now);
          this->lastFrame.processed = true;
          this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
        }
        else {
          this->p_target(this->s.currentPos);
          this->p_tiltTarget(this->s.currentTiltPos);
          this->pinTiltOnlyLift();
        }
        if(this->lastFrame.repeats > SETMY_REPEATS + 2) this->lastFrame.processed = true;
        if(this->lastFrame.processed) {
          ESP_LOGD(TAG, "%s Processing MY after %d repeats", this->name, this->lastFrame.repeats);
        }
        break;
      default:
        break;
    }
  }
}
void ShadeBehaviour::onFrame(somfy_frame_t &frame, bool internal, uint32_t now) {
  // The reason why we are processing all frames here is so
  // any linked remotes that may happen to be on the same ESPSomfy RTS
  // device can trigger the appropriate actions.
  if(this->shadeId == 255) return; 
  const uint32_t curTime = now;
  this->lastFrame.copy(frame);
  int8_t dir = 0;
  this->moveStart = this->tiltStart = curTime;
  this->startPos = this->s.currentPos;
  this->startTiltPos = this->s.currentTiltPos;
  // If the command is coming from a remote then we are aborting all these positioning operations.
  if(!internal) this->settingMyPos = this->settingPos = this->settingTiltPos = false;
  somfy_commands cmd = this->transformCommand(frame.cmd);
  // At this point we are not processing the combo buttons
  // will need to see what the shade does when you press both.
  switch(cmd) {
    case somfy_commands::Sensor:
      this->lastFrame.processed = true;
      if(this->settings.shadeType == shade_types::drycontact || this->settings.shadeType == shade_types::drycontact2) return;
      {
        const uint8_t prevFlags = this->s.stateFlags;
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
          this->s.stateFlags |= static_cast<uint8_t>(somfy_flags_t::DemoMode);
        else
          this->s.stateFlags &= ~(static_cast<uint8_t>(somfy_flags_t::DemoMode));
        const bool isSunny = this->s.stateFlags & static_cast<uint8_t>(somfy_flags_t::Sunny);
        const bool isWindy = this->s.stateFlags & static_cast<uint8_t>(somfy_flags_t::Windy);
        this->sensors.onSensorFrame(wasSunny, wasWindy, isSunny, isWindy, (uint32_t)curTime);
        this->emitState();
        this->events.onSensorFlags();
      }
      break;
    case somfy_commands::Prog:
    case somfy_commands::MyUp:
    case somfy_commands::MyDown:
    case somfy_commands::MyUpDown:
    case somfy_commands::UpDown:
      this->lastFrame.processed = true;
      if(this->settings.shadeType == shade_types::drycontact || this->settings.shadeType == shade_types::drycontact2) return;
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
      
    case somfy_commands::Flag:
      this->lastFrame.processed = true;
      if(this->settings.shadeType == shade_types::drycontact || this->settings.shadeType == shade_types::drycontact2) return;
      if(this->lastFrame.rollingCode & 0x8000) return; // Some sensors send bogus frames with a rollingCode >= 32768 that cause them to change the state.
      this->p_sunFlag(false);
      //this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::SunFlag));
      this->events.onSaveWanted();
      this->emitState();
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      this->events.onSensorFlags();
      break;    
    case somfy_commands::SunFlag:
      if(this->settings.shadeType == shade_types::drycontact || this->settings.shadeType == shade_types::drycontact2) return;
      if(this->lastFrame.rollingCode & 0x8000) return; // Some sensors send bogus frames with a rollingCode >= 32768 that cause them to change the state.
      {
        const bool isWindy = this->s.stateFlags & static_cast<uint8_t>(somfy_flags_t::Windy);
        const bool isSunny = this->s.stateFlags & static_cast<uint8_t>(somfy_flags_t::Sunny);
        //this->flags |= static_cast<uint8_t>(somfy_flags_t::SunFlag);
        this->p_sunFlag(true);
        this->applySensorMove(this->sensors.onSunFlag(isSunny, isWindy));
        this->events.onSaveWanted();
        this->emitState();
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        this->events.onSensorFlags();
      }
      break;
    case somfy_commands::Up:
      if(this->settings.shadeType == shade_types::drycontact) {
        this->lastFrame.processed = true;
        return;
      }
      else if(this->settings.shadeType == shade_types::drycontact2) {
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        if(this->s.currentPos != 0.0f) this->p_target(0);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        return;
      }
      if(this->settings.tiltType == tilt_types::tiltmotor || this->settings.tiltType == tilt_types::euromode) {
        // Wait another half second just in case we are potentially processing a tilt.
        if(!internal) this->lastFrame.await = curTime + 500;
        else this->lastFrame.processed = true;
      }
      else {
        // If from a remote we will simply be going up.
        this->pinTiltOnlyLift();
        if(this->settings.tiltType == tilt_types::tiltonly && !internal) this->p_tiltTarget(0.0f);
        else if(!internal) {
          if(this->settings.tiltType != tilt_types::tiltonly) this->p_target(0.0f);
          this->p_tiltTarget(0.0f);
        }
        this->lastFrame.processed = true;
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      }
      break;
    case somfy_commands::Down:
      if(this->settings.shadeType == shade_types::drycontact) {
        this->lastFrame.processed = true;
        return;
      }
      else if(this->settings.shadeType == shade_types::drycontact2) {
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        if(this->s.currentPos != 100.0f) this->p_target(100);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        return;
      }
      if (!this->sensors.windBlocksDown((uint32_t)curTime)) {
        if(this->settings.tiltType == tilt_types::tiltmotor || this->settings.tiltType == tilt_types::euromode) {
          // Wait another half seccond just in case we are potentially processing a tilt.
          if(!internal) this->lastFrame.await = curTime + 500;
          else this->lastFrame.processed = true;
        }
        else {
          this->lastFrame.processed = true;
          this->pinTiltOnlyLift();
          if(!internal) {
            if(this->settings.tiltType != tilt_types::tiltonly) this->p_target(100.0f);
            if(this->settings.tiltType != tilt_types::none) this->p_tiltTarget(100.0f);
          }
        }
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      }
      break;
    case somfy_commands::My:
      if(this->settings.shadeType == shade_types::drycontact2) return;
      if(this->isToggle()) { // This is a one button device
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        if(!this->isIdle()) this->p_target(this->s.currentPos);
        else if(this->s.currentPos == 100.0f) this->p_target(0.0f);
        else if(this->s.currentPos == 0.0f) this->p_target(100.0f);
        else this->p_target(this->s.lastMovement == -1 ? 100 : 0);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        return;
      }
      else if(this->settings.shadeType == shade_types::drycontact) {
        // In this case we need to toggle the contact but we only should do this if
        // this is not a repeat.
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        if(this->s.currentPos == 100.0f) this->p_target(0);
        else if(this->s.currentPos == 0.0f) this->p_target(100);
        else this->p_target(this->s.lastMovement == -1 ? 100 : 0);
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
          ESP_LOGI(TAG, "Moving to My target");
          this->lastFrame.processed = true;
          this->pinTiltOnlyLift();
          if(this->s.myTiltPos >= 0.0f && this->s.myTiltPos <= 100.0f) this->p_tiltTarget(this->s.myTiltPos);
          if(this->s.myPos >= 0.0f && this->s.myPos <= 100.0f && this->settings.tiltType != tilt_types::tiltonly) this->p_target(this->s.myPos);
          this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        }
      }
      else {
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        this->pinTiltOnlyLift();
        if(!internal) {
          if(this->settings.tiltType != tilt_types::tiltonly) this->p_target(this->s.currentPos);
          this->p_tiltTarget(this->s.currentTiltPos);
        }
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      }
      break;
    case somfy_commands::StepUp:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      if(this->settings.shadeType == shade_types::drycontact || this->settings.shadeType == shade_types::drycontact2) return;
      dir = 0;
      // The number of steps the frame asks for.
      if(!this->applyStep(-1, frame.stepSize == 0 ? 1 : frame.stepSize)) return;
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
    case somfy_commands::StepDown:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      if(this->settings.shadeType == shade_types::drycontact || this->settings.shadeType == shade_types::drycontact2) return;
      dir = 1;
      // The number of steps the frame asks for.
      if(!this->applyStep(1, frame.stepSize == 0 ? 1 : frame.stepSize)) return;
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
    case somfy_commands::Toggle:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      if(!this->isIdle()) this->p_target(this->s.currentPos);
      else if(this->s.currentPos == 100.0f) this->p_target(0);
      else if(this->s.currentPos == 0.0f) this->p_target(100);
      else this->p_target(this->s.lastMovement == -1 ? 100 : 0);
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
    case somfy_commands::Stop:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      this->p_target(this->s.currentPos);
      this->p_tiltTarget(this->s.currentTiltPos);      
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
    case somfy_commands::Favorite:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      if(this->settings.simMy) {
        this->moveToMyPosition(now);
      }
      else {
        if(this->s.myTiltPos >= 0.0f && this->s.myTiltPos <= 100.0f) this->p_tiltTarget(this->s.myTiltPos);
        if(this->s.myPos >= 0.0f && this->s.myPos <= 100.0f && this->settings.tiltType != tilt_types::tiltonly) this->p_target(this->s.myPos);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      }
      break;
    default:
      dir = 0;
      break;
  }
  //if(dir == 0 && this->settings.tiltType == tilt_types::tiltmotor && this->s.tiltDirection != 0) this->setTiltMovement(0, now);
  this->setMovement(dir, now);
}
void ShadeBehaviour::onGroupCommand(somfy_commands cmd, uint8_t repeat, uint32_t now) {
  // The reason why we are processing all frames here is so
  // any linked remotes that may happen to be on the same ESPSomfy RTS
  // device can trigger the appropriate actions.
  if(this->shadeId == 255) return; 
  const uint32_t curTime = now;
  int8_t dir = 0;
  this->moveStart = this->tiltStart = curTime;
  this->startPos = this->s.currentPos;
  this->startTiltPos = this->s.currentTiltPos;
  // If the command is coming from a remote then we are aborting all these positioning operations.
  switch(cmd) {
    case somfy_commands::Up:
      if(this->settings.tiltType == tilt_types::tiltmotor) {
        if(repeat >= TILT_REPEATS)
          this->p_tiltTarget(0.0f);
        else
          this->p_target(0.0f);
      }
      else if(this->settings.tiltType == tilt_types::tiltonly) {
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
      if (!this->sensors.windBlocksDown((uint32_t)curTime)) {
        if(this->settings.tiltType == tilt_types::tiltmotor) {
          if(repeat >= TILT_REPEATS)
            this->p_tiltTarget(100.0f);
          else
            this->p_target(100.0f);
        }
        else if(this->settings.tiltType == tilt_types::tiltonly) {
          this->p_target(100.0f);
          this->p_currentPos(100.0f);
          this->p_tiltTarget(100.0f);
        }
        else {
            this->p_target(100.0f);
            if(this->settings.tiltType != tilt_types::none) this->p_tiltTarget(100.0f);
        }
      }
      break;
    case somfy_commands::My:
      if(this->isIdle()) {
        ESP_LOGD(TAG, "Shade #%d is idle", this->shadeId);
        if(this->settings.simMy) {
          this->moveToMyPosition(now);
        }
        else {
          if(this->s.myTiltPos >= 0.0f && this->s.myTiltPos <= 100.0f) this->p_tiltTarget(this->s.myTiltPos);
          if(this->s.myPos >= 0.0f && this->s.myPos <= 100.0f && this->settings.tiltType != tilt_types::tiltonly) this->p_target(this->s.myPos);
          this->pinTiltOnlyLift();
        }
      }
      else {
        if(this->settings.tiltType == tilt_types::tiltonly) this->pinTiltOnlyLift();
        else this->p_target(this->s.currentPos);
        this->p_tiltTarget(this->s.currentTiltPos);
      }
      break;
    case somfy_commands::StepUp:
      // A group step is always one step.
      if(!this->applyStep(-1, 1)) return;
      break;
    case somfy_commands::StepDown:
      dir = 1;
      if(!this->applyStep(1, 1)) return;
      break;
    case somfy_commands::Flag:
      this->p_sunFlag(false);
      // Every group works its flags out again, as for a Flag frame.
      this->events.onSensorFlags();
      if(this->settings.sunSensor) {
        this->events.onSaveWanted();
        this->emitState();
      }
      else {
        ESP_LOGW(TAG, "Shade does not have sensor %d", joinShadeFlags(this->settings, false, this->s.stateFlags));
      }
      break;    
    case somfy_commands::SunFlag:
      if(this->settings.sunSensor) {
        const bool isWindy = this->s.stateFlags & static_cast<uint8_t>(somfy_flags_t::Windy);
        const bool isSunny = this->s.stateFlags & static_cast<uint8_t>(somfy_flags_t::Sunny);
        this->p_sunFlag(true);
        //this->flags |= static_cast<uint8_t>(somfy_flags_t::SunFlag);
        this->applySensorMove(this->sensors.onSunFlag(isSunny, isWindy));
        this->events.onSaveWanted();
        this->emitState();
        this->events.onSensorFlags();
      }
      else
        ESP_LOGW(TAG, "Shade does not have sensor %d", joinShadeFlags(this->settings, false, this->s.stateFlags));
      break;
    default:
      dir = 0;
      break;
  }
  this->setMovement(dir, now);
}
void ShadeBehaviour::setTiltMovement(int8_t dir, uint32_t now) {
  int8_t currDir = this->s.tiltDirection;
  if(dir == 0) {
    // The shade tilt is stopped.
    this->startTiltPos = this->s.currentTiltPos;
    this->tiltStart = 0;
    this->p_tiltDirection(dir);
    if(currDir != dir) {
      this->commitTiltPosition();
    }
  }
  else if(this->s.tiltDirection != dir) {
    this->tiltStart = now;
    this->startTiltPos = this->s.currentTiltPos;
    this->p_tiltDirection(dir);
  }
  if(this->s.tiltDirection != currDir) {
    this->emitState();
  }
}
void ShadeBehaviour::onTxQueued(uint32_t ticket, uint32_t now) {
  // This runs before the caller sets the new target so direction still says
  // whether the shade was moving.  A reversal is simulated as soon as it is
  // queued even though the motor turns once it is on air.  See TxHold::mark
  // for which of several queued commands is held.
  this->tx.hold.mark(ticket, this->s.direction != 0 || this->s.tiltDirection != 0, now);
}
void ShadeBehaviour::onTxEvent(const somfy_tx_event_t &evt) {
  const ShadeTxActions act = this->tx.onEvent(evt, this->s.direction != 0 || this->s.tiltDirection != 0);
  if(act.startMovement) {
    // The motor starts now so the simulated movement does too.
    this->moveStart = this->tiltStart = act.startAt;
  }
  if(act.cancelMovement) {
    // The motor never got the command so it hasn't moved.
    ESP_LOGW(TAG, "[Shade %u] Command dropped by the radio, staying at %.2f%%", this->shadeId, this->s.currentPos);
    this->settingPos = this->settingTiltPos = this->settingMyPos = false;
    this->p_target(this->s.currentPos);
    this->p_tiltTarget(this->s.currentTiltPos);
    this->emitState();
  }
  if(act.commitMy) this->commitMyPosition();
  if(act.restoreMy) {
    ESP_LOGW(TAG, "[Shade %u] Favorite not sent in full, keeping the previous one", this->shadeId);
    this->p_myPos(act.myOldPos);
    this->p_myTiltPos(act.myOldTiltPos);
    this->emitState();
  }
  if(act.runToLimit) {
    ESP_LOGW(TAG, "[Shade %u] Stop dropped by the radio, running on to the limit", this->shadeId);
    this->runToLimit(act.limitDir, act.limitTilt, act.limitSince);
    this->emitState();
  }
}
bool ShadeBehaviour::sendStop(int8_t dir, bool tilt, uint32_t now) {
  // Stops jump the radio queue so the motor stops as close to the target as it can.
  if(const send_result_t sent = this->transmit(somfy_commands::My, this->remote.repeats, 0, 0, true, now)) {
    this->tx.stopSent(sent.ticket, now, dir, tilt);
    return true;
  }
  ESP_LOGW(TAG, "[Shade %u] Stop not sent, running on to the limit", this->shadeId);
  this->runToLimit(dir, tilt, now);
  return false;
}
bool ShadeBehaviour::sendSetMy(uint16_t delayMs, float oldPos, float oldTiltPos, uint32_t now) {
  // The new favorite is already in myPos/myTiltPos.  It is committed once the
  // whole long press is on air, see processTxEvent.
  if(const send_result_t sent = this->transmit(somfy_commands::My, SETMY_REPEATS, 0, delayMs, false, now)) {
    this->tx.setMySent(sent.ticket, oldPos, oldTiltPos);
    // Without the radio (GPIO remotes) there is nothing to wait for.
    if(sent.ticket == 0) this->commitMyPosition();
    return true;
  }
  // The motor never learned it so keep the favorite it has.
  this->p_myPos(oldPos);
  this->p_myTiltPos(oldTiltPos);
  return false;
}
void ShadeBehaviour::pinTiltOnlyLift() {
  if(this->settings.tiltType != tilt_types::tiltonly) return;
  this->p_target(100.0f);
  this->p_currentPos(100.0f);
}
void ShadeBehaviour::runToLimit(int8_t dir, bool tilt, uint32_t since) {
  // The motor didn't get a stop at the target so it keeps going until its end
  // limit.  Carry on simulating that from where and when it should have stopped.
  if(tilt) {
    this->startTiltPos = this->s.currentTiltPos;
    this->tiltStart = since;
    this->settingTiltPos = false;
    this->p_tiltTarget(limitTarget(dir));
  }
  else {
    this->startPos = this->s.currentPos;
    this->moveStart = since;
    this->settingPos = false;
    this->p_target(limitTarget(dir));
  }
  // The favorite can't be set where the shade won't stop.
  this->settingMyPos = false;
}
void ShadeBehaviour::applySensorMove(SensorMove move) {
  const SensorTargets t = sensorTargets(move, this->settings.tiltType, this->s.myPos, this->s.myTiltPos);
  if(t.setLift) this->p_target(t.lift);
  if(t.setTilt) this->p_tiltTarget(t.tilt);
}
bool ShadeBehaviour::applyStep(int8_t dir, uint8_t frameStepSize) {
  const StepTargets t = stepTargets(this->settings.tiltType, dir, this->s.currentPos, this->s.currentTiltPos,
    this->settings.upTime, this->settings.downTime, this->settings.tiltTime, this->settings.stepSize, frameStepSize);
  if(t.action == StepAction::ignore) return false;
  if(t.setLift) this->p_target(t.lift);
  if(t.setTilt) this->p_tiltTarget(t.tilt);
  return true;
}
void ShadeBehaviour::setMovement(int8_t dir, uint32_t now) {
  int8_t currDir = this->s.direction;
  int8_t currTiltDir = this->s.tiltDirection;
  if(dir == 0) {
    if(currDir != dir || currTiltDir != dir) this->commitShadePosition();
  }
  else {
    this->tiltStart = this->moveStart = now;
    this->startPos = this->s.currentPos;
    this->startTiltPos = this->s.currentTiltPos;
  }
  if(this->s.direction != currDir || currTiltDir != this->s.tiltDirection) {
    this->emitState();
  }
}
bool ShadeBehaviour::setMyPosition(int8_t pos, int8_t tilt, uint32_t now) {
  if(!this->isIdle()) return true; // Don't do this if it is moving.
  bool sent = true;
  if(this->settings.tiltType == tilt_types::tiltonly) {
    this->p_myPos(-1.0f);    
    if(tilt != floor(this->s.currentTiltPos)) {
      // The favorite is set once we get there, if the move went out.
      if(tilt == floor(this->s.myTiltPos))
        sent = this->settingMyPos = this->moveToMyPosition(now);
      else
        sent = this->settingMyPos = this->moveToTarget(100, tilt, now);
    }
    else if(tilt == floor(this->s.myTiltPos)) {
      // Of so we need to clear the my position. These motors are finicky so send
      // a my command to ensure we are actually at the my position then send the clear
      // command.  There really is no other way to do this.
      if(this->s.currentTiltPos != this->s.myTiltPos) {
        sent = this->settingMyPos = this->moveToMyPosition(now);
      }
      else if((sent = this->transmit(somfy_commands::My, this->remote.repeats, 0, 0, false, now).sent)) {
        this->settingPos = false;
        this->settingMyPos = true;
      }
    }
    else return this->storeMyPosition(-1.0f, this->s.currentTiltPos, now);
    this->commitMyPosition();
    this->emitState();
  }
  else if(this->settings.tiltType != tilt_types::none) {
      if(tilt < 0) tilt = 0;
      if(pos != floor(this->s.currentPos) || tilt != floor(this->s.currentTiltPos)) {
        // The favorite is set once we get there, if the move went out.
        if(pos == floor(this->s.myPos) && tilt == floor(this->s.myTiltPos))
          sent = this->settingMyPos = this->moveToMyPosition(now);
        else
          sent = this->settingMyPos = this->moveToTarget(pos, tilt, now);
      }
      else if(pos == floor(this->s.myPos) && tilt == floor(this->s.myTiltPos)) {
        // Of so we need to clear the my position. These motors are finicky so send
        // a my command to ensure we are actually at the my position then send the clear
        // command.  There really is no other way to do this.
        if(this->s.currentPos != this->s.myPos || this->s.currentTiltPos != this->s.myTiltPos) {
          sent = this->settingMyPos = this->moveToMyPosition(now);
        }
        else if((sent = this->transmit(somfy_commands::My, this->remote.repeats, 0, 0, false, now).sent)) {
          this->settingPos = false;
          this->settingMyPos = true;
        }
      }
      else return this->storeMyPosition(this->s.currentPos, this->s.currentTiltPos, now);
      this->commitMyPosition();
      this->emitState();
  }
  else {
    if(pos != floor(this->s.currentPos)) {
      // The favorite is set once we get there, if the move went out.
      if(pos == floor(this->s.myPos))
        sent = this->settingMyPos = this->moveToMyPosition(now);
      else
        sent = this->settingMyPos = this->moveToTarget(pos, -1.0f, now);
    }
    else if(pos == floor(this->s.myPos)) {
      // Of so we need to clear the my position. These motors are finicky so send
      // a my command to ensure we are actually at the my position then send the clear
      // command.  There really is no other way to do this.
      if(this->s.myPos != this->s.currentPos) {
        sent = this->settingMyPos = this->moveToMyPosition(now);
      }
      else if((sent = this->transmit(somfy_commands::My, this->remote.repeats, 0, 0, false, now).sent)) {
        this->settingPos = false;
        this->settingMyPos = true;
      }
    }
    else sent = this->storeMyPosition(this->s.currentPos, -1.0f, now);
  }
  return sent;
}
bool ShadeBehaviour::storeMyPosition(float pos, float tiltPos, uint32_t now) {
  // Set it before sending so processFrame doesn't head for the old one.  It is
  // only committed once the motor was told about it.
  const float oldPos = this->s.myPos;
  const float oldTiltPos = this->s.myTiltPos;
  this->p_myPos(pos);
  this->p_myTiltPos(tiltPos);
  const bool sent = this->sendSetMy(0, oldPos, oldTiltPos, now);
  this->emitState();
  return sent;
}
bool ShadeBehaviour::moveToMyPosition(uint32_t now) {
  if(!this->isIdle()) return true;
  ESP_LOGI(TAG, "Moving to My Position");
  if(this->settings.tiltType == tilt_types::tiltonly) {
    this->p_currentPos(100.0f);
    this->p_myPos(-1.0f);
  }
  if(this->s.currentPos == this->s.myPos) {
    if(this->settings.tiltType != tilt_types::none) {
      if(this->s.currentTiltPos == this->s.myTiltPos) return true; // Nothing to see here since we are already here.
    }
    else
      return true;
  }
  if(this->s.myPos == -1 && (this->settings.tiltType == tilt_types::none || this->s.myTiltPos == -1)) return true;
  this->settingPos = false;
  if(this->settings.simMy) {
    ESP_LOGI(TAG, "Moving to simulated favorite");
    if(!this->moveToTarget(this->s.myPos, this->s.myTiltPos, now)) return false;
  }
  else if(!this->transmit(somfy_commands::My, this->remote.repeats, 0, 0, false, now).sent) return false;
  if(this->settings.tiltType != tilt_types::tiltonly && this->s.myPos >= 0.0f && this->s.myPos <= 100.0f) this->p_target(this->s.myPos);
  if(this->s.myTiltPos >= 0.0f && this->s.myTiltPos <= 100.0f) this->p_tiltTarget(this->s.myTiltPos);
  return true;
}
bool ShadeBehaviour::sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize, uint32_t now) {
  ESP_LOGD(TAG, "Send command start");
  // This sendCommand function will always be called externally. sendCommand at the remote level
  // is expected to be called internally when the motor needs commanded.
  // If same direction command sent while already moving, stop the state tracking.
  // The real motor stops on its own when it receives the same direction again.
  if((cmd == somfy_commands::Up && this->s.direction == -1) ||
     (cmd == somfy_commands::Down && this->s.direction == 1)) {
    ESP_LOGD(TAG, "Same command as dir");
    if(!this->transmit(cmd, repeat, 0, 0, false, now).sent) return false;
    this->p_target(this->s.currentPos);
    this->p_tiltTarget(this->s.currentTiltPos);
    this->setMovement(0, now);
    return true;
  }
  // The simulated state is only moved for commands the radio took.
  if(cmd == somfy_commands::Up) {
    if(this->settings.tiltType == tilt_types::euromode) {
      // In euromode we need to long press for 2 seconds on the
      // up command.
      if(!this->transmit(cmd, TILT_REPEATS, 0, 0, false, now).sent) return false;
      this->p_target(0.0f);
    }
    else {
      if(!this->transmit(cmd, repeat, 0, 0, false, now).sent) return false;
      if(this->settings.tiltType == tilt_types::tiltonly) {
        this->p_target(100.0f);
        this->p_tiltTarget(0.0f);
        this->p_currentPos(100.0f);
      }
      else this->p_target(0.0f);
      if(this->settings.tiltType == tilt_types::integrated) this->p_tiltTarget(0.0f);
    }
  }
  else if(cmd == somfy_commands::Down) {
    if(this->settings.tiltType == tilt_types::euromode) {
      // In euromode we need to long press for 2 seconds on the
      // down command.
      if(!this->transmit(cmd, TILT_REPEATS, 0, 0, false, now).sent) return false;
      this->p_target(100.0f);
    }
    else {
      if(!this->transmit(cmd, repeat, 0, 0, false, now).sent) return false;
      if(this->settings.tiltType == tilt_types::tiltonly) {
        this->p_target(100.0f);
        this->p_tiltTarget(100.0f);
        this->p_currentPos(100.0f);
      }
      else this->p_target(100.0f);
      if(this->settings.tiltType == tilt_types::integrated) this->p_tiltTarget(100.0f);
    }
  }
  else if(cmd == somfy_commands::My) {
    if(this->isToggle() || this->settings.shadeType == shade_types::drycontact) {
      if(!this->transmit(cmd, repeat, 0, 0, false, now).sent) return false;
    }
    else if(this->settings.shadeType == shade_types::drycontact2){
        ESP_LOGD(TAG, "Send command start 1");
      return true;
    }
    else if(this->isIdle()) {
      bool sent = this->moveToMyPosition(now);
        ESP_LOGD(TAG, "Send command end 2");
      return sent;
    }
    else {
      if(!this->transmit(cmd, repeat, 0, 0, false, now).sent) return false;
      if(this->settings.tiltType != tilt_types::tiltonly) this->p_target(this->s.currentPos);
      this->p_tiltTarget(this->s.currentTiltPos);
      this->pinTiltOnlyLift();
    }
  }
  else if(cmd == somfy_commands::Toggle) {
    if(this->remote.bitLength != 80) { if(!this->transmit(somfy_commands::My, repeat, stepSize, 0, false, now).sent) return false; }
    else if(!this->transmit(somfy_commands::Toggle, repeat, 0, 0, false, now).sent) return false;
  }
  else if(this->isToggle() && cmd == somfy_commands::Prog) {
    if(!this->transmit(somfy_commands::Toggle, repeat, stepSize, 0, false, now).sent) return false;
  }
  else {
    if(!this->transmit(cmd, repeat, stepSize, 0, false, now).sent) return false;
  }
  ESP_LOGD(TAG, "Send command end");
  return true;
}
uint8_t ShadeBehaviour::tiltRepeats() const { return this->settings.tiltType == tilt_types::tiltmotor ? TILT_REPEATS : this->remote.repeats; }
bool ShadeBehaviour::sendTiltCommand(somfy_commands cmd, uint32_t now) {
  float target;
  if(cmd == somfy_commands::Up) target = 0.0f;
  else if(cmd == somfy_commands::Down) target = 100.0f;
  else if(cmd == somfy_commands::My) target = this->s.currentTiltPos;
  else return true;
  if(!this->transmit(cmd, this->tiltRepeats(), 0, 0, false, now).sent) return false;
  this->p_tiltTarget(target);
  return true;
}
bool ShadeBehaviour::moveToTiltTarget(float target, uint32_t now) {
  somfy_commands cmd = somfy_commands::My;
  if(target < this->s.currentTiltPos)
    cmd = somfy_commands::Up;
  else if(target > this->s.currentTiltPos)
    cmd = somfy_commands::Down;
  if(target >= 0.0f && target <= 100.0f) {
    // Only send a command if the lift is not moving.
    if(this->s.currentPos == this->s.target || this->settings.tiltType == tilt_types::tiltmotor) {
      if(cmd != somfy_commands::My) {
        ESP_LOGI(TAG, "Moving Tilt to %.2f%% from %.2f%% using %s",
            target, this->s.currentTiltPos, translateSomfyCommand(cmd).c_str());
        if(!this->transmit(cmd, this->tiltRepeats(), 0, 0, false, now).sent) return false;
      }
      // If the blind is currently moving then the command to stop it
      // will occur on its own when the tilt target is set.
    }
    this->p_tiltTarget(target);
  }
  if(cmd != somfy_commands::My) this->settingTiltPos = true;
  return true;
}
bool ShadeBehaviour::moveToTarget(float pos, float tilt, uint32_t now) {
  ESP_LOGI(TAG, "[Shade %u] moveToTarget(pos=%.2f, tilt=%.2f) settingPos=%d direction=%d currentTarget=%.2f currentPos=%.2f",
      this->shadeId, pos, tilt, this->settingPos, this->s.direction, this->s.target, this->s.currentPos);
  somfy_commands cmd = somfy_commands::My;
  if(this->isToggle()) {
    // Overload this as we cannot seek a position on a garage door or single button device.
    this->p_target(pos);
    this->p_currentPos(pos);
    this->emitState();
    return true;
  }
  if(this->settings.tiltType == tilt_types::tiltonly) {
    // A tilt only shade is always at 100%.
    this->p_target(100.0f);
    this->p_myPos(-1.0f);
    this->p_currentPos(100.0f);
    pos = 100;
    if(tilt < this->s.currentTiltPos) cmd = somfy_commands::Up;
    else if(tilt > this->s.currentTiltPos) cmd = somfy_commands::Down;
  }
  else {
    if(pos < this->s.currentPos)
      cmd = somfy_commands::Up;
    else if(pos > this->s.currentPos)
      cmd = somfy_commands::Down;
    else if(tilt >= 0 && tilt < this->s.currentTiltPos)
      cmd = somfy_commands::Up;
    else if(tilt >= 0 && tilt > this->s.currentTiltPos)
      cmd = somfy_commands::Down;
  }
  if(cmd != somfy_commands::My) {
    if(tilt >= 0) {
      ESP_LOGI(TAG, "Moving to %.2f%% from %.2f%% tilt %.2f%% from %.2f%% using %s",
          pos, this->s.currentPos, tilt, this->s.currentTiltPos, translateSomfyCommand(cmd).c_str());
    } else {
      ESP_LOGI(TAG, "Moving to %.2f%% from %.2f%% using %s",
          pos, this->s.currentPos, translateSomfyCommand(cmd).c_str());
    }
    if(!this->transmit(cmd, this->settings.tiltType == tilt_types::euromode ? TILT_REPEATS : this->remote.repeats, 0, 0, false, now).sent) return false;
    this->settingPos = true;
    this->p_target(pos);
    if(tilt >= 0) {
      this->p_tiltTarget(tilt);
      this->settingTiltPos = true;
    }
  }
  return true;
}
