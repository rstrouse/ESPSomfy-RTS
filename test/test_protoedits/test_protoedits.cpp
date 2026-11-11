#include <unity.h>
#include <string>
#include <ArduinoJson.h>
#include "ProtoEdits.h"

// Socket protocol 2's edit ops: which edit each is, and the edit route's
// request that its payload becomes (docs/PROTOCOL.md).

void setUp() {}
void tearDown() {}

static JsonDocument payload;
static JsonDocument doc;

static const char *expand(model_edit_type_t type, const char *json) {
  payload.clear();
  TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(payload, json).code());
  return expandEditPayload(type, payload.as<JsonObjectConst>(), doc);
}
static std::string written() {
  std::string out;
  serializeJson(doc, out);
  return out;
}
#define TEST_ASSERT_GOOD(why) TEST_ASSERT_NULL_MESSAGE(why, why)

/*********************************************************************
 * Which edit an op is
 ********************************************************************/
static void test_every_edit_op_is_an_edit() {
  model_edit_type_t t;
  TEST_ASSERT_TRUE(protoEditType(static_cast<uint8_t>(proto_type_t::addShade), t));
  TEST_ASSERT_EQUAL(model_edit_type_t::add_shade, t);
  TEST_ASSERT_TRUE(protoEditType(static_cast<uint8_t>(proto_type_t::groupSortOrder), t));
  TEST_ASSERT_EQUAL(model_edit_type_t::group_order, t);
  TEST_ASSERT_TRUE(protoEditType(static_cast<uint8_t>(proto_type_t::setPaired), t));
  TEST_ASSERT_EQUAL(model_edit_type_t::set_paired, t);
  // One op for each edit.
  int count = 0;
  for(int type = 16; type <= 127; type++)
    if(protoEditType(static_cast<uint8_t>(type), t)) count++;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(model_edit_type_t::group_order) + 1, count);
}
static void test_other_types_are_not_edits() {
  model_edit_type_t t;
  TEST_ASSERT_FALSE(protoEditType(static_cast<uint8_t>(proto_type_t::shadeCommand), t));
  TEST_ASSERT_FALSE(protoEditType(static_cast<uint8_t>(proto_type_t::getNextShade), t));
  TEST_ASSERT_FALSE(protoEditType(static_cast<uint8_t>(proto_type_t::shade), t));
}
static void test_the_edit_ops_are_the_schemas_edit_ops() {
  // The TYPE table marks the edit ops; each needs its edit here, and only they
  // have one.
  model_edit_type_t t;
  for(int type = 0; type <= 255; type++) {
    const bool edit = protoOpKind(static_cast<uint8_t>(type)) == proto_op_t::edit;
    TEST_ASSERT_EQUAL_MESSAGE(edit, protoEditType(static_cast<uint8_t>(type), t), protoTypeName(static_cast<uint8_t>(type)));
  }
}

/*********************************************************************
 * Reads
 ********************************************************************/
static uint8_t groupId;
static const char *readPayload(proto_type_t type, const char *json) {
  payload.clear();
  TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(payload, json).code());
  return parseReadPayload(static_cast<uint8_t>(type), payload.as<JsonObjectConst>(), groupId);
}
static void test_group_options_reads_its_group() {
  TEST_ASSERT_GOOD(readPayload(proto_type_t::groupOptions, R"({"g":4})"));
  TEST_ASSERT_EQUAL_UINT8(4, groupId);
  TEST_ASSERT_EQUAL_STRING("groupId is missing", readPayload(proto_type_t::groupOptions, R"({})"));
  TEST_ASSERT_EQUAL_STRING("groupId must be 0-255", readPayload(proto_type_t::groupOptions, R"({"g":300})"));
  TEST_ASSERT_EQUAL_STRING("groupId must be 0-255", readPayload(proto_type_t::groupOptions, R"({"g":"4"})"));
}
static void test_the_other_reads_need_nothing() {
  TEST_ASSERT_GOOD(readPayload(proto_type_t::getNextShade, R"({})"));
  TEST_ASSERT_EQUAL_UINT8(0, groupId);
  TEST_ASSERT_GOOD(readPayload(proto_type_t::getNextRoom, R"({})"));
  TEST_ASSERT_GOOD(readPayload(proto_type_t::getNextGroup, R"({})"));
}

/*********************************************************************
 * The payload as the edit route's request
 ********************************************************************/
