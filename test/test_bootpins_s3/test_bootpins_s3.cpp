#include <unity.h>
#include <string.h>
// As on the ESP32-S3, where GPIO45 picks the flash voltage.
#define CONFIG_IDF_TARGET_ESP32S3 1
#include "BootPins.h"

void setUp() {}
void tearDown() {}

static void test_gpio45_picks_the_flash_voltage() {
  const char *effect = bootPinEffect(45);
  TEST_ASSERT_NOT_NULL(effect);
  TEST_ASSERT_NOT_NULL(strstr(effect, "1.8 V"));
}

static void test_gpio0_picks_the_boot_mode() {
  TEST_ASSERT_NOT_NULL(bootPinEffect(0));
}

static void test_gpio46_blocks_flashing_over_usb() {
  const char *effect = bootPinEffect(46);
  TEST_ASSERT_NOT_NULL(effect);
  TEST_ASSERT_NOT_NULL(strstr(effect, "USB"));
}

static void test_gpio12_is_safe() {
  // 12 is the S3's default SCK.
  TEST_ASSERT_NULL(bootPinEffect(12));
}

static void test_default_radio_pins_are_safe() {
  // transceiver_config_t::load()'s defaults for the S3.
  const uint8_t pins[] = {15, 14, 11, 13, 12, 10};
  for(uint8_t pin : pins) TEST_ASSERT_NULL_MESSAGE(bootPinEffect(pin), "default radio pin");
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_gpio45_picks_the_flash_voltage);
  RUN_TEST(test_gpio0_picks_the_boot_mode);
  RUN_TEST(test_gpio46_blocks_flashing_over_usb);
  RUN_TEST(test_gpio12_is_safe);
  RUN_TEST(test_default_radio_pins_are_safe);
  return UNITY_END();
}
