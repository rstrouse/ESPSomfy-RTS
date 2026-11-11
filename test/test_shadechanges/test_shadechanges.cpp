#include <unity.h>
#include <string>
#include "ShadeChanges.h"

static std::string published;
static void record(const char *topic, int32_t value, bool retain, void *ctx) {
  (void)ctx;
  if(!published.empty()) published += " ";
  published += std::string(topic) + "=" + std::to_string(value) + (retain ? "R" : "");
}
static std::string topics(uint16_t changed, const shade_snapshot_t &shade) {
  published.clear();
  publishShadeTopics(changed, shade, record, nullptr);
  return published;
}
// A shade as the snapshot shows it to clients.
static shade_snapshot_t shade() {
  shade_snapshot_t s{};
  s.shadeId = 4;
  s.position = 30;
  s.target = 70;
  s.direction = 1;
  s.myPos = -1;
  s.myTiltPos = -1;
  s.tiltType = static_cast<uint8_t>(tilt_types::none);
  s.tiltPosition = 40;
  s.tiltTarget = 45;
  s.tiltDirection = -1;
  s.lastRollingCode = 1234;
  return s;
}

void setUp() {}
void tearDown() {}

static void test_nothing_changed_publishes_nothing() {
  TEST_ASSERT_EQUAL_STRING("", topics(0, shade()).c_str());
  TEST_ASSERT_EQUAL_STRING("", topics(SHADE_CHANGED_STATE, shade()).c_str());
}
static void test_position_and_target_are_the_shades_own() {
  // Both used to go out as a constant 50.
  TEST_ASSERT_EQUAL_STRING("position=30R", topics(SHADE_CHANGED_POSITION, shade()).c_str());
  TEST_ASSERT_EQUAL_STRING("target=70R", topics(SHADE_CHANGED_TARGET, shade()).c_str());
}
static void test_only_the_changed_topics_go_out() {
  TEST_ASSERT_EQUAL_STRING("direction=1R lastRollingCode=1234",
    topics(SHADE_CHANGED_DIRECTION | SHADE_CHANGED_ROLLING_CODE, shade()).c_str());
}
static void test_an_unset_favorite_is_minus_1() {
  TEST_ASSERT_EQUAL_STRING("mypos=-1R myTiltPos=-1R", topics(SHADE_CHANGED_MY_POS | SHADE_CHANGED_MY_TILT_POS, shade()).c_str());
}
static void test_tilt_topics() {
  TEST_ASSERT_EQUAL_STRING("tiltDirection=-1R tiltPosition=40R tiltTarget=45R",
    topics(SHADE_CHANGED_TILT_DIRECTION | SHADE_CHANGED_TILT_POSITION | SHADE_CHANGED_TILT_TARGET, shade()).c_str());
}
static void test_sun_and_wind_are_0_or_1() {
  shade_snapshot_t s = shade();
  s.flags = static_cast<uint8_t>(somfy_flags_t::SunFlag) | static_cast<uint8_t>(somfy_flags_t::Windy);
  TEST_ASSERT_EQUAL_STRING("sunFlag=1 sunny=0 windy=1",
    topics(SHADE_CHANGED_SUN_FLAG | SHADE_CHANGED_SUNNY | SHADE_CHANGED_WINDY, s).c_str());
}
static void test_full_state_of_a_plain_shade() {
  // Direction used to go out as a constant 0 when MQTT connected.
  TEST_ASSERT_EQUAL_STRING("position=30R direction=1R target=70R lastRollingCode=1234 mypos=-1R myTiltPos=-1R windy=0",
    topics(shadeStateTopics(shade()), shade()).c_str());
}
static void test_full_state_adds_tilt_and_sun_when_the_shade_has_them() {
  shade_snapshot_t s = shade();
  s.tiltType = static_cast<uint8_t>(tilt_types::integrated);
  s.sunSensor = true;
  const uint16_t state = shadeStateTopics(s);
  TEST_ASSERT_TRUE(state & SHADE_CHANGED_TILT_POSITION);
  TEST_ASSERT_TRUE(state & SHADE_CHANGED_SUNNY);
  TEST_ASSERT_FALSE(state & SHADE_CHANGED_STATE);
  TEST_ASSERT_EQUAL_UINT16(SHADE_CHANGED_TOPICS, state);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_nothing_changed_publishes_nothing);
  RUN_TEST(test_position_and_target_are_the_shades_own);
  RUN_TEST(test_only_the_changed_topics_go_out);
  RUN_TEST(test_an_unset_favorite_is_minus_1);
  RUN_TEST(test_tilt_topics);
  RUN_TEST(test_sun_and_wind_are_0_or_1);
  RUN_TEST(test_full_state_of_a_plain_shade);
  RUN_TEST(test_full_state_adds_tilt_and_sun_when_the_shade_has_them);
  return UNITY_END();
}
