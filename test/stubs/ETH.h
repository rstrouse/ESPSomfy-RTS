// Stand-in for the Arduino core's ETH.h in the native test env.  Uses the
// Arduino 3.x numbering, which put ETH_PHY_GENERIC first, so a stored PHY id
// that is cast instead of translated fails the tests.
#ifndef eth_stub_h
#define eth_stub_h
typedef enum {
  ETH_PHY_GENERIC,
  ETH_PHY_LAN8720,
  ETH_PHY_TLK110,
  ETH_PHY_RTL8201,
  ETH_PHY_DP83848,
  ETH_PHY_KSZ8041,
  ETH_PHY_KSZ8081,
  ETH_PHY_MAX
} eth_phy_type_t;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
// The P4's ETH.h takes the IDF's enum, where 0 is a deprecated default.
typedef enum {
  EMAC_CLK_DEFAULT,
  EMAC_CLK_EXT_IN,
  EMAC_CLK_OUT
} emac_rmii_clock_mode_t;
typedef emac_rmii_clock_mode_t eth_clock_mode_t;
#else
typedef enum {
  ETH_CLOCK_GPIO0_IN,
  ETH_CLOCK_GPIO0_OUT,
  ETH_CLOCK_GPIO16_OUT,
  ETH_CLOCK_GPIO17_OUT
} eth_clock_mode_t;
#endif
#endif
