#ifndef MODEL_EDITOR_H
#define MODEL_EDITOR_H
#include <stddef.h>
#include <stdint.h>
#include "SomfyFrame.h"
#include "ShadeTypes.h"

// The rules ModelEditor applies when the shade model is edited: which id, sort
// order and remote address a new room, shade or group gets, how ids go into
// and out of a list, and which GPIO pins a shade may not use.  Kept free of the
// shade model so it can be unit tested on the PC (pio test -e native).  The
// edits themselves are applied on loop() (SomfyJson.cpp), since only loop()
// changes the shade model (docs/adr/0002-loop-task-is-the-only-writer-of-the-shade-model.md).

// The edits, one per edit route.
enum class model_edit_type_t : uint8_t {
  add_room, add_shade, add_group,
  save_room, save_shade, save_group,
  delete_room, delete_shade, delete_group,
  link_remote, unlink_remote,
  link_to_group, unlink_from_group,
  link_repeater, unlink_repeater,
  set_paired,
  room_order, shade_order, group_order
};
// What an edit did, sent in the edit op's reply.  id is the room,
// shade or group it added or changed, 0 when none.  shadeId is the shade a
// group gained or lost.  code is the validation error of a shade that wasn't
// saved, 0 otherwise.  notFound: it was refused for an id that doesn't exist.
struct edit_result_t {
  bool ok = true;
  bool notFound = false;
  uint8_t id = 0;
  uint8_t shadeId = 0;
  int8_t code = 0;
  const char *desc = "";
  static edit_result_t error(const char *desc, int8_t code = 0) {
    edit_result_t r;
    r.ok = false;
    r.desc = desc;
    r.code = code;
    return r;
  }
  static edit_result_t missing(const char *desc) {
    edit_result_t r = error(desc);
    r.notFound = true;
    return r;
  }
};
// Why an add, or the read that offers one, is refused when every slot of its
// kind is taken.
#define EDIT_ROOMS_FULL "Maximum number of rooms exceeded."
#define EDIT_SHADES_FULL "Maximum number of shades exceeded."
#define EDIT_GROUPS_FULL "Maximum number of groups exceeded."

// The lists an edit can put in a new order.
enum class sort_list_t : uint8_t { none, rooms, shades, groups };
// What protocol 2 clients are sent once an edit has gone through: the whole
// items it changed (0 for none), a list's new order, the repeaters.  Deletes
// send their removals as they happen, so they need nothing here.
struct edit_changes_t {
  uint8_t shadeId = 0;
  uint8_t groupId = 0;
  uint8_t roomId = 0;
  sort_list_t order = sort_list_t::none;
  bool repeaters = false;
  // A delete: the id of what was removed.
  uint8_t removedShadeId = 0;
  uint8_t removedGroupId = 0;
  uint8_t removedRoomId = 0;
};
edit_changes_t editChanges(model_edit_type_t type, const edit_result_t &result);

// The lowest id from 1 to lastId that isn't in usedIds, or 0 when every one
// is taken.
uint8_t firstFreeId(const uint8_t *usedIds, size_t count, uint8_t lastId);
// The sort order that puts a new item after every existing one.
int8_t nextSortOrder(const int8_t *orders, size_t count);
// candidate, or the next address after it that isn't in used.  No two shades or
// groups may share a remote address.
uint32_t freeRemoteAddress(uint32_t candidate, const uint32_t *used, size_t count);
// The remote address a new shade or group with that id is offered: the
// starting address plus its id, or the next free one after it.
uint32_t offeredRemoteAddress(uint32_t startingAddress, uint8_t id, const uint32_t *used, size_t count);

// A list of ids or addresses with 0 for an empty slot, kept packed at the front.
// Adding one that is already there changes nothing and succeeds; false when the
// list is full.
bool addToList(uint8_t *list, size_t size, uint8_t item);
bool addToList(uint32_t *list, size_t size, uint32_t item);
// Removes every copy and packs the rest to the front.  False when it wasn't there.
bool removeFromList(uint8_t *list, size_t size, uint8_t item);
bool removeFromList(uint32_t *list, size_t size, uint32_t item);
// Packs the items to the front, keeping their order.
void packList(uint8_t *list, size_t size);

// Who else drives a GPIO pin.
struct pin_users_t {
  bool (*uses)(uint8_t pin, void *ctx) = nullptr;
  void *ctx = nullptr;
  bool usesPin(uint8_t pin) const { return this->uses && this->uses(pin, this->ctx); }
};
#define PIN_CONFLICT_RADIO -10
#define PIN_CONFLICT_ETHERNET -11
#define PIN_CONFLICT_SHADE -12
// Whether a shade of type driven over proto may use these pins (255 for none):
// 0, or which of the radio, the Ethernet PHY or another shade already uses one.
// Only the pins the shade type and protocol actually drive are checked.
int8_t gpioPinConflict(shade_types type, radio_proto proto, uint8_t upPin, uint8_t downPin, uint8_t myPin,
    const pin_users_t &radio, const pin_users_t &ethernet, const pin_users_t &otherShades);
#endif
