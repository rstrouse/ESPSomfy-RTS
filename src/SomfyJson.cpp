#include "esp_log.h"
#include "ConfigSettings.h"
#include "Somfy.h"
#include <atomic>
#include "ModelEditor.h"
#include "SomfyEditor.h"
#include "ShadeChanges.h"
#include "outputs/ProtoSocket.h"
#include "LoopQueue.h"

static const char *TAG = "Somfy";

extern SomfyShadeController somfy;
extern ConfigSettings settings;
extern LoopQueue loopQueue;

int8_t SomfyShade::validateJSON(JsonObject &obj) {
  int8_t ret = 0;
  shade_types type = this->shadeType;
  if(!obj["shadeType"].isNull()) {
    if(obj["shadeType"].is<const char *>()) {
      if(strncmp(obj["shadeType"].as<const char *>(), "roller", 7) == 0)
        type = shade_types::roller;
      else if(strncmp(obj["shadeType"].as<const char *>(), "ldrapery", 9) == 0)
        type = shade_types::ldrapery;
      else if(strncmp(obj["shadeType"].as<const char *>(), "rdrapery", 9) == 0)
        type = shade_types::rdrapery;
      else if(strncmp(obj["shadeType"].as<const char *>(), "cdrapery", 9) == 0)
        type = shade_types::cdrapery;
      else if(strncmp(obj["shadeType"].as<const char *>(), "garage1", 7) == 0)
        type = shade_types::garage1;
      else if(strncmp(obj["shadeType"].as<const char *>(), "garage3", 7) == 0)
        type = shade_types::garage3;
      else if(strncmp(obj["shadeType"].as<const char *>(), "blind", 5) == 0)
        type = shade_types::blind;
      else if(strncmp(obj["shadeType"].as<const char *>(), "awning", 7) == 0)
        type = shade_types::awning;
      else if(strncmp(obj["shadeType"].as<const char *>(), "shutter", 8) == 0)
        type = shade_types::shutter;
      else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact2", 12) == 0)
        type = shade_types::drycontact2;
      else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact", 11) == 0)
        type = shade_types::drycontact;
    }
    else {
      type = static_cast<shade_types>(obj["shadeType"].as<uint8_t>());
    }
  }
  // The pins and protocol the shade is about to have.
  const radio_proto proto = !obj["proto"].isNull() ? static_cast<radio_proto>(obj["proto"].as<uint8_t>()) : this->proto;
  const uint8_t upPin = !obj["gpioUp"].isNull() ? obj["gpioUp"].as<uint8_t>() : this->gpioUp;
  const uint8_t downPin = !obj["gpioDown"].isNull() ? obj["gpioDown"].as<uint8_t>() : this->gpioDown;
  const uint8_t myPin = !obj["gpioMy"].isNull() ? obj["gpioMy"].as<uint8_t>() : this->gpioMy;
  pin_users_t radio, ethernet, others;
  if(somfy.transceiver.config.enabled)
    radio.uses = [](uint8_t pin, void *) { return somfy.transceiver.usesPin(pin); };
#if HAS_ETHERNET
  if(settings.connType == conn_types_t::ethernet || settings.connType == conn_types_t::ethernetpref)
    ethernet.uses = [](uint8_t pin, void *) { return settings.Ethernet.usesPin(pin); };
#endif
  others.ctx = this;
  others.uses = [](uint8_t pin, void *ctx) {
    const uint8_t self = static_cast<SomfyShade *>(ctx)->getShadeId();
    for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
      SomfyShade &shade = somfy.shades[i];
      if(shade.getShadeId() == self || shade.getShadeId() == 255) continue;
      if(shade.usesPin(pin)) return true;
    }
    return false;
  };
  ret = gpioPinConflict(type, proto, upPin, downPin, myPin, radio, ethernet, others);
  return ret;
}
int8_t SomfyShade::fromJSON(JsonObject &obj) {
  int8_t err = this->validateJSON(obj);
  if(err == 0) {
    if(!obj["name"].isNull()) strlcpy(this->name, obj["name"], sizeof(this->name));
    if(!obj["roomId"].isNull()) this->roomId = obj["roomId"];
    if(!obj["upTime"].isNull()) this->upTime = obj["upTime"];
    if(!obj["downTime"].isNull()) this->downTime = obj["downTime"];
    if(!obj["remoteAddress"].isNull()) this->setRemoteAddress(obj["remoteAddress"]);
    if(!obj["tiltTime"].isNull()) this->tiltTime = obj["tiltTime"];
    if(!obj["stepSize"].isNull()) this->stepSize = obj["stepSize"];
    if(!obj["hasTilt"].isNull()) this->tiltType = static_cast<bool>(obj["hasTilt"]) ? tilt_types::none : tilt_types::tiltmotor;
    if(!obj["bitLength"].isNull()) this->bitLength = obj["bitLength"];
    if(!obj["proto"].isNull()) this->proto = static_cast<radio_proto>(obj["proto"].as<uint8_t>());
    if(!obj["sunSensor"].isNull()) this->sunSensor = obj["sunSensor"];
    if(!obj["simMy"].isNull()) this->simMy = obj["simMy"];
    if(!obj["light"].isNull()) this->light = obj["light"];
    if(!obj["gpioFlags"].isNull()) this->gpioFlags = obj["gpioFlags"];
    if(!obj["gpioLLTrigger"].isNull()) {
      if(obj["gpioLLTrigger"].as<bool>())
        this->gpioFlags |= (uint8_t)gpio_flags_t::LowLevelTrigger;
      else
        this->gpioFlags &= ~(uint8_t)gpio_flags_t::LowLevelTrigger;
    }
    
    if(!obj["shadeType"].isNull()) {
      if(obj["shadeType"].is<const char *>()) {
        if(strncmp(obj["shadeType"].as<const char *>(), "roller", 7) == 0)
          this->shadeType = shade_types::roller;
        else if(strncmp(obj["shadeType"].as<const char *>(), "ldrapery", 9) == 0)
          this->shadeType = shade_types::ldrapery;
        else if(strncmp(obj["shadeType"].as<const char *>(), "rdrapery", 9) == 0)
          this->shadeType = shade_types::rdrapery;
        else if(strncmp(obj["shadeType"].as<const char *>(), "cdrapery", 9) == 0)
          this->shadeType = shade_types::cdrapery;
        else if(strncmp(obj["shadeType"].as<const char *>(), "garage1", 7) == 0)
          this->shadeType = shade_types::garage1;
        else if(strncmp(obj["shadeType"].as<const char *>(), "garage3", 7) == 0)
          this->shadeType = shade_types::garage3;
        else if(strncmp(obj["shadeType"].as<const char *>(), "blind", 5) == 0)
          this->shadeType = shade_types::blind;
        else if(strncmp(obj["shadeType"].as<const char *>(), "awning", 7) == 0)
          this->shadeType = shade_types::awning;
        else if(strncmp(obj["shadeType"].as<const char *>(), "shutter", 8) == 0)
          this->shadeType = shade_types::shutter;
        else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact2", 12) == 0)
          this->shadeType = shade_types::drycontact2;
        else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact", 11) == 0)
          this->shadeType = shade_types::drycontact;
      }
      else {
        this->shadeType = static_cast<shade_types>(obj["shadeType"].as<uint8_t>());
      }
    }
    if(!obj["flipCommands"].isNull()) this->flipCommands = obj["flipCommands"].as<bool>();
    if(!obj["flipPosition"].isNull()) this->flipPosition = obj["flipPosition"].as<bool>();
    if(!obj["repeats"].isNull()) this->repeats = obj["repeats"];
    if(!obj["tiltType"].isNull()) {
      if(obj["tiltType"].is<const char *>()) {
        if(strncmp(obj["tiltType"].as<const char *>(), "none", 4) == 0)
          this->tiltType = tilt_types::none;
        else if(strncmp(obj["tiltType"].as<const char *>(), "tiltmotor", 9) == 0)
          this->tiltType = tilt_types::tiltmotor;
        else if(strncmp(obj["tiltType"].as<const char *>(), "integ", 5) == 0)
          this->tiltType = tilt_types::integrated;
        else if(strncmp(obj["tiltType"].as<const char *>(), "tiltonly", 8) == 0)
          this->tiltType = tilt_types::tiltonly;
      }
      else {
        this->tiltType = static_cast<tilt_types>(obj["tiltType"].as<uint8_t>());
      }
    }
    if(!obj["linkedAddresses"].isNull()) {
      uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
      memset(linkedAddresses, 0x00, sizeof(linkedAddresses));
      JsonArray arr = obj["linkedAddresses"];
      uint8_t i = 0;
      for(uint32_t addr : arr) {
        linkedAddresses[i++] = addr;
      }
      for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
        this->linkedRemotes[j].setRemoteAddress(linkedAddresses[j]);
      }
    }
    if(!obj["flags"].isNull()) this->setFlags(obj["flags"].as<uint8_t>());
    if(this->proto == radio_proto::GP_Remote || this->proto == radio_proto::GP_Relay) {
      if(!obj["gpioUp"].isNull()) this->gpioUp = obj["gpioUp"];
      if(!obj["gpioDown"].isNull()) this->gpioDown = obj["gpioDown"];
      pinMode(this->gpioUp, OUTPUT);
      pinMode(this->gpioDown, OUTPUT);
    }
    if(this->proto == radio_proto::GP_Remote) {
      if(!obj["gpioMy"].isNull()) this->gpioMy = obj["gpioMy"];
      pinMode(this->gpioMy, OUTPUT);
    }
  }
  return err;
}
bool SomfyRoom::fromJSON(JsonObject &obj) {
  if(!obj["name"].isNull()) strlcpy(this->name, obj["name"], sizeof(this->name));
  if(!obj["sortOrder"].isNull()) this->sortOrder = obj["sortOrder"];
  return true;
}
bool SomfyGroup::fromJSON(JsonObject &obj) {
  if(!obj["name"].isNull()) strlcpy(this->name, obj["name"], sizeof(this->name));
  if(!obj["roomId"].isNull()) this->roomId = obj["roomId"];
  if(!obj["remoteAddress"].isNull()) this->setRemoteAddress(obj["remoteAddress"]);
  if(!obj["bitLength"].isNull()) this->bitLength = obj["bitLength"];
  if(!obj["proto"].isNull()) this->proto = static_cast<radio_proto>(obj["proto"].as<uint8_t>());
  if(!obj["flipCommands"].isNull()) this->flipCommands = obj["flipCommands"].as<bool>();
  
  //if(!obj["sunSensor"].isNull()) this->hasSunSensor() = obj["sunSensor"];  This is calculated
  if(!obj["repeats"].isNull()) this->repeats = obj["repeats"];
  // Members change only through /linkToGroup and /unlinkFromGroup (ModelEditor).
  return true;
}
bool Transceiver::fromJSON(JsonObject& obj) {
    if (!obj["config"].isNull()) {
      JsonObject objConfig = obj["config"];
      // Parse into a copy so the radio task never sees a half updated config.
      transceiver_config_t cfg = this->config;
      cfg.fromJSON(objConfig);
      this->setConfig(cfg);
    }
    return true;
}
void transceiver_config_t::fromJSON(JsonObject& obj) {
    //Serial.print("Deserialize Radio JSON ");
    if(!obj["type"].isNull()) this->type = obj["type"];
    if(!obj["CSNPin"].isNull()) this->CSNPin = obj["CSNPin"];
    if(!obj["MISOPin"].isNull()) this->MISOPin = obj["MISOPin"];
    if(!obj["MOSIPin"].isNull()) this->MOSIPin = obj["MOSIPin"];
    if(!obj["RXPin"].isNull()) this->RXPin = obj["RXPin"];
    if(!obj["SCKPin"].isNull()) this->SCKPin = obj["SCKPin"];
    if(!obj["TXPin"].isNull()) this->TXPin = obj["TXPin"];
    if(!obj["rxBandwidth"].isNull()) this->rxBandwidth = obj["rxBandwidth"]; // float
    if(!obj["frequency"].isNull()) this->frequency = obj["frequency"];  // float
    if(!obj["deviation"].isNull()) this->deviation = obj["deviation"];  // float
    if(!obj["enabled"].isNull()) this->enabled = obj["enabled"];
    if(!obj["txPower"].isNull()) this->txPower = obj["txPower"];
    if(!obj["proto"].isNull()) this->proto = static_cast<radio_proto>(obj["proto"].as<uint8_t>());
    if(!obj["noiseDetection"].isNull()) this->noiseDetection = obj["noiseDetection"];
    ESP_LOGD(TAG, "SCK:%u MISO:%u MOSI:%u CSN:%u RX:%u TX:%u", this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin, this->RXPin, this->TXPin);
}

