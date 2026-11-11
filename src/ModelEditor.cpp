#include "ModelEditor.h"

edit_changes_t editChanges(model_edit_type_t type, const edit_result_t &result) {
  edit_changes_t c;
  switch(type) {
    case model_edit_type_t::add_shade:
    case model_edit_type_t::save_shade:
    case model_edit_type_t::set_paired:
    case model_edit_type_t::link_remote:
    case model_edit_type_t::unlink_remote:
      c.shadeId = result.id;
      break;
    case model_edit_type_t::add_group:
    case model_edit_type_t::save_group:
      c.groupId = result.id;
      break;
    // The shade's inGroup goes with it.
    case model_edit_type_t::link_to_group:
    case model_edit_type_t::unlink_from_group:
      c.groupId = result.id;
      c.shadeId = result.shadeId;
      break;
    case model_edit_type_t::add_room:
    case model_edit_type_t::save_room:
      c.roomId = result.id;
      break;
    case model_edit_type_t::room_order: c.order = sort_list_t::rooms; break;
    case model_edit_type_t::shade_order: c.order = sort_list_t::shades; break;
    case model_edit_type_t::group_order: c.order = sort_list_t::groups; break;
    case model_edit_type_t::link_repeater:
    case model_edit_type_t::unlink_repeater:
      c.repeaters = true;
      break;
    case model_edit_type_t::delete_room: c.removedRoomId = result.id; break;
    case model_edit_type_t::delete_shade: c.removedShadeId = result.id; break;
    case model_edit_type_t::delete_group: c.removedGroupId = result.id; break;
  }
  return c;
}

uint8_t firstFreeId(const uint8_t *usedIds, size_t count, uint8_t lastId) {
  for(uint16_t id = 1; id <= lastId; id++) {
    bool used = false;
    for(size_t i = 0; i < count && !used; i++) used = usedIds[i] == id;
    if(!used) return static_cast<uint8_t>(id);
  }
  return 0;
}
int8_t nextSortOrder(const int8_t *orders, size_t count) {
  int8_t order = -1;
  for(size_t i = 0; i < count; i++)
    if(orders[i] > order) order = orders[i];
  return order + 1;
}
uint32_t freeRemoteAddress(uint32_t candidate, const uint32_t *used, size_t count) {
  // Start over after every bump: the new address may be taken too.
  for(size_t i = 0; i < count; i++) {
    if(used[i] == candidate) {
      candidate++;
      i = static_cast<size_t>(-1);
    }
  }
  return candidate;
}
uint32_t offeredRemoteAddress(uint32_t startingAddress, uint8_t id, const uint32_t *used, size_t count) {
  return freeRemoteAddress(startingAddress + id, used, count);
}

template<typename T>
static bool addTo(T *list, size_t size, T item) {
  for(size_t i = 0; i < size; i++)
    if(list[i] == item) return true;
  for(size_t i = 0; i < size; i++) {
    if(list[i] == 0) {
      list[i] = item;
      return true;
    }
  }
  return false;
}
template<typename T>
static bool removeFrom(T *list, size_t size, T item) {
  bool removed = false;
  size_t j = 0;
  for(size_t i = 0; i < size; i++) {
    if(list[i] == 0) continue;
    if(list[i] == item) removed = true;
    else list[j++] = list[i];
  }
  while(j < size) list[j++] = 0;
  return removed;
}
void packList(uint8_t *list, size_t size) { removeFrom<uint8_t>(list, size, 0); }
bool addToList(uint8_t *list, size_t size, uint8_t item) { return addTo<uint8_t>(list, size, item); }
bool addToList(uint32_t *list, size_t size, uint32_t item) { return addTo<uint32_t>(list, size, item); }
bool removeFromList(uint8_t *list, size_t size, uint8_t item) { return removeFrom<uint8_t>(list, size, item); }
bool removeFromList(uint32_t *list, size_t size, uint32_t item) { return removeFrom<uint32_t>(list, size, item); }

int8_t gpioPinConflict(shade_types type, radio_proto proto, uint8_t upPin, uint8_t downPin, uint8_t myPin,
    const pin_users_t &radio, const pin_users_t &ethernet, const pin_users_t &otherShades) {
  if(proto != radio_proto::GP_Relay && proto != radio_proto::GP_Remote) return 0;
  // A dry contact or a single button gate driven by a remote only uses the down
  // pin; a two way dry contact and a relay don't use My.
  if(type == shade_types::drycontact ||
    ((type == shade_types::garage1 || type == shade_types::lgate1 || type == shade_types::cgate1 || type == shade_types::rgate1)
      && proto == radio_proto::GP_Remote)) upPin = myPin = 255;
  else if(type == shade_types::drycontact2) myPin = 255;
  if(proto == radio_proto::GP_Relay) myPin = 255;
  const uint8_t pins[] = { upPin, downPin, myPin };
  auto clash = [&pins](const pin_users_t &users) {
    for(uint8_t pin : pins)
      if(pin != 255 && users.usesPin(pin)) return true;
    return false;
  };
  if(clash(radio)) return PIN_CONFLICT_RADIO;
  if(clash(ethernet)) return PIN_CONFLICT_ETHERNET;
  if(clash(otherShades)) return PIN_CONFLICT_SHADE;
  return 0;
}
