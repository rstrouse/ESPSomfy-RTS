#include <unity.h>
// As on the original ESP32, so the translation is built.
#define CONFIG_ETH_USE_ESP32_EMAC 1
#include "EthPhy.h"

void setUp() {}
void tearDown() {}

static eth_phy_type_t translate(eth_phy_id_t id) {
  eth_phy_type_t type = ETH_PHY_MAX;
  TEST_ASSERT_TRUE(ethPhyType(static_cast<uint8_t>(id), type));
  return type;
}

/*********************************************************************
 * Stored ids
 ********************************************************************/
static void test_ids_match_stored_values() {
  // NVS, backups and the web UI hold these numbers.  Changing one moves
  // every existing Ethernet board to another chip.
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(eth_phy_id_t::LAN8720));
  TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(eth_phy_id_t::TLK110));
  TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(eth_phy_id_t::RTL8201));
  TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(eth_phy_id_t::DP83848));
  TEST_ASSERT_EQUAL_UINT8(4, static_cast<uint8_t>(eth_phy_id_t::DM9051));
  TEST_ASSERT_EQUAL_UINT8(5, static_cast<uint8_t>(eth_phy_id_t::KSZ8041));
  TEST_ASSERT_EQUAL_UINT8(6, static_cast<uint8_t>(eth_phy_id_t::KSZ8081));
}

/*********************************************************************
 * ethPhyType
 ********************************************************************/
static void test_translates_each_rmii_chip_by_name() {
  TEST_ASSERT_EQUAL(ETH_PHY_LAN8720, translate(eth_phy_id_t::LAN8720));
  TEST_ASSERT_EQUAL(ETH_PHY_TLK110, translate(eth_phy_id_t::TLK110));
  TEST_ASSERT_EQUAL(ETH_PHY_RTL8201, translate(eth_phy_id_t::RTL8201));
  TEST_ASSERT_EQUAL(ETH_PHY_DP83848, translate(eth_phy_id_t::DP83848));
  TEST_ASSERT_EQUAL(ETH_PHY_KSZ8041, translate(eth_phy_id_t::KSZ8041));
  TEST_ASSERT_EQUAL(ETH_PHY_KSZ8081, translate(eth_phy_id_t::KSZ8081));
}
static void test_lan8720_is_not_the_framework_zero() {
  // The default board stores 0.  Arduino 3.x's 0 is ETH_PHY_GENERIC.
  TEST_ASSERT_NOT_EQUAL(0, translate(eth_phy_id_t::LAN8720));
}
static void test_rejects_dm9051() {
  eth_phy_type_t type = ETH_PHY_MAX;
  TEST_ASSERT_FALSE(ethPhyType(static_cast<uint8_t>(eth_phy_id_t::DM9051), type));
}
static void test_rejects_unknown_ids() {
  eth_phy_type_t type = ETH_PHY_MAX;
  TEST_ASSERT_FALSE(ethPhyType(7, type));
  TEST_ASSERT_FALSE(ethPhyType(255, type));
}

/*********************************************************************
 * Clock modes
 ********************************************************************/
static void test_clock_ids_match_stored_values() {
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(eth_clock_id_t::GPIO0_IN));
  TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(eth_clock_id_t::GPIO0_OUT));
  TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(eth_clock_id_t::GPIO16_OUT));
  TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(eth_clock_id_t::GPIO17_OUT));
}
static void test_translates_each_clock_mode() {
  eth_clock_mode_t mode;
  TEST_ASSERT_TRUE(ethClockMode(0, mode));
  TEST_ASSERT_EQUAL(ETH_CLOCK_GPIO0_IN, mode);
  TEST_ASSERT_TRUE(ethClockMode(1, mode));
  TEST_ASSERT_EQUAL(ETH_CLOCK_GPIO0_OUT, mode);
  TEST_ASSERT_TRUE(ethClockMode(2, mode));
  TEST_ASSERT_EQUAL(ETH_CLOCK_GPIO16_OUT, mode);
  TEST_ASSERT_TRUE(ethClockMode(3, mode));
  TEST_ASSERT_EQUAL(ETH_CLOCK_GPIO17_OUT, mode);
}
static void test_rejects_unknown_clock_ids() {
  eth_clock_mode_t mode;
  TEST_ASSERT_FALSE(ethClockMode(4, mode));
  TEST_ASSERT_FALSE(ethClockMode(255, mode));
}
static void test_a_save_takes_each_clock_id_that_translates() {
  eth_clock_mode_t mode;
  TEST_ASSERT_EQUAL_UINT8(3, ETH_CLOCK_ID_MAX);
  TEST_ASSERT_TRUE(ethClockMode(ETH_CLOCK_ID_MAX, mode));
  TEST_ASSERT_FALSE(ethClockMode(ETH_CLOCK_ID_MAX + 1, mode));
}
static void test_clock_pins() {
  TEST_ASSERT_EQUAL_INT8(0, ethClockPin(0));
  TEST_ASSERT_EQUAL_INT8(0, ethClockPin(1));
  TEST_ASSERT_EQUAL_INT8(16, ethClockPin(2));
  TEST_ASSERT_EQUAL_INT8(17, ethClockPin(3));
  TEST_ASSERT_EQUAL_INT8(-1, ethClockPin(4));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_ids_match_stored_values);
  RUN_TEST(test_translates_each_rmii_chip_by_name);
  RUN_TEST(test_lan8720_is_not_the_framework_zero);
  RUN_TEST(test_rejects_dm9051);
  RUN_TEST(test_rejects_unknown_ids);
  RUN_TEST(test_clock_ids_match_stored_values);
  RUN_TEST(test_translates_each_clock_mode);
  RUN_TEST(test_rejects_unknown_clock_ids);
  RUN_TEST(test_a_save_takes_each_clock_id_that_translates);
  RUN_TEST(test_clock_pins);
  return UNITY_END();
}