static void test_short_keys_become_the_route_names() {
  TEST_ASSERT_GOOD(expand(model_edit_type_t::save_shade, R"({"s":3,"n":"Office","U":12000,"a":1193046})"));
  TEST_ASSERT_EQUAL_STRING(R"({"shadeId":3,"name":"Office","upTime":12000,"remoteAddress":1193046})", written().c_str());
}
static void test_booleans_become_true_and_false() {
  // set_paired reads paired | false, which takes only a boolean.
  TEST_ASSERT_GOOD(expand(model_edit_type_t::set_paired, R"({"s":3,"i":1})"));
  TEST_ASSERT_EQUAL_STRING(R"({"shadeId":3,"paired":true})", written().c_str());
  TEST_ASSERT_GOOD(expand(model_edit_type_t::save_shade, R"({"s":3,"fp":0})"));
  TEST_ASSERT_EQUAL_STRING(R"({"shadeId":3,"flipPosition":false})", written().c_str());
}
static void test_only_what_was_sent_is_in_the_request() {
  TEST_ASSERT_GOOD(expand(model_edit_type_t::save_room, R"({"r":2})"));
  TEST_ASSERT_EQUAL_STRING(R"({"roomId":2})", written().c_str());
  TEST_ASSERT_GOOD(expand(model_edit_type_t::add_room, R"({})"));
  TEST_ASSERT_EQUAL_STRING("{}", written().c_str());
}
static void test_a_sort_order_is_its_list_of_ids() {
  TEST_ASSERT_GOOD(expand(model_edit_type_t::shade_order, R"({"is":[3,1,2]})"));
  TEST_ASSERT_EQUAL_STRING("[3,1,2]", written().c_str());
  TEST_ASSERT_EQUAL_STRING("ids is missing", expand(model_edit_type_t::room_order, R"({})"));
  TEST_ASSERT_EQUAL_STRING("ids must be a list of ids", expand(model_edit_type_t::group_order, R"({"is":[1,"x"]})"));
  TEST_ASSERT_EQUAL_STRING("ids must be a list of ids", expand(model_edit_type_t::group_order, R"({"is":[1,300]})"));
}
static void test_values_are_checked_against_their_kind() {
  TEST_ASSERT_EQUAL_STRING("paired is not a boolean (1 or 0)", expand(model_edit_type_t::set_paired, R"({"s":3,"i":true})"));
  TEST_ASSERT_EQUAL_STRING("paired is not a boolean (1 or 0)", expand(model_edit_type_t::set_paired, R"({"s":3,"i":2})"));
  TEST_ASSERT_EQUAL_STRING("shadeId is not a whole number", expand(model_edit_type_t::delete_shade, R"({"s":"3"})"));
  TEST_ASSERT_EQUAL_STRING("name is not a string", expand(model_edit_type_t::save_room, R"({"r":1,"n":5})"));
}
static void test_an_unknown_key_is_refused() {
  TEST_ASSERT_EQUAL_STRING("Unknown field zz", expand(model_edit_type_t::save_room, R"({"r":1,"zz":5})"));
}
static void test_a_field_the_route_would_ignore_is_refused() {
  // In the sync, but not something a save sets.
  TEST_ASSERT_EQUAL_STRING("linkedRemotes can't be set by this edit", expand(model_edit_type_t::save_shade, R"({"s":3,"lk":[1]})"));
  TEST_ASSERT_EQUAL_STRING("paired can't be set by this edit", expand(model_edit_type_t::save_shade, R"({"s":3,"i":1})"));
  TEST_ASSERT_EQUAL_STRING("shadeId can't be set by this edit", expand(model_edit_type_t::add_shade, R"({"s":3})"));
  TEST_ASSERT_EQUAL_STRING("upTime can't be set by this edit", expand(model_edit_type_t::save_group, R"({"g":1,"U":5})"));
  TEST_ASSERT_EQUAL_STRING("name can't be set by this edit", expand(model_edit_type_t::delete_room, R"({"r":1,"n":"x"})"));
  // light is its own field, and setSensor sets the sun and wind.
  TEST_ASSERT_EQUAL_STRING("flags can't be set by this edit", expand(model_edit_type_t::save_shade, R"({"s":3,"f":1})"));
  // Worked out from the group's shades.
  TEST_ASSERT_EQUAL_STRING("sunSensor can't be set by this edit", expand(model_edit_type_t::save_group, R"({"g":1,"ss":1})"));
}
static void test_set_paired_needs_paired() {
  // Left out is not given, so it doesn't unpair the shade.
  TEST_ASSERT_EQUAL_STRING("paired is missing", expand(model_edit_type_t::set_paired, R"({"s":3})"));
}
static void test_link_remote_takes_the_routes_rolling_code() {
  TEST_ASSERT_GOOD(expand(model_edit_type_t::link_remote, R"({"s":3,"a":1193046,"c":12})"));
  TEST_ASSERT_EQUAL_STRING(R"({"shadeId":3,"remoteAddress":1193046,"rollingCode":12})", written().c_str());
  TEST_ASSERT_EQUAL_STRING("lastRollingCode can't be set by this edit", expand(model_edit_type_t::unlink_remote, R"({"s":3,"a":5,"c":12})"));
}
static void test_a_sort_order_takes_only_ids() {
  TEST_ASSERT_EQUAL_STRING("A sort order takes only ids", expand(model_edit_type_t::room_order, R"({"is":[1],"r":2})"));
}
static void test_a_shade_save_takes_what_the_form_sends() {
  TEST_ASSERT_GOOD(expand(model_edit_type_t::save_shade,
    R"({"s":3,"n":"Office","r":1,"a":5,"st":1,"y":2,"bl":80,"pr":1,"U":1,"D":2,"Y":3,"z":5,"fc":1,"fp":0,"ss":1,"li":0,"sm":1,"rp":2,"gu":4,"gd":5,"gm":6,"gl":1})"));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_every_edit_op_is_an_edit);
  RUN_TEST(test_other_types_are_not_edits);
  RUN_TEST(test_the_edit_ops_are_the_schemas_edit_ops);
  RUN_TEST(test_group_options_reads_its_group);
  RUN_TEST(test_the_other_reads_need_nothing);
  RUN_TEST(test_short_keys_become_the_route_names);
  RUN_TEST(test_booleans_become_true_and_false);
  RUN_TEST(test_only_what_was_sent_is_in_the_request);
  RUN_TEST(test_a_sort_order_is_its_list_of_ids);
  RUN_TEST(test_values_are_checked_against_their_kind);
  RUN_TEST(test_an_unknown_key_is_refused);
  RUN_TEST(test_a_field_the_route_would_ignore_is_refused);
  RUN_TEST(test_set_paired_needs_paired);
  RUN_TEST(test_link_remote_takes_the_routes_rolling_code);
  RUN_TEST(test_a_sort_order_takes_only_ids);
  RUN_TEST(test_a_shade_save_takes_what_the_form_sends);
  return UNITY_END();
}
