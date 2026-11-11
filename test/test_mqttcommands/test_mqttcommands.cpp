#include <unity.h>
#include <string.h>
#include <string>
#include "entryPoints/MqttCommands.h"

static shade_request_t req;

static bool parse(const char *topic, const char *value) {
  req = shade_request_t();
  return parseMqttCommand(topic, reinterpret_cast<const uint8_t *>(value), strlen(value), req);
}
static void assertType(shade_request_type_t expected) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(req.type));
}
static void assertCommand(somfy_commands expected) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(req.command));
}

void setUp() {}
void tearDown() {}

/*********************************************************************
 * Topics
 ********************************************************************/
static void test_the_last_four_segments_name_the_command() {
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/target/set", "30"));
  assertType(shade_request_type_t::shade_command);
  TEST_ASSERT_EQUAL_UINT8(4, req.id);
  TEST_ASSERT_EQUAL_INT8(30, req.target);
}
static void test_the_root_topic_may_be_empty_or_have_slashes() {
  TEST_ASSERT_TRUE(parse("shades/12/target/set", "30"));
  TEST_ASSERT_EQUAL_UINT8(12, req.id);
  TEST_ASSERT_TRUE(parse("home/somfy/rts/shades/255/target/set", "30"));
  TEST_ASSERT_EQUAL_UINT8(255, req.id);
}
static void test_topics_it_does_not_take() {
  TEST_ASSERT_FALSE(parse("espsomfy/shades/4/target", "30"));        // not a set topic
  TEST_ASSERT_FALSE(parse("espsomfy/shades/4/target/get", "30"));
  TEST_ASSERT_FALSE(parse("espsomfy/rooms/4/target/set", "30"));     // no such entity
  TEST_ASSERT_FALSE(parse("espsomfy/shades/4/name/set", "30"));      // no such command
  TEST_ASSERT_FALSE(parse("espsomfy/shades/x4/target/set", "30"));   // not an id
  TEST_ASSERT_FALSE(parse("espsomfy/shades/256/target/set", "30"));
  TEST_ASSERT_FALSE(parse("espsomfy/shades//target/set", "30"));
  TEST_ASSERT_FALSE(parse("target/set", "30"));
  TEST_ASSERT_FALSE(parse("", "30"));
}
static void test_the_payload_need_not_be_terminated() {
  const char payload[] = { '4', '2', 'x', 'y' };
  req = shade_request_t();
  TEST_ASSERT_TRUE(parseMqttCommand("espsomfy/shades/4/target/set", reinterpret_cast<const uint8_t *>(payload), 2, req));
  TEST_ASSERT_EQUAL_INT8(42, req.target);
}

/*********************************************************************
 * Shades
 ********************************************************************/
static void test_targets_must_be_in_range() {
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/target/set", "100"));
  TEST_ASSERT_FALSE(parse("espsomfy/shades/4/target/set", "101"));
  TEST_ASSERT_FALSE(parse("espsomfy/shades/4/target/set", "-1"));
  TEST_ASSERT_FALSE(parse("espsomfy/shades/4/tiltTarget/set", "101"));
}
static void test_tilt_target() {
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/tiltTarget/set", "30"));
  assertType(shade_request_type_t::tilt_command);
  TEST_ASSERT_EQUAL_INT8(30, req.target);
}
static void test_direction_sends_up_down_or_my() {
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/direction/set", "-1"));
  assertType(shade_request_type_t::shade_command);
  TEST_ASSERT_EQUAL_INT8(-1, req.target);
  assertCommand(somfy_commands::Up);
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/direction/set", "1"));
  assertCommand(somfy_commands::Down);
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/direction/set", "0"));
  assertCommand(somfy_commands::My);
}
static void test_favorite_lift_and_tilt() {
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/mypos/set", "30"));
  assertType(shade_request_type_t::set_favorite);
  TEST_ASSERT_EQUAL_INT8(30, req.position);
  TEST_ASSERT_EQUAL_INT8(-1, req.tiltPosition);
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/myTiltPos/set", "60"));
  assertType(shade_request_type_t::set_favorite);
  TEST_ASSERT_EQUAL_INT8(-1, req.position);
  TEST_ASSERT_EQUAL_INT8(60, req.tiltPosition);
  TEST_ASSERT_FALSE(parse("espsomfy/shades/4/mypos/set", "120"));
}
static void test_sun_flag() {
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/sunFlag/set", "1"));
  assertType(shade_request_type_t::shade_command);
  assertCommand(somfy_commands::SunFlag);
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/sunFlag/set", "0"));
  assertCommand(somfy_commands::Flag);
}
static void test_sunny_and_windy_leave_the_other_alone() {
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/sunny/set", "1"));
  assertType(shade_request_type_t::shade_sensor);
  TEST_ASSERT_EQUAL_INT8(1, req.sunny);
  TEST_ASSERT_EQUAL_INT8(-1, req.windy);
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/windy/set", "0"));
  TEST_ASSERT_EQUAL_INT8(-1, req.sunny);
  TEST_ASSERT_EQUAL_INT8(0, req.windy);
  TEST_ASSERT_FALSE(parse("espsomfy/shades/4/windy/set", "-1"));
}
static void test_position_and_tilt_position_correct_the_shade() {
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/position/set", "30"));
  assertType(shade_request_type_t::set_positions);
  TEST_ASSERT_EQUAL_INT8(30, req.position);
  TEST_ASSERT_EQUAL_INT8(-1, req.tiltPosition);
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/tiltPosition/set", "60"));
  TEST_ASSERT_EQUAL_INT8(-1, req.position);
  TEST_ASSERT_EQUAL_INT8(60, req.tiltPosition);
}
static void test_shade_requests_use_the_default_repeats() {
  TEST_ASSERT_TRUE(parse("espsomfy/shades/4/direction/set", "1"));
  TEST_ASSERT_EQUAL_INT8(-1, req.repeat);
}

