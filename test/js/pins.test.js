// The radio pins that can stop the board booting (data-src/js/pins.js).
const test = require('node:test');
const assert = require('node:assert/strict');
const { BootPins } = require('../../data-src/js/pins.js');

// transceiver_config_t::load()'s defaults for the original ESP32.
const esp32Defaults = { SCKPin: 18, CSNPin: 5, MOSIPin: 23, MISOPin: 19, TXPin: 13, RXPin: 35 };

test('the original ESP32 defaults are safe', () => {
    assert.deepEqual(BootPins.radioWarnings('', esp32Defaults), []);
});

test('RX on GPIO12 warns of the flash voltage on the original ESP32', () => {
    // Issue #34.
    const warnings = BootPins.radioWarnings('', Object.assign({}, esp32Defaults, { RXPin: 12 }));
    assert.equal(warnings.length, 1);
    assert.match(warnings[0], /^RX is on GPIO-12/);
    assert.match(warnings[0], /1\.8 V/);
});

test('TX on GPIO2 warns of flashing over USB on the original ESP32', () => {
    // The WT32-ETH01 wiring in the wiki.
    const warnings = BootPins.radioWarnings('', Object.assign({}, esp32Defaults, { TXPin: 2 }));
    assert.equal(warnings.length, 1);
    assert.match(warnings[0], /^TX is on GPIO-2, .*Held high, it stops flashing over USB, so disconnect it to flash\.$/);
});

test('GPIO8 held low blocks flashing on the C3 and C6', () => {
    const config = { SCKPin: 4, CSNPin: 7, MOSIPin: 6, MISOPin: 5, TXPin: 10, RXPin: 8 };
    for (const chip of ['c3', 'c6']) {
        const warnings = BootPins.radioWarnings(chip, config);
        assert.equal(warnings.length, 1);
        assert.match(warnings[0], /^RX is on GPIO-8, .*Held low, it stops flashing over USB/);
    }
});

test('an unknown chip model warns of nothing', () => {
    assert.deepEqual(BootPins.radioWarnings('h2', Object.assign({}, esp32Defaults, { RXPin: 12 })), []);
});

test('a missing chip model is the original ESP32', () => {
    assert.equal(BootPins.radioWarnings(null, Object.assign({}, esp32Defaults, { TXPin: 0 })).length, 1);
});

test('GPIO12 is safe on the S3', () => {
    assert.deepEqual(BootPins.radioWarnings('s3', { SCKPin: 12, CSNPin: 10, MOSIPin: 11, MISOPin: 13, TXPin: 15, RXPin: 14 }), []);
});

test('MISO and TX warn, the pins the radio only reads do not', () => {
    const config = { SCKPin: 4, CSNPin: 0, MOSIPin: 1, MISOPin: 9, TXPin: 10, RXPin: 3 };
    assert.deepEqual(BootPins.radioWarnings('c3', config).map((w) => w.split(' ')[0]), ['MISO']);
    assert.deepEqual(BootPins.radioWarnings('c6', Object.assign({}, config, { MISOPin: 2, TXPin: 9, RXPin: 9 })).map((w) => w.split(' ')[0]),
        ['TX']);
});

test('the pre-v3 P4 has the P4 pins', () => {
    const p4 = { SCKPin: 27, CSNPin: 46, MOSIPin: 32, MISOPin: 33, TXPin: 2, RXPin: 35 };
    assert.equal(BootPins.radioWarnings('p4', p4).length, 1);
    assert.deepEqual(BootPins.radioWarnings('p4es', p4), BootPins.radioWarnings('p4', p4));
});
