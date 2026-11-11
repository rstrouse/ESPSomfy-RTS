// What the web UI's panels bind (data-src/js/views.js), from protocol 2's
// shapes.
const test = require('node:test');
const assert = require('node:assert/strict');
const { ProtoViews } = require('../../data-src/js/views.js');
const { ProtoModel } = require('../../data-src/js/model.js');
const { recordedSchema, deviceSections } = require('./helpers.js');

const schema = recordedSchema();
function synced() {
    const model = new ProtoModel(schema);
    model.beginSync();
    for (const d of deviceSections(schema)) model.applySync(d);
    model.endSync();
    return model;
}

test('a settings section has every field, defaults where the device left them out', () => {
    // ssdpBroadcast off and no NTP server are at their defaults, so not sent.
    assert.deepEqual(ProtoViews.settings(schema, 'general', { hostname: 'Somfy' }),
        { hostname: 'Somfy', ssdpBroadcast: false, ntpServer: '', posixZone: '' });
    assert.deepEqual(ProtoViews.settings(schema, 'security', {}), { securityType: 0, username: '', hasPassword: false, hasPin: false });
});

test('the settings sections are what the device sends', () => {
    for (const section of Object.keys(ProtoViews.SETTINGS))
        for (const name of ProtoViews.SETTINGS[section]) assert.ok(schema.keys[name], `${section}.${name}`);
});

test('a group shows its shades whole', () => {
    const m = synced();
    const group = m.groupList()[0];
    const view = ProtoViews.group(m, group);
    assert.equal(view.linkedShades.length, group.linkedShades.length);
    assert.equal(view.linkedShades[0].name, m.shades.get(group.linkedShades[0]).name);
    // The model's item is left as it is.
    assert.equal(typeof group.linkedShades[0], 'number');
    // A shade the model hasn't got yet still shows.
    assert.equal(ProtoViews.group(m, Object.assign({}, group, { linkedShades: [250] })).linkedShades[0].shadeId, 250);
    assert.equal(ProtoViews.group(m, undefined), undefined);
});

test('a frame and a scan under the names the UI reads', () => {
    const f = ProtoViews.frame(schema, { remoteAddress: 5, lastRollingCode: 9, command: 'Up', rssi: -60, valid: true, pulses: [1, 2] });
    assert.equal(f.address, 5);
    assert.equal(f.rcode, 9);
    assert.equal(f.encKey, 0);
    assert.deepEqual(f.pulses, [1, 2]);
    assert.equal(f.stepSize, undefined);
    const s = ProtoViews.scan(schema, { scanning: true, testFreq: 433.1, testRssi: -80 });
    assert.deepEqual(s, { scanning: true, testFreq: 433.1, testRSSI: -80, frequency: 0, RSSI: 0 });
});

test('the network section as the panel binds it', () => {
    const v = ProtoViews.network(schema, { connMode: 1, ssid: 'home', hasPassphrase: true, dhcp: true, PWRPin: -1 });
    assert.equal(v.connType, 1);
    assert.equal(v.hasEthernet, false);
    assert.deepEqual(v.wifi, { ssid: 'home', hasPassphrase: true, roaming: false, hidden: false });
    assert.equal(v.ethernet.PWRPin, -1);
    assert.deepEqual(v.ip, { dhcp: true, ip: '', gateway: '', subnet: '', dns1: '', dns2: '' });
});

test('the radio section as the panel binds it, and back', () => {
    const config = ProtoViews.radio(schema, { bitLength: 80, frequency: 433.42, enabled: true, radioInit: true });
    assert.equal(config.type, 80);
    assert.equal(config.bitLength, undefined);
    assert.equal(config.TXPin, 0);
    const save = ProtoViews.radioSave(config);
    assert.equal(save.section, 'radio');
    assert.equal(save.bitLength, 80);
    assert.equal(save.frequency, 433.42);
    // Only shown, never saved.
    assert.equal('radioInit' in save, false);
    assert.equal('type' in save, false);
});

test('an op refusal for the error box, with a shade validation code', () => {
    assert.deepEqual(ProtoViews.opError('saveShade', { code: 'badRequest', desc: 'Pin in use', data: { validation: -10 } }),
        { htmlError: 'badRequest', service: 'saveShade', desc: 'Pin in use', code: -10 });
    assert.deepEqual(ProtoViews.opError('reboot', { code: 'closed', desc: 'The connection closed' }),
        { htmlError: 'closed', service: 'reboot', desc: 'The connection closed' });
});