/*********************************************************************
 * ModelEditor on the device (SomfyEditor.h).  Here rather than in a file of
 * its own because every file that uses ArduinoJson carries its own copy of the
 * library's helpers and assert strings (moving the radio's fromJSON here saved
 * 2.8 KB of flash).
 ********************************************************************/
static std::atomic<bool> editBusy(false);

bool beginModelEdit() { return !editBusy.exchange(true); }
void cancelModelEdit(model_edit_t *edit) {
  delete edit;
  editBusy.store(false);
}
model_edit_t *newModelEdit(model_edit_type_t type, bool &busy) {
  busy = !beginModelEdit();
  if(busy) return nullptr;
  model_edit_t *edit = new (std::nothrow) model_edit_t();
  if(!edit) {
    cancelModelEdit(nullptr);
    return nullptr;
  }
  edit->type = type;
  return edit;
}
bool submitModelEdit(model_edit_t *edit) {
  loop_item_t item;
  item.type = loop_item_type_t::model_edit;
  item.edit = edit;
  if(!edit->doc.overflowed() && loopQueue.submit(item)) return true;
  cancelModelEdit(edit);
  return false;
}
// What protocol 2 clients get after an edit (editChanges).  A shade or group
// goes out whole from the change fan-out, by its config bit, so it is sent
// once with whatever else changed; the rest goes out now.
static void markEdited(const model_edit_t &edit, const edit_result_t &result) {
  const edit_changes_t c = editChanges(edit.type, result);
  SomfyShade *shade = c.shadeId ? somfy.getShadeById(c.shadeId) : nullptr;
  if(shade) shade->configChanged();
  SomfyGroup *group = c.groupId ? somfy.getGroupById(c.groupId) : nullptr;
  if(group) group->configChanged();
  SomfyRoom *room = c.roomId ? somfy.getRoomById(c.roomId) : nullptr;
  if(room) protoSocket.room(*room);
  if(c.order != sort_list_t::none) protoSocket.order(c.order);
  if(c.repeaters) protoSocket.repeaters();
  if(c.removedShadeId) protoSocket.shadeRemoved(c.removedShadeId);
  if(c.removedGroupId) protoSocket.groupRemoved(c.removedGroupId);
  if(c.removedRoomId) protoSocket.roomRemoved(c.removedRoomId);
}
void runModelEdit(model_edit_t *edit) {
  const edit_result_t result = applyModelEdit(*edit);
  if(!result.ok) ESP_LOGW(TAG, "Edit %u refused: %s", static_cast<uint8_t>(edit->type), result.desc);
  // The reply first: the edit's events follow it (docs/PROTOCOL.md), so a
  // client that waits for the reply doesn't read past them.
  protoSocket.replyEdit(edit->replyTo, edit->type, result);
  if(result.ok) markEdited(*edit, result);
  cancelModelEdit(edit);
}

