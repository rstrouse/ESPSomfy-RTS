#include "esp_log.h"
#include "Somfy.h"
#include "ModelEditor.h"

static const char *TAG = "Somfy";

extern SomfyShadeController somfy;

void SomfyGroup::clear() {
  this->setGroupId(255);
  this->setRemoteAddress(0);
  this->repeats = 0;
  this->roomId = 0;
  this->name[0] = 0x00;
  memset(&this->linkedShades, 0x00, sizeof(this->linkedShades));
}
// False when the group is full.  The caller saves the change.
bool SomfyGroup::linkShade(uint8_t shadeId) { return addToList(this->linkedShades, SOMFY_MAX_GROUPED_SHADES, shadeId); }
// False when the shade wasn't in the group.  The caller saves the change.
bool SomfyGroup::unlinkShade(uint8_t shadeId) { return removeFromList(this->linkedShades, SOMFY_MAX_GROUPED_SHADES, shadeId); }
// Linked shade ids are kept packed so a search can stop at the first 0.
void SomfyGroup::compressLinkedShadeIds() { packList(this->linkedShades, SOMFY_MAX_GROUPED_SHADES); }
bool SomfyGroup::hasShadeId(uint8_t shadeId) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] == 0) break;
    if(this->linkedShades[i] == shadeId) return true;
  }
  return false;
}
void SomfyGroup::updateFlags() { 
  uint8_t oldFlags = this->flags;
  this->flags = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] != 0) {
      SomfyShade *shade = somfy.getShadeById(this->linkedShades[i]);
      if(shade) this->flags |= shade->getFlags();
    }
    else break;
  }
  if(oldFlags != this->flags) this->emitState();
}
int8_t SomfyGroup::p_direction(int8_t dir) {
  int8_t old = this->direction;
  if(old != dir) {
    this->direction = dir;
    this->changed |= GROUP_CHANGED_DIRECTION;
  }
  return old;
}
bool SomfyGroup::sendCommand(somfy_commands cmd) { return this->sendCommand(cmd, this->repeats); }
bool SomfyGroup::sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize) {
  ESP_LOGI(TAG, "[Group %u] sendCommand cmd=%s repeat=%u", this->getGroupId(), translateSomfyCommand(cmd).c_str(), repeat);
  // This sendCommand function will always be called externally. sendCommand at the remote level
  // is expected to be called internally when the motor needs commanded.
  if(this->bitLength == 0) this->bitLength = somfy.transceiver.config.type;
  // Leave the group and its shades alone if the radio didn't take the command.
  if(!SomfyRemote::sendCommand(cmd, repeat, stepSize)) return false;

  switch(cmd) {
    case somfy_commands::My:
      this->p_direction(0);
      break;
    case somfy_commands::Up:
      this->p_direction(-1);
      break;
    case somfy_commands::Down:
      this->p_direction(1);
      break;
    default:
      break;
  }
  
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] != 0) {
      SomfyShade *shade = somfy.getShadeById(this->linkedShades[i]);
      if(shade) {
        ESP_LOGI(TAG, "[Group %u] processInternalCommand on shade %u cmd=%s", this->getGroupId(), shade->getShadeId(), translateSomfyCommand(cmd).c_str());
        shade->processInternalCommand(cmd, repeat);
        shade->emitCommand(cmd, "group", this->getRemoteAddress());
      }
    }
  }
  this->updateFlags();
  this->emitState();
  return true;
}
bool SomfyGroup::save() { somfy.commit(); return true; }