/*********************************************************************
 * Groups
 ********************************************************************/
static void test_group_direction_and_sun_flag() {
  TEST_ASSERT_TRUE(parse("espsomfy/groups/2/direction/set", "-1"));
  assertType(shade_request_type_t::group_command);
  TEST_ASSERT_EQUAL_UINT8(2, req.id);
  assertCommand(somfy_commands::Up);
  TEST_ASSERT_TRUE(parse("espsomfy/groups/2/sunFlag/set", "1"));
  assertType(shade_request_type_t::group_command);
  assertCommand(somfy_commands::SunFlag);
}
static void test_group_sunny_and_windy() {
  TEST_ASSERT_TRUE(parse("espsomfy/groups/2/sunny/set", "0"));
  assertType(shade_request_type_t::group_sensor);
  TEST_ASSERT_EQUAL_INT8(0, req.sunny);
  TEST_ASSERT_EQUAL_INT8(-1, req.windy);
  TEST_ASSERT_TRUE(parse("espsomfy/groups/2/windy/set", "1"));
  TEST_ASSERT_EQUAL_INT8(1, req.windy);
}
static void test_every_subscribed_topic_is_understood() {
  // What the connection subscribes to and what the parser takes can't drift.
  for(const char *pattern : MQTT_COMMAND_TOPICS) {
    std::string topic = std::string("espsomfy/") + pattern;
    topic.replace(topic.find('+'), 1, "3");
    TEST_ASSERT_TRUE_MESSAGE(parse(topic.c_str(), "1"), topic.c_str());
  }
}
static void test_groups_only_take_their_commands() {
  TEST_ASSERT_FALSE(parse("espsomfy/groups/2/target/set", "30"));
  TEST_ASSERT_FALSE(parse("espsomfy/groups/2/mypos/set", "30"));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_the_last_four_segments_name_the_command);
  RUN_TEST(test_the_root_topic_may_be_empty_or_have_slashes);
  RUN_TEST(test_topics_it_does_not_take);
  RUN_TEST(test_the_payload_need_not_be_terminated);
  RUN_TEST(test_targets_must_be_in_range);
  RUN_TEST(test_tilt_target);
  RUN_TEST(test_direction_sends_up_down_or_my);
  RUN_TEST(test_favorite_lift_and_tilt);
  RUN_TEST(test_sun_flag);
  RUN_TEST(test_sunny_and_windy_leave_the_other_alone);
  RUN_TEST(test_position_and_tilt_position_correct_the_shade);
  RUN_TEST(test_shade_requests_use_the_default_repeats);
  RUN_TEST(test_group_direction_and_sun_flag);
  RUN_TEST(test_group_sunny_and_windy);
  RUN_TEST(test_groups_only_take_their_commands);
  RUN_TEST(test_every_subscribed_topic_is_understood);
  return UNITY_END();
}
