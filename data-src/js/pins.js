// The strapping pins whose level at reset can keep each chip from starting
// the firmware, or from being flashed over USB, as src/BootPins.h has them
// for the log.  A radio that drives one at reset stops the board booting
// until EN is pressed (issue #34), or esptool from connecting.  Keep the
// pins, the radio pins checked and the shared RX/TX rule in step with that
// file.
// The minify step joins this file into index.js; Node loads it on its own for
// the tests (test/js).
class BootPins {
    // A warning for each of the radio config's pins that can stop the board
    // booting or being flashed.
    static radioWarnings(chipModel, config) {
        const chip = BootPins.CHIPS[chipModel || ''] || {};
        const warnings = [];
        for (const [field, name] of BootPins.RADIO_PINS) {
            const pin = config[field];
            if (field === 'RXPin' && pin === config.TXPin) continue;
            const effect = chip[pin];
            if (effect) warnings.push(`${name} is on GPIO-${pin}, a strapping pin the radio can drive at reset.  ${effect}`);
        }
        return warnings;
    }
}
BootPins.DOWNLOAD = 'Held low, it starts the download mode instead of the firmware: move it to another pin.';
BootPins.FLASH_1V8 = 'Held high, it powers the flash at 1.8 V and the board bootloops: move it to another pin.';
BootPins.NO_USB_HIGH = 'Held high, it stops flashing over USB, so disconnect it to flash.';
BootPins.NO_USB_LOW = 'Held low, it stops flashing over USB, so disconnect it to flash.';
// By the chip model the header sends, '' for the original ESP32.
BootPins.CHIPS = {
    '': { 0: BootPins.DOWNLOAD, 12: BootPins.FLASH_1V8, 2: BootPins.NO_USB_HIGH },
    s2: { 0: BootPins.DOWNLOAD, 45: BootPins.FLASH_1V8, 46: BootPins.NO_USB_HIGH },
    s3: { 0: BootPins.DOWNLOAD, 45: BootPins.FLASH_1V8, 46: BootPins.NO_USB_HIGH },
    c3: { 9: BootPins.DOWNLOAD, 8: BootPins.NO_USB_LOW },
    c5: { 28: BootPins.DOWNLOAD },
    c6: { 9: BootPins.DOWNLOAD, 8: BootPins.NO_USB_LOW },
    p4: { 35: BootPins.DOWNLOAD },
    p4es: { 35: BootPins.DOWNLOAD }
};
// The radio pins the CC1101 drives: at power-up GDO0 puts out a clock, GDO2
// CHIP_RDYn, and SO follows CHIP_RDYn while CSN is low.  Once it is set up,
// GDO2 puts out what it hears.
BootPins.RADIO_PINS = [['TXPin', 'TX'], ['RXPin', 'RX'], ['MISOPin', 'MISO']];
if (typeof module !== 'undefined' && module.exports) module.exports = { BootPins };
