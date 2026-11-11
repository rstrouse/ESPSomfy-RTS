#include <unity.h>
#include <set>
#include "ModelEditor.h"

void setUp() {}
void tearDown() {}

/*********************************************************************
 * Ids, sort orders and remote addresses
 ********************************************************************/
static void test_first_free_id_fills_gaps() {
  const uint8_t ids[] = { 1, 2, 4, 255, 255 };
  TEST_ASSERT_EQUAL_UINT8(3, firstFreeId(ids, 5, 30));
}
static void test_first_free_id_of_an_empty_list_is_1() {
  TEST_ASSERT_EQUAL_UINT8(1, firstFreeId(nullptr, 0, 30));
}
static void test_first_free_id_is_0_when_every_one_is_taken() {
  const uint8_t ids[] = { 1, 2, 3 };
  TEST_ASSERT_EQUAL_UINT8(0, firstFreeId(ids, 3, 3));
  TEST_ASSERT_EQUAL_UINT8(4, firstFreeId(ids, 3, 4));
}
static void test_next_sort_order_goes_after_the_last() {
  const int8_t orders[] = { 3, 0, 7, 2 };
  TEST_ASSERT_EQUAL_INT8(8, nextSortOrder(orders, 4));
  TEST_ASSERT_EQUAL_INT8(0, nextSortOrder(nullptr, 0));
}
static void test_free_remote_address_skips_every_taken_one() {
  // 101 is taken, so 102 is tried, which is taken too, and so on.
  const uint32_t used[] = { 103, 101, 102, 500 };
  TEST_ASSERT_EQUAL_UINT32(104, freeRemoteAddress(101, used, 4));
  TEST_ASSERT_EQUAL_UINT32(100, freeRemoteAddress(100, used, 4));
}
static void test_a_new_one_is_offered_the_starting_address_plus_its_id() {
  TEST_ASSERT_EQUAL_UINT32(1005, offeredRemoteAddress(1000, 5, nullptr, 0));
  // Taken by another shade or group, so the next free one.
  const uint32_t used[] = { 1006, 1005 };
  TEST_ASSERT_EQUAL_UINT32(1007, offeredRemoteAddress(1000, 5, used, 2));
}

/*********************************************************************
 * Lists
 ********************************************************************/
static void test_add_goes_into_the_first_empty_slot() {
  uint8_t list[4] = { 5, 7, 0, 0 };
  TEST_ASSERT_TRUE(addToList(list, 4, 9));
  const uint8_t expected[] = { 5, 7, 9, 0 };
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, list, 4);
}
static void test_adding_one_already_there_changes_nothing() {
  uint8_t list[4] = { 5, 7, 0, 0 };
  TEST_ASSERT_TRUE(addToList(list, 4, 7));
  const uint8_t expected[] = { 5, 7, 0, 0 };
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, list, 4);
}
static void test_adding_to_a_full_list_fails() {
  uint8_t list[2] = { 5, 7 };
  TEST_ASSERT_FALSE(addToList(list, 2, 9));
}
static void test_remove_packs_the_rest_in_order() {
  uint8_t list[5] = { 1, 4, 3, 4, 0 };
  TEST_ASSERT_TRUE(removeFromList(list, 5, 4));
  const uint8_t expected[] = { 1, 3, 0, 0, 0 };
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, list, 5);
}
static void test_removing_one_not_there_reports_it() {
  uint8_t list[3] = { 1, 2, 0 };
  TEST_ASSERT_FALSE(removeFromList(list, 3, 9));
}
static void test_pack_closes_gaps() {
  uint8_t list[7] = { 1, 0, 4, 3, 0, 0, 6 };
  packList(list, 7);
  const uint8_t expected[] = { 1, 4, 3, 6, 0, 0, 0 };
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, list, 7);
}
static void test_repeater_addresses_use_the_same_rules() {
  uint32_t list[3] = { 0, 0, 0 };
  TEST_ASSERT_TRUE(addToList(list, 3, 0xABCDEFu));
  TEST_ASSERT_TRUE(addToList(list, 3, 0x123456u));
  TEST_ASSERT_TRUE(addToList(list, 3, 0xABCDEFu));
  TEST_ASSERT_TRUE(removeFromList(list, 3, 0xABCDEFu));
  TEST_ASSERT_EQUAL_UINT32(0x123456u, list[0]);
  TEST_ASSERT_EQUAL_UINT32(0, list[1]);
}