static edit_result_t done(uint8_t id = 0) {
  edit_result_t r;
  r.id = id;
  return r;
}
// Gives each id in the request the next sort order, in the order given.
template<typename Find>
static edit_result_t applyOrder(JsonDocument &doc, Find find) {
  if(!doc.is<JsonArray>()) return edit_result_t::error("JSON parse error");
  int8_t order = 0;
  for(JsonVariant v : doc.as<JsonArray>()) {
    auto *item = find(v.as<uint8_t>());
    if(item) item->sortOrder = order++;
  }
  somfy.commit();
  return done();
}
edit_result_t applyModelEdit(model_edit_t &edit) {
  JsonObject obj = edit.doc.as<JsonObject>();
  switch(edit.type) {
    case model_edit_type_t::add_room: {
      SomfyRoom *room = somfy.addRoom();
      if(!room) return edit_result_t::error(EDIT_ROOMS_FULL);
      room->fromJSON(obj);
      room->save();
      room->publish();
      return done(room->roomId);
    }
    case model_edit_type_t::add_shade: {
      SomfyShade *shade = somfy.addShade();
      if(!shade) return edit_result_t::error(EDIT_SHADES_FULL);
      const int8_t err = shade->fromJSON(obj);
      if(err != 0) {
        // Not added after all: free the slot it took.
        shade->clear();
        return edit_result_t::error("Data Error.", err);
      }
      shade->save();
      return done(shade->getShadeId());
    }
    case model_edit_type_t::add_group: {
      SomfyGroup *group = somfy.addGroup();
      if(!group) return edit_result_t::error(EDIT_GROUPS_FULL);
      group->fromJSON(obj);
      group->save();
      group->publish();
      return done(group->getGroupId());
    }
    case model_edit_type_t::save_room: {
      if(obj["roomId"].isNull()) return edit_result_t::error("No room id was supplied.");
      SomfyRoom *room = somfy.getRoomById(obj["roomId"]);
      if(!room) return edit_result_t::missing("Room Id not found.");
      room->fromJSON(obj);
      room->save();
      room->publish();
      return done(room->roomId);
    }
    case model_edit_type_t::save_shade: {
      if(obj["shadeId"].isNull()) return edit_result_t::error("No shade id was supplied.");
      SomfyShade *shade = somfy.getShadeById(obj["shadeId"]);
      if(!shade) return edit_result_t::missing("Shade Id not found.");
      const int8_t err = shade->fromJSON(obj);
      if(err != 0) return edit_result_t::error("Data Error.", err);
      shade->save();
      shade->emitState();
      return done(shade->getShadeId());
    }
    case model_edit_type_t::save_group: {
      if(obj["groupId"].isNull()) return edit_result_t::error("No group id was supplied.");
      SomfyGroup *group = somfy.getGroupById(obj["groupId"]);
      if(!group) return edit_result_t::missing("Group Id not found.");
      group->fromJSON(obj);
      group->save();
      group->emitState();
      return done(group->getGroupId());
    }
    case model_edit_type_t::delete_room: {
      if(obj["roomId"].isNull()) return edit_result_t::error("No room id was supplied.");
      const uint8_t roomId = obj["roomId"];
      if(!somfy.getRoomById(roomId)) return edit_result_t::missing("Room with the specified id not found.");
      somfy.deleteRoom(roomId);
      return done(roomId);
    }
    case model_edit_type_t::delete_shade: {
      if(obj["shadeId"].isNull()) return edit_result_t::error("No shade id was supplied.");
      const uint8_t shadeId = obj["shadeId"];
      SomfyShade *shade = somfy.getShadeById(shadeId);
      if(!shade) return edit_result_t::missing("Shade with the specified id not found.");
      if(shade->isInGroup()) return edit_result_t::error("This shade is a member of a group and cannot be deleted.");
      somfy.deleteShade(shadeId);
      return done(shadeId);
    }
    case model_edit_type_t::delete_group: {
      if(obj["groupId"].isNull()) return edit_result_t::error("No group id was supplied.");
      const uint8_t groupId = obj["groupId"];
      if(!somfy.getGroupById(groupId)) return edit_result_t::missing("Group with the specified id not found.");
      somfy.deleteGroup(groupId);
      return done(groupId);
    }
    case model_edit_type_t::link_remote:
    case model_edit_type_t::unlink_remote: {
      if(obj["shadeId"].isNull()) return edit_result_t::error("No shade id was supplied.");
      SomfyShade *shade = somfy.getShadeById(obj["shadeId"]);
      if(!shade) return edit_result_t::missing("Shade Id not found.");
      if(obj["remoteAddress"].isNull()) return edit_result_t::error("Remote address not provided.");
      const uint32_t address = obj["remoteAddress"];
      if(edit.type == model_edit_type_t::unlink_remote) shade->unlinkRemote(address);
      else if(!shade->linkRemote(address, obj["rollingCode"] | (uint16_t)0))
        return edit_result_t::error("This shade already has the most linked remotes it can.");
      return done(shade->getShadeId());
    }
    case model_edit_type_t::link_to_group:
    case model_edit_type_t::unlink_from_group: {
      const uint8_t groupId = obj["groupId"] | (uint8_t)0;
      const uint8_t shadeId = obj["shadeId"] | (uint8_t)0;
      if(groupId == 0) return edit_result_t::error("Group id not provided.");
      if(shadeId == 0) return edit_result_t::error("Shade id not provided.");
      SomfyGroup *group = somfy.getGroupById(groupId);
      if(!group) return edit_result_t::missing("Group id not found.");
      if(!somfy.getShadeById(shadeId)) return edit_result_t::missing("Shade id not found.");
      if(edit.type == model_edit_type_t::unlink_from_group) group->unlinkShade(shadeId);
      else if(!group->linkShade(shadeId)) return edit_result_t::error("This group already has the most shades it can.");
      somfy.commit();
      group->emitState();
      edit_result_t r = done(groupId);
      r.shadeId = shadeId;
      return r;
    }
    case model_edit_type_t::link_repeater:
    case model_edit_type_t::unlink_repeater: {
      const uint32_t address = !obj["address"].isNull() ? obj["address"].as<uint32_t>() : (obj["remoteAddress"] | (uint32_t)0);
      if(address == 0) return edit_result_t::error("No repeater address was supplied.");
      if(edit.type == model_edit_type_t::unlink_repeater) somfy.unlinkRepeater(address);
      else if(!somfy.linkRepeater(address)) return edit_result_t::error("Maximum number of repeaters exceeded.");
      somfy.commit();
      return done();
    }
    case model_edit_type_t::set_paired: {
      if(obj["shadeId"].isNull()) return edit_result_t::error("No shade id was supplied.");
      SomfyShade *shade = somfy.getShadeById(obj["shadeId"]);
      if(!shade) return edit_result_t::missing("Shade not found to pair");
      shade->paired = obj["paired"] | false;
      shade->save();
      shade->emitState();
      return done(shade->getShadeId());
    }
    case model_edit_type_t::room_order:
      return applyOrder(edit.doc, [](uint8_t id) { return id != 0 ? somfy.getRoomById(id) : nullptr; });
    case model_edit_type_t::shade_order:
      return applyOrder(edit.doc, [](uint8_t id) { return id != 255 ? somfy.getShadeById(id) : nullptr; });
    case model_edit_type_t::group_order:
      return applyOrder(edit.doc, [](uint8_t id) { return id != 255 ? somfy.getGroupById(id) : nullptr; });
  }
  return edit_result_t::error("Unknown edit.");
}
