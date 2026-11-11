#include <unity.h>
#include <string.h>
// No target defined: the original ESP32.
#include "BootPins.h"

void setUp() {}
void tearDown() {}

static void test_gpio12_picks_the_flash_voltage() {
  // Issue #34: a CC1101 holding GDO2 high on GPIO12 at power-up.
  const char *effect = bootPinEffect(12);
  TEST_ASSERT_NOT_NULL(effect);
  TEST_ASSERT_NOT_NULL(strstr(effect, "1.8 V"));
}

static void test_gpio0_picks_the_boot_mode() {
  const char *effect = bootPinEffect(0);
  TEST_ASSERT_NOT_NULL(effect);
  TEST_ASSERT_NOT_NULL(strstr(effect, "download mode"));
}

static void test_gpio2_blocks_flashing_over_usb() {
  // It only counts with GPIO0 low, so the board boots, but esptool can't
  // start the download mode while it is high.
  const char *effect = bootPinEffect(2);
  TEST_ASSERT_NOT_NULL(effect);
  TEST_ASSERT_NOT_NULL(strstr(effect, "high"));
  TEST_ASSERT_NOT_NULL(strstr(effect, "USB"));
}

static void test_harmless_strapping_pins_are_not_listed() {
  // 5 and 15 set the SDIO timing and quiet the boot log.  5 is the default
  // CSN.
  TEST_ASSERT_NULL(bootPinEffect(5));
  TEST_ASSERT_NULL(bootPinEffect(15));
}

static void test_default_radio_pins_are_safe() {
  // transceiver_config_t::load()'s defaults for the original ESP32.
  const uint8_t pins[] = {13, 35, 23, 19, 18, 5};
  for(uint8_t pin : pins) TEST_ASSERT_NULL_MESSAGE(bootPinEffect(pin), "default radio pin");
}

static void test_other_chips_boot_pins_are_safe_here() {
  TEST_ASSERT_NULL(bootPinEffect(8));
  TEST_ASSERT_NULL(bootPinEffect(9));
  TEST_ASSERT_NULL(bootPinEffect(45));
  TEST_ASSERT_NULL(bootPinEffect(46));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_gpio12_picks_the_flash_voltage);
  RUN_TEST(test_gpio0_picks_the_boot_mode);
  RUN_TEST(test_gpio2_blocks_flashing_over_usb);
  RUN_TEST(test_harmless_strapping_pins_are_not_listed);
  RUN_TEST(test_default_radio_pins_are_safe);
  RUN_TEST(test_other_chips_boot_pins_are_safe_here);
  return UNITY_END();
}
