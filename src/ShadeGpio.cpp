#include "esp_log.h"
#include "Somfy.h"

static const char *TAG = "Somfy";

void SomfyShade::setGPIOs() {
  if(this->proto == radio_proto::GP_Relay) {
    // Determine whether the direction needs to be set.
    uint8_t p_on = (this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0x00 ? HIGH : LOW;
    uint8_t p_off = (this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0x00 ? LOW : HIGH;
    
    int8_t dir = this->state().direction;
    if(dir == 0 && this->tiltType == tilt_types::integrated)
      dir = this->state().tiltDirection;
    else if(this->tiltType == tilt_types::tiltonly)
      dir = this->state().tiltDirection;
    if(this->shadeType == shade_types::drycontact) {
      digitalWrite(this->gpioDown, this->state().currentPos == 100 ? p_on : p_off);
      this->gpioDir = this->state().currentPos == 100 ? 1 : -1;
    }
    else if(this->shadeType == shade_types::drycontact2) {
      if(this->state().currentPos == 100) {
        digitalWrite(this->gpioDown, p_off);
        digitalWrite(this->gpioUp, p_on);
      }
      else {
        digitalWrite(this->gpioUp, p_off);
        digitalWrite(this->gpioDown, p_on);
      }
      this->gpioDir = this->state().currentPos == 100 ? 1 : -1;
    }
    else {
      switch(dir) {
        case -1:
          digitalWrite(this->gpioDown, p_off);
          digitalWrite(this->gpioUp, p_on);
          if(dir != this->gpioDir) ESP_LOGD(TAG, "UP: true, DOWN: false");
          this->gpioDir = dir;
          break;
        case 1:
          digitalWrite(this->gpioUp, p_off);
          digitalWrite(this->gpioDown, p_on);
          if(dir != this->gpioDir) ESP_LOGD(TAG, "UP: false, DOWN: true");
          this->gpioDir = dir;
          break;
        default:
          digitalWrite(this->gpioUp, p_off);
          digitalWrite(this->gpioDown, p_off);
          if(dir != this->gpioDir) ESP_LOGD(TAG, "UP: false, DOWN: false");
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
          if(dir != this->gpioDir) ESP_LOGD(TAG, "UP: false, DOWN: false, MY: true");
        }
        break;
      case somfy_commands::Up:
        if(this->shadeType != shade_types::drycontact && !this->isToggle() && this->shadeType != shade_types::drycontact2) {
          digitalWrite(this->gpioMy, p_off);
          digitalWrite(this->gpioDown, p_off);
          digitalWrite(this->gpioUp, p_on);
          dir = -1;
          ESP_LOGD(TAG, "UP: true, DOWN: false, MY: false");
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
        ESP_LOGD(TAG, "UP: false, DOWN: true, MY: false");
        break;
      case somfy_commands::MyUp:
        if(this->shadeType != shade_types::drycontact && !this->isToggle() && this->shadeType != shade_types::drycontact2) {
          digitalWrite(this->gpioDown, p_off);
          digitalWrite(this->gpioMy, p_on);
          digitalWrite(this->gpioUp, p_on);
          ESP_LOGD(TAG, "UP: true, DOWN: false, MY: true");
        }
        break;
      case somfy_commands::MyDown:
        if(this->shadeType != shade_types::drycontact && !this->isToggle() && this->shadeType != shade_types::drycontact2) {
          digitalWrite(this->gpioUp, p_off);
          digitalWrite(this->gpioMy, p_on);
          digitalWrite(this->gpioDown, p_on);
          ESP_LOGD(TAG, "UP: false, DOWN: true, MY: true");
        }
        break;
      case somfy_commands::MyUpDown:
        if(this->shadeType != shade_types::drycontact && this->isToggle() && this->shadeType != shade_types::drycontact2) {
          digitalWrite(this->gpioUp, p_on);
          digitalWrite(this->gpioMy, p_on);
          digitalWrite(this->gpioDown, p_on);
          ESP_LOGD(TAG, "UP: true, DOWN: true, MY: true");
        }
        break;
      default:
        break;
    }
    this->gpioRelease = millis() + (frame.repeats * 200);
    this->gpioDir = dir;
  }  
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