/*********************************************************************
 * GPIO pins
 ********************************************************************/
static std::set<uint8_t> radioPins, ethernetPins, shadePins;
static bool inSet(uint8_t pin, void *ctx) { return static_cast<std::set<uint8_t> *>(ctx)->count(pin) > 0; }
static pin_users_t users(std::set<uint8_t> &pins) {
  pin_users_t u;
  u.uses = inSet;
  u.ctx = &pins;
  return u;
}
static int8_t conflict(shade_types type, radio_proto proto, uint8_t up, uint8_t down, uint8_t my) {
  return gpioPinConflict(type, proto, up, down, my, users(radioPins), users(ethernetPins), users(shadePins));
}

static void test_a_radio_shade_uses_no_pins() {
  radioPins = { 4 };
  TEST_ASSERT_EQUAL_INT8(0, conflict(shade_types::roller, radio_proto::RTS, 4, 4, 4));
}
static void test_free_pins_are_fine() {
  radioPins = { 4 }; ethernetPins = { 18 }; shadePins = { 21 };
  TEST_ASSERT_EQUAL_INT8(0, conflict(shade_types::roller, radio_proto::GP_Remote, 25, 26, 27));
}
static void test_a_pin_the_radio_uses() {
  radioPins = { 26 }; ethernetPins = {}; shadePins = {};
  TEST_ASSERT_EQUAL_INT8(PIN_CONFLICT_RADIO, conflict(shade_types::roller, radio_proto::GP_Remote, 25, 26, 27));
}
static void test_every_pin_is_checked_against_ethernet() {
  // It used to check only the up pin against Ethernet.
  radioPins = {}; shadePins = {};
  ethernetPins = { 26 };
  TEST_ASSERT_EQUAL_INT8(PIN_CONFLICT_ETHERNET, conflict(shade_types::roller, radio_proto::GP_Remote, 25, 26, 27));
  ethernetPins = { 27 };
  TEST_ASSERT_EQUAL_INT8(PIN_CONFLICT_ETHERNET, conflict(shade_types::roller, radio_proto::GP_Remote, 25, 26, 27));
}
static void test_a_pin_another_shade_uses() {
  radioPins = {}; ethernetPins = {};
  shadePins = { 25 };
  TEST_ASSERT_EQUAL_INT8(PIN_CONFLICT_SHADE, conflict(shade_types::roller, radio_proto::GP_Remote, 25, 26, 27));
}
static void test_only_the_pins_a_shade_drives_are_checked() {
  radioPins = {}; ethernetPins = {};
  shadePins = { 25, 27 };
  // A relay never drives My.
  TEST_ASSERT_EQUAL_INT8(PIN_CONFLICT_SHADE, conflict(shade_types::roller, radio_proto::GP_Relay, 25, 26, 27));
  TEST_ASSERT_EQUAL_INT8(0, conflict(shade_types::roller, radio_proto::GP_Relay, 24, 26, 27));
  // A dry contact only drives down; a two way dry contact up and down.
  TEST_ASSERT_EQUAL_INT8(0, conflict(shade_types::drycontact, radio_proto::GP_Remote, 25, 26, 27));
  TEST_ASSERT_EQUAL_INT8(0, conflict(shade_types::drycontact2, radio_proto::GP_Remote, 24, 26, 27));
  // A single button gate on a remote only drives down.
  TEST_ASSERT_EQUAL_INT8(0, conflict(shade_types::garage1, radio_proto::GP_Remote, 25, 26, 27));
}
static void test_255_means_no_pin() {
  radioPins = { 255 }; ethernetPins = { 255 }; shadePins = { 255 };
  TEST_ASSERT_EQUAL_INT8(0, conflict(shade_types::roller, radio_proto::GP_Remote, 255, 26, 255));
}

