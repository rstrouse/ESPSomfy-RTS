#ifndef ETH_PHY_H
#define ETH_PHY_H
#include <stdint.h>
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif

// Ethernet needs the RMII MAC, which only the original ESP32 and the P4 have.
// The C3, C5, S3 and C6 build without it.
#if defined(CONFIG_ETH_USE_ESP32_EMAC) && CONFIG_ETH_USE_ESP32_EMAC
#define HAS_ETHERNET 1
#else
#define HAS_ETHERNET 0
#endif

// PHY chip ids as stored in NVS and backups and sent to the web UI.  They are
// Arduino 2.x's eth_phy_type_t values, frozen: Arduino 3.x renumbered that
// enum, so an id is translated by name when Ethernet starts, never cast.
enum class eth_phy_id_t : uint8_t {
  LAN8720 = 0,
  TLK110 = 1,
  RTL8201 = 2,
  DP83848 = 3,
  DM9051 = 4, // An SPI chip.  The RMII MAC can't drive it.
  KSZ8041 = 5,
  KSZ8081 = 6
};

// RMII clock ids as stored in NVS and backups and sent to the web UI: the
// original ESP32's eth_clock_mode_t values.  The P4's eth_clock_mode_t is the
// IDF's emac_rmii_clock_mode_t, a different enum, so an id is translated.
enum class eth_clock_id_t : uint8_t {
  GPIO0_IN = 0, // The clock comes in.  On the P4, on GPIO 50.
  GPIO0_OUT = 1,
  GPIO16_OUT = 2,
  GPIO17_OUT = 3
};
// The highest clock id the chip takes: the P4's ETH.begin() takes the clock
// in on GPIO 50 only.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
constexpr uint8_t ETH_CLOCK_ID_MAX = 0;
#else
constexpr uint8_t ETH_CLOCK_ID_MAX = 3;
#endif

// The GPIO a stored clock id puts the RMII clock on, -1 for an id the chip
// can't use.
inline int8_t ethClockPin(uint8_t id) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  return static_cast<eth_clock_id_t>(id) == eth_clock_id_t::GPIO0_IN ? 50 : -1;
#else
  switch(static_cast<eth_clock_id_t>(id)) {
    case eth_clock_id_t::GPIO0_IN:
    case eth_clock_id_t::GPIO0_OUT: return 0;
    case eth_clock_id_t::GPIO16_OUT: return 16;
    case eth_clock_id_t::GPIO17_OUT: return 17;
    default: return -1;
  }
#endif
}

#if HAS_ETHERNET
#include <ETH.h>
// Sets type to the framework's PHY for a stored id.  False for an id the RMII
// MAC can't drive.
inline bool ethPhyType(uint8_t id, eth_phy_type_t &type) {
  switch(static_cast<eth_phy_id_t>(id)) {
    case eth_phy_id_t::LAN8720: type = ETH_PHY_LAN8720; return true;
    case eth_phy_id_t::TLK110: type = ETH_PHY_TLK110; return true;
    case eth_phy_id_t::RTL8201: type = ETH_PHY_RTL8201; return true;
    case eth_phy_id_t::DP83848: type = ETH_PHY_DP83848; return true;
    case eth_phy_id_t::KSZ8041: type = ETH_PHY_KSZ8041; return true;
    case eth_phy_id_t::KSZ8081: type = ETH_PHY_KSZ8081; return true;
    default: return false;
  }
}
// Sets mode to the framework's clock mode for a stored id.  False for an id
// the chip can't use.
inline bool ethClockMode(uint8_t id, eth_clock_mode_t &mode) {
  switch(static_cast<eth_clock_id_t>(id)) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    case eth_clock_id_t::GPIO0_IN: mode = EMAC_CLK_EXT_IN; return true;
#else
    case eth_clock_id_t::GPIO0_IN: mode = ETH_CLOCK_GPIO0_IN; return true;
    case eth_clock_id_t::GPIO0_OUT: mode = ETH_CLOCK_GPIO0_OUT; return true;
    case eth_clock_id_t::GPIO16_OUT: mode = ETH_CLOCK_GPIO16_OUT; return true;
    case eth_clock_id_t::GPIO17_OUT: mode = ETH_CLOCK_GPIO17_OUT; return true;
#endif
    default: return false;
  }
}
#endif
#endif
