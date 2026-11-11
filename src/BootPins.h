#ifndef BOOT_PINS_H
#define BOOT_PINS_H
#include <stdint.h>
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif

// The strapping pins whose level at reset can keep the chip from starting
// the firmware, or from being flashed over USB.  The ROM reads them before
// any code runs, so a radio that drives one at reset stops the board booting
// until EN is pressed (issue #34), or esptool from connecting.  The strapping
// pins that only quiet the boot log or set the SDIO timing aren't listed.
// The web UI has the same pins in data-src/js/pins.js, and checks the same
// radio pins as transceiver_config_t::apply(): keep the three in step.
constexpr const char *BOOT_PIN_DOWNLOAD = "Held low, it starts the download mode instead of the firmware: move it to another pin.";
constexpr const char *BOOT_PIN_FLASH_1V8 = "Held high, it powers the flash at 1.8 V and the board bootloops: move it to another pin.";
constexpr const char *BOOT_PIN_NO_USB_HIGH = "Held high, it stops flashing over USB, so disconnect it to flash.";
constexpr const char *BOOT_PIN_NO_USB_LOW = "Held low, it stops flashing over USB, so disconnect it to flash.";

// Returns what the pin does at reset and what to do, or nullptr for a pin
// that is safe.
inline const char *bootPinEffect(uint8_t pin) {
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
  if(pin == 0) return BOOT_PIN_DOWNLOAD;
  if(pin == 45) return BOOT_PIN_FLASH_1V8;
  if(pin == 46) return BOOT_PIN_NO_USB_HIGH;
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
  if(pin == 9) return BOOT_PIN_DOWNLOAD;
  if(pin == 8) return BOOT_PIN_NO_USB_LOW;
#elif defined(CONFIG_IDF_TARGET_ESP32C5)
  if(pin == 28) return BOOT_PIN_DOWNLOAD;
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
  if(pin == 35) return BOOT_PIN_DOWNLOAD;
#else
  // The original ESP32.  MTDI picks the flash voltage unless the
  // VDD_SDIO efuses are burned.
  if(pin == 0) return BOOT_PIN_DOWNLOAD;
  if(pin == 12) return BOOT_PIN_FLASH_1V8;
  if(pin == 2) return BOOT_PIN_NO_USB_HIGH;
#endif
  return nullptr;
}
#endif