/*********************************************************************
 * What protocol 2 clients are sent after an edit
 ********************************************************************/
/*********************************************************************
 * Results
 ********************************************************************/
static void test_a_missing_id_is_not_found() {
  const edit_result_t r = edit_result_t::missing("Shade Id not found.");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_TRUE(r.notFound);
  TEST_ASSERT_EQUAL_STRING("Shade Id not found.", r.desc);
}
static void test_any_other_refusal_is_not_not_found() {
  const edit_result_t r = edit_result_t::error("No shade id was supplied.", 3);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_FALSE(r.notFound);
  TEST_ASSERT_EQUAL_INT8(3, r.code);
  TEST_ASSERT_FALSE(edit_result_t().notFound);
}

static edit_result_t resultFor(uint8_t id, uint8_t shadeId = 0) {
  edit_result_t r;
  r.id = id;
  r.shadeId = shadeId;
  return r;
}
static void test_a_shade_edit_sends_the_whole_shade() {
  const model_edit_type_t types[] = {
    model_edit_type_t::add_shade, model_edit_type_t::save_shade, model_edit_type_t::set_paired,
    model_edit_type_t::link_remote, model_edit_type_t::unlink_remote
  };
  for(model_edit_type_t t : types) {
    const edit_changes_t c = editChanges(t, resultFor(7));
    TEST_ASSERT_EQUAL_UINT8(7, c.shadeId);
    TEST_ASSERT_EQUAL_UINT8(0, c.groupId);
    TEST_ASSERT_EQUAL_UINT8(0, c.roomId);
  }
}
static void test_a_group_edit_sends_the_whole_group() {
  TEST_ASSERT_EQUAL_UINT8(3, editChanges(model_edit_type_t::add_group, resultFor(3)).groupId);
  TEST_ASSERT_EQUAL_UINT8(3, editChanges(model_edit_type_t::save_group, resultFor(3)).groupId);
}
static void test_linking_a_shade_to_a_group_sends_both() {
  // The shade's inGroup changes with the group's shades.
  for(model_edit_type_t t : {model_edit_type_t::link_to_group, model_edit_type_t::unlink_from_group}) {
    const edit_changes_t c = editChanges(t, resultFor(3, 9));
    TEST_ASSERT_EQUAL_UINT8(3, c.groupId);
    TEST_ASSERT_EQUAL_UINT8(9, c.shadeId);
  }
}
static void test_a_room_edit_sends_the_room() {
  TEST_ASSERT_EQUAL_UINT8(2, editChanges(model_edit_type_t::add_room, resultFor(2)).roomId);
  TEST_ASSERT_EQUAL_UINT8(2, editChanges(model_edit_type_t::save_room, resultFor(2)).roomId);
}
static void test_a_sort_edit_sends_its_list() {
  TEST_ASSERT_EQUAL(sort_list_t::rooms, editChanges(model_edit_type_t::room_order, resultFor(0)).order);
  TEST_ASSERT_EQUAL(sort_list_t::shades, editChanges(model_edit_type_t::shade_order, resultFor(0)).order);
  TEST_ASSERT_EQUAL(sort_list_t::groups, editChanges(model_edit_type_t::group_order, resultFor(0)).order);
  TEST_ASSERT_EQUAL(sort_list_t::none, editChanges(model_edit_type_t::save_shade, resultFor(1)).order);
}
static void test_a_repeater_edit_sends_the_repeaters() {
  TEST_ASSERT_TRUE(editChanges(model_edit_type_t::link_repeater, resultFor(0)).repeaters);
  TEST_ASSERT_TRUE(editChanges(model_edit_type_t::unlink_repeater, resultFor(0)).repeaters);
  TEST_ASSERT_FALSE(editChanges(model_edit_type_t::save_room, resultFor(1)).repeaters);
}
static void test_deletes_send_their_removal() {
  // After the reply, as every edit's events are, so a client waiting for the
  // reply doesn't read past it.
  const edit_changes_t room = editChanges(model_edit_type_t::delete_room, resultFor(4));
  const edit_changes_t shade = editChanges(model_edit_type_t::delete_shade, resultFor(5));
  const edit_changes_t group = editChanges(model_edit_type_t::delete_group, resultFor(6));
  TEST_ASSERT_EQUAL_UINT8(4, room.removedRoomId);
  TEST_ASSERT_EQUAL_UINT8(5, shade.removedShadeId);
  TEST_ASSERT_EQUAL_UINT8(6, group.removedGroupId);
  for(const edit_changes_t &c : {room, shade, group}) {
    // Nothing is left of it to send whole.
    TEST_ASSERT_EQUAL_UINT8(0, c.shadeId);
    TEST_ASSERT_EQUAL_UINT8(0, c.groupId);
    TEST_ASSERT_EQUAL_UINT8(0, c.roomId);
    TEST_ASSERT_EQUAL(sort_list_t::none, c.order);
    TEST_ASSERT_FALSE(c.repeaters);
  }
  TEST_ASSERT_EQUAL_UINT8(0, room.removedShadeId + room.removedGroupId);
  TEST_ASSERT_EQUAL_UINT8(0, shade.removedRoomId + shade.removedGroupId);
  TEST_ASSERT_EQUAL_UINT8(0, group.removedRoomId + group.removedShadeId);
}
static void test_other_edits_remove_nothing() {
  const edit_changes_t c = editChanges(model_edit_type_t::save_room, resultFor(4));
  TEST_ASSERT_EQUAL_UINT8(0, c.removedRoomId + c.removedShadeId + c.removedGroupId);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_first_free_id_fills_gaps);
  RUN_TEST(test_first_free_id_of_an_empty_list_is_1);
  RUN_TEST(test_first_free_id_is_0_when_every_one_is_taken);
  RUN_TEST(test_next_sort_order_goes_after_the_last);
  RUN_TEST(test_free_remote_address_skips_every_taken_one);
  RUN_TEST(test_a_new_one_is_offered_the_starting_address_plus_its_id);
  RUN_TEST(test_a_missing_id_is_not_found);
  RUN_TEST(test_any_other_refusal_is_not_not_found);
  RUN_TEST(test_add_goes_into_the_first_empty_slot);
  RUN_TEST(test_adding_one_already_there_changes_nothing);
  RUN_TEST(test_adding_to_a_full_list_fails);
  RUN_TEST(test_remove_packs_the_rest_in_order);
  RUN_TEST(test_removing_one_not_there_reports_it);
  RUN_TEST(test_pack_closes_gaps);
  RUN_TEST(test_repeater_addresses_use_the_same_rules);
  RUN_TEST(test_a_radio_shade_uses_no_pins);
  RUN_TEST(test_free_pins_are_fine);
  RUN_TEST(test_a_pin_the_radio_uses);
  RUN_TEST(test_every_pin_is_checked_against_ethernet);
  RUN_TEST(test_a_pin_another_shade_uses);
  RUN_TEST(test_only_the_pins_a_shade_drives_are_checked);
  RUN_TEST(test_255_means_no_pin);
  RUN_TEST(test_a_shade_edit_sends_the_whole_shade);
  RUN_TEST(test_a_group_edit_sends_the_whole_group);
  RUN_TEST(test_linking_a_shade_to_a_group_sends_both);
  RUN_TEST(test_a_room_edit_sends_the_room);
  RUN_TEST(test_a_sort_edit_sends_its_list);
  RUN_TEST(test_a_repeater_edit_sends_the_repeaters);
  RUN_TEST(test_deletes_send_their_removal);
  RUN_TEST(test_other_edits_remove_nothing);
  return UNITY_END();
}
