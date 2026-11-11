#include <unity.h>
// As on the ESP32-P4, which has the RMII MAC too but takes the IDF's clock
// enum.
#define CONFIG_ETH_USE_ESP32_EMAC 1
#define CONFIG_IDF_TARGET_ESP32P4 1
#include "EthPhy.h"

void setUp() {}
void tearDown() {}

static void test_builds_with_ethernet() {
  TEST_ASSERT_EQUAL(1, HAS_ETHERNET);
}
static void test_ip101_is_the_tlk110_id() {
  // Waveshare's ESP32-P4-ETH boards have an IP101, which Arduino drives as
  // the TLK110; the web UI's preset stores 1.
  eth_phy_type_t type = ETH_PHY_MAX;
  TEST_ASSERT_TRUE(ethPhyType(1, type));
  TEST_ASSERT_EQUAL(ETH_PHY_TLK110, type);
}
static void test_clock_in_is_ext_in() {
  // A cast would give the IDF's deprecated EMAC_CLK_DEFAULT.
  eth_clock_mode_t mode = EMAC_CLK_DEFAULT;
  TEST_ASSERT_TRUE(ethClockMode(static_cast<uint8_t>(eth_clock_id_t::GPIO0_IN), mode));
  TEST_ASSERT_EQUAL(EMAC_CLK_EXT_IN, mode);
}
static void test_rejects_clock_out() {
  // ETH.begin() keeps the clock on GPIO 50, which only takes a clock in.
  eth_clock_mode_t mode;
  TEST_ASSERT_FALSE(ethClockMode(1, mode));
  TEST_ASSERT_FALSE(ethClockMode(2, mode));
  TEST_ASSERT_FALSE(ethClockMode(3, mode));
  TEST_ASSERT_FALSE(ethClockMode(4, mode));
}
static void test_a_save_takes_clock_in_only() {
  // ProtoSettings refuses a CLKMode over this, so a save can't store a mode
  // that stops Ethernet at the next start.
  eth_clock_mode_t mode;
  TEST_ASSERT_EQUAL_UINT8(0, ETH_CLOCK_ID_MAX);
  TEST_ASSERT_TRUE(ethClockMode(ETH_CLOCK_ID_MAX, mode));
  TEST_ASSERT_FALSE(ethClockMode(ETH_CLOCK_ID_MAX + 1, mode));
}
static void test_clock_pin_is_50() {
  TEST_ASSERT_EQUAL_INT8(50, ethClockPin(0));
  TEST_ASSERT_EQUAL_INT8(-1, ethClockPin(1));
  TEST_ASSERT_EQUAL_INT8(-1, ethClockPin(3));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_builds_with_ethernet);
  RUN_TEST(test_ip101_is_the_tlk110_id);
  RUN_TEST(test_clock_in_is_ext_in);
  RUN_TEST(test_rejects_clock_out);
  RUN_TEST(test_a_save_takes_clock_in_only);
  RUN_TEST(test_clock_pin_is_50);
  return UNITY_END();
}
