#include <unity.h>
#include "ShadeSettings.h"

static const uint8_t SUN_FLAG = static_cast<uint8_t>(somfy_flags_t::SunFlag);
static const uint8_t SUN_SENSOR = static_cast<uint8_t>(somfy_flags_t::SunSensor);
static const uint8_t DEMO_MODE = static_cast<uint8_t>(somfy_flags_t::DemoMode);
static const uint8_t LIGHT = static_cast<uint8_t>(somfy_flags_t::Light);
static const uint8_t WINDY = static_cast<uint8_t>(somfy_flags_t::Windy);
static const uint8_t SUNNY = static_cast<uint8_t>(somfy_flags_t::Sunny);
static const uint8_t LIGHTED = static_cast<uint8_t>(somfy_flags_t::Lighted);
static const uint8_t SIM_MY = static_cast<uint8_t>(somfy_flags_t::SimMy);

void setUp() {}
void tearDown() {}

static void test_every_byte_comes_back_unchanged() {
  // The config file and JSON hold the whole byte, so splitting it and putting
  // it back together must not lose or move a bit.
  for(int flags = 0; flags <= 0xFF; flags++) {
    ShadeSettings settings;
    bool light = false;
    uint8_t state = 0;
    splitShadeFlags(static_cast<uint8_t>(flags), settings, light, state);
    TEST_ASSERT_EQUAL_HEX8(flags, joinShadeFlags(settings, light, state));
  }
}
static void test_setting_bits_go_to_the_settings() {
  ShadeSettings settings;
  bool light = false;
  uint8_t state = 0xFF;
  splitShadeFlags(SUN_SENSOR | LIGHT | SIM_MY, settings, light, state);
  TEST_ASSERT_TRUE(settings.sunSensor);
  TEST_ASSERT_TRUE(light);
  TEST_ASSERT_TRUE(settings.simMy);
  TEST_ASSERT_EQUAL_HEX8(0, state);
}
static void test_state_bits_stay_state() {
  ShadeSettings settings;
  settings.sunSensor = settings.simMy = true;
  bool light = true;
  uint8_t state = 0;
  const uint8_t stateBits = SUN_FLAG | DEMO_MODE | WINDY | SUNNY | LIGHTED;
  splitShadeFlags(stateBits, settings, light, state);
  TEST_ASSERT_FALSE(settings.sunSensor);
  TEST_ASSERT_FALSE(light);
  TEST_ASSERT_FALSE(settings.simMy);
  TEST_ASSERT_EQUAL_HEX8(stateBits, state);
}
static void test_join_ignores_setting_bits_in_the_state() {
  // The settings decide those bits, whatever the state byte holds.
  ShadeSettings settings;
  TEST_ASSERT_EQUAL_HEX8(WINDY, joinShadeFlags(settings, false, WINDY | SUN_SENSOR | LIGHT | SIM_MY));
  settings.simMy = true;
  TEST_ASSERT_EQUAL_HEX8(SIM_MY | SUNNY, joinShadeFlags(settings, false, SUNNY));
}
static void test_defaults() {
  ShadeSettings shade;
  TEST_ASSERT_EQUAL_HEX8(0, joinShadeFlags(shade, false, 0));
  TEST_ASSERT_EQUAL_UINT32(10000, shade.upTime);
  TEST_ASSERT_EQUAL_UINT32(10000, shade.downTime);
  TEST_ASSERT_EQUAL_UINT32(7000, shade.tiltTime);
  TEST_ASSERT_EQUAL_UINT16(100, shade.stepSize);
  RemoteSettings remote;
  TEST_ASSERT_FALSE(remote.flipCommands);
  TEST_ASSERT_EQUAL_UINT8(0, remote.bitLength);
  TEST_ASSERT_EQUAL_UINT8(1, remote.repeats);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_every_byte_comes_back_unchanged);
  RUN_TEST(test_setting_bits_go_to_the_settings);
  RUN_TEST(test_state_bits_stay_state);
  RUN_TEST(test_join_ignores_setting_bits_in_the_state);
  RUN_TEST(test_defaults);
  return UNITY_END();
}
