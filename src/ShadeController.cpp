#include <Preferences.h>
#include "TaskWdt.h"
#include "esp_log.h"
#include "ConfigSettings.h"
#include "Somfy.h"
#include "ConfigFile.h"
#include "ModelEditor.h"
#include "ShadeChanges.h"

static const char *TAG = "Somfy";

extern SomfyShadeController somfy;
extern ConfigSettings settings;

void SomfyShadeController::end() { this->transceiver.disableReceive(); }
SomfyShadeController::SomfyShadeController() {
  memset(this->m_shadeIds, 255, sizeof(this->m_shadeIds));
  uint64_t mac = ESP.getEfuseMac();
  this->startingAddress = mac & 0x0FFFFF;
}
SomfyShade *SomfyShadeController::findShadeByRemoteAddress(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade &shade = this->shades[i];
    if(shade.getRemoteAddress() == address) return &shade;
    else {
      for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
        if(shade.linkedRemotes[j].getRemoteAddress() == address) return &shade;
      }
    }
  }
  return nullptr;
}
SomfyGroup *SomfyShadeController::findGroupByRemoteAddress(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup &group = this->groups[i];
    if(group.getRemoteAddress() == address) return &group;
  }
  return nullptr;
}
void SomfyShadeController::updateGroupFlags() {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &this->groups[i];
    if(group && group->getGroupId() != 255) {
      uint8_t flags = group->flags;
      group->updateFlags();
      if(flags != group->flags)
        group->emitState();
    }
  }
}
bool SomfyShadeController::begin() {
  Preferences pref;
  // Load up all the configuration data.
  //ShadeConfigFile::getAppVersion(this->appVersion);
  ESP_LOGI(TAG, "App Version:%u.%u.%u", settings.appVersion.major, settings.appVersion.minor, settings.appVersion.build);
  if(ShadeConfigFile::exists()) {
    ESP_LOGI(TAG, "shades.cfg exists so we are using that");
    ShadeConfigFile::load(this);
  }
  else {
    ESP_LOGI(TAG, "Starting clean");
  }
  this->transceiver.begin();

  // Set the radio type for shades that have yet to be specified.
  bool saveFlag = false;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() != 255 && shade->bitLength == 0) {
      //Serial.printf("Setting bit length to %d\n", this->transceiver.config.type);
      shade->bitLength = this->transceiver.config.type;
      saveFlag = true;
    }
  }
  if(saveFlag) somfy.commit();
  return true;
}
void SomfyShadeController::commit() {
  resetTaskWdt(); // Make sure we don't reset inadvertently.
  ShadeConfigFile file;
  file.begin();
  file.save(this);
  file.end();
  this->isDirty = false;
  this->lastCommit = millis();
}
void SomfyShadeController::writeBackup() {
  resetTaskWdt(); // Make sure we don't reset inadvertently.
  this->backupData = "";
  this->backupData.reserve(16384);
  ShadeConfigFile file;
  file.beginRAM(&this->backupData);
  file.backup(this);
  file.end();
}
SomfyRoom * SomfyShadeController::getRoomById(uint8_t roomId) {
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    if(this->rooms[i].roomId == roomId) return &this->rooms[i];
  }
  return nullptr;
}
SomfyShade * SomfyShadeController::getShadeById(uint8_t shadeId) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() == shadeId) return &this->shades[i];
  }
  return nullptr;
}
SomfyGroup * SomfyShadeController::getGroupById(uint8_t groupId) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() == groupId) return &this->groups[i];
  }
  return nullptr;
}
void SomfyRoom::clear() {
  this->roomId = 0;
  strcpy(this->name, "");
}
bool SomfyRoom::save() { somfy.commit(); return true; }
void SomfyShadeController::processFrame(somfy_frame_t &frame, bool internal, const SomfyRemote *skip) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(&this->shades[i] == skip) continue;
    if(this->shades[i].getShadeId() != 255) this->shades[i].processFrame(frame, internal);
  }
}
void SomfyShadeController::markTxPending(uint32_t remoteAddress, uint32_t ticket, const SomfyRemote *skip) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade == skip) continue;
    if(shade->getShadeId() != 255 && shade->findRemote(remoteAddress)) shade->markTxPending(ticket);
  }
  // Group commands move their member shades through processInternalCommand.
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &this->groups[i];
    if(group->getGroupId() == 255 || group->getRemoteAddress() != remoteAddress) continue;
    for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) {
      if(group->linkedShades[j] == 0) continue;
      SomfyShade *shade = this->getShadeById(group->linkedShades[j]);
      if(shade) shade->markTxPending(ticket);
    }
  }
}
void SomfyShadeController::processTxEvent(const somfy_tx_event_t &evt) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(this->shades[i].getShadeId() != 255) this->shades[i].processTxEvent(evt);
}
void SomfyShadeController::processWaitingFrame() {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(this->shades[i].getShadeId() != 255) this->shades[i].processWaitingFrame();
}
// A deleted shade, group or room leaves a gap, so a new one takes the lowest
// free id, from 1 up to the maximum.
uint8_t SomfyShadeController::getNextShadeId() {
  uint8_t ids[SOMFY_MAX_SHADES];
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) ids[i] = this->shades[i].getShadeId();
  const uint8_t id = firstFreeId(ids, SOMFY_MAX_SHADES, SOMFY_MAX_SHADES);
  return id == 0 ? 255 : id;
}
int8_t SomfyShadeController::getMaxShadeOrder() {
  int8_t orders[SOMFY_MAX_SHADES];
  size_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(this->shades[i].getShadeId() != 255) orders[count++] = this->shades[i].sortOrder;
  return nextSortOrder(orders, count) - 1;
}
int8_t SomfyShadeController::getMaxGroupOrder() {
  int8_t orders[SOMFY_MAX_GROUPS];
  size_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++)
    if(this->groups[i].getGroupId() != 255) orders[count++] = this->groups[i].sortOrder;
  return nextSortOrder(orders, count) - 1;
}
uint8_t SomfyShadeController::getNextGroupId() {
  uint8_t ids[SOMFY_MAX_GROUPS];
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) ids[i] = this->groups[i].getGroupId();
  const uint8_t id = firstFreeId(ids, SOMFY_MAX_GROUPS, SOMFY_MAX_GROUPS);
  return id == 0 ? 255 : id;
}
uint32_t SomfyShadeController::getNextRemoteAddress(uint8_t id) {
  uint32_t used[SOMFY_MAX_SHADES + SOMFY_MAX_GROUPS];
  size_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(this->shades[i].getShadeId() != 255) used[count++] = this->shades[i].getRemoteAddress();
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++)
    if(this->groups[i].getGroupId() != 255) used[count++] = this->groups[i].getRemoteAddress();
  return offeredRemoteAddress(this->startingAddress, id, used, count);
}
uint8_t SomfyShadeController::getNextRoomId() {
  uint8_t ids[SOMFY_MAX_ROOMS];
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) ids[i] = this->rooms[i].roomId;
  return firstFreeId(ids, SOMFY_MAX_ROOMS, SOMFY_MAX_ROOMS);
}
int8_t SomfyShadeController::getMaxRoomOrder() {
  int8_t orders[SOMFY_MAX_ROOMS];
  size_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++)
    if(this->rooms[i].roomId != 0) orders[count++] = this->rooms[i].sortOrder;
  return nextSortOrder(orders, count) - 1;
}
uint8_t SomfyShadeController::repeaterCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(this->repeaters[i] != 0) count++;
  }
  return count;
}
uint8_t SomfyShadeController::roomCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    if(this->rooms[i].roomId != 0) count++;
  }
  return count;
}
uint8_t SomfyShadeController::shadeCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() != 255) count++;
  }
  return count;
}
uint8_t SomfyShadeController::groupCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() != 255) count++;
  }
  return count;
}
SomfyShade *SomfyShadeController::addShade() {
  Preferences pref;
  uint8_t shadeId = this->getNextShadeId();
  // So the next shade id will be the first one we run into with an id of 255 so
  // if it gets deleted in the middle then it will get the first slot that is empty.
  // There is no apparent way around this.  In the future we might actually add an indexer
  // to it for sorting later.  The time has come so the sort order is set below.
  if(shadeId == 255) return nullptr;
  SomfyShade *shade = &this->shades[shadeId - 1];
  if(shade) {
    shade->setShadeId(shadeId);
    shade->sortOrder = this->getMaxShadeOrder() + 1;
    ESP_LOGD(TAG, "Sort order set to %d", shade->sortOrder);
    this->isDirty = true;
  }
  return shade;
}
bool SomfyShadeController::unlinkRepeater(uint32_t address) {
  removeFromList(this->repeaters, SOMFY_MAX_REPEATERS, address);
  this->isDirty = true;
  return true;
}
// False when every repeater slot is taken.
bool SomfyShadeController::linkRepeater(uint32_t address) {
  if(!addToList(this->repeaters, SOMFY_MAX_REPEATERS, address)) return false;
  this->isDirty = true;
  return true;
}
SomfyRoom *SomfyShadeController::addRoom() {
  uint8_t roomId = this->getNextRoomId();
  // So the next room id will be the first one we run into with an id of 0 so
  if(roomId == 0) return nullptr;
  SomfyRoom *room = &this->rooms[roomId - 1];
  if(room) {
    room->roomId = roomId;
    room->sortOrder = this->getMaxRoomOrder() + 1;
    this->isDirty = true;
  }
  return room;
}
SomfyGroup *SomfyShadeController::addGroup() {
  uint8_t groupId = this->getNextGroupId();
  // So the next shade id will be the first one we run into with an id of 255 so
  // if it gets deleted in the middle then it will get the first slot that is empty.
  // There is no apparent way around this.  In the future we might actually add an indexer
  // to it for sorting later.
  if(groupId == 255) return nullptr;
  SomfyGroup *group = &this->groups[groupId - 1];
  if(group) {
    group->setGroupId(groupId);
    group->sortOrder = this->getMaxGroupOrder() + 1;
    this->isDirty = true;
  }
  return group;
}
uint32_t SomfyShadeController::sendFrame(somfy_frame_t &frame, uint8_t repeat, uint16_t delayMs, bool priority) {
  somfy_tx_job_t job;
  encodeTxJob(job, frame, repeat);
  job.firstRepeat = 1;
  return this->transceiver.submitTx(job, priority, delayMs);
}
bool SomfyShadeController::deleteShade(uint8_t shadeId) {
  Preferences pref;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() == shadeId) {
      shades[i].unpublish();
      this->shades[i].clear();
    }
  }
  // Its groups lose it, or a new shade given its id would join them.
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() != 255 && this->groups[i].unlinkShade(shadeId)) this->groups[i].configChanged();
  }
  this->commit();
  return true;
}
bool SomfyShadeController::deleteRoom(uint8_t roomId) {
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    if(this->rooms[i].roomId == roomId) {
      rooms[i].unpublish();
      for(uint8_t j = 0; j < SOMFY_MAX_SHADES; j++) {
        if(shades[j].roomId == roomId) {
          shades[j].roomId = 0;
          shades[j].emitState();
          shades[j].configChanged();
        }
      }
      for(uint8_t j = 0; j < SOMFY_MAX_GROUPS; j++) {
        if(groups[j].roomId == roomId) {
          groups[j].roomId = 0;
          groups[j].emitState();
          groups[j].configChanged();
        }
      }
      this->rooms[i].clear();
    }
  }
  this->commit();
  return true;
}
bool SomfyShadeController::deleteGroup(uint8_t groupId) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() == groupId) {
      // Its shades are no longer in a group, which their inGroup shows.
      for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES && groups[i].linkedShades[j] != 0; j++) {
        SomfyShade *shade = this->getShadeById(groups[i].linkedShades[j]);
        if(shade) shade->configChanged();
      }
      groups[i].unpublish();
      this->groups[i].clear();
    }
  }
  this->commit();
  return true;
}
void SomfyShadeController::loop() { 
  this->transceiver.loop(); 
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() != 255) {
      this->shades[i].checkMovement();
      this->shades[i].setGPIOs();
    }
  }
  // Only commit the file once per second.
  if(this->isDirty && millis() - this->lastCommit > 1000) {
    this->commit();
  }
}
