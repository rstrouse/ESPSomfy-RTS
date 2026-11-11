// What the web UI's panels bind, made from socket protocol 2's items, events
// and settings (docs/PROTOCOL.md), and back.  The UI kept its names where they
// differ from the protocol's: a frame's address and rcode, a scan's RSSI, the
// network panel's wifi, ethernet and ip objects, the radio's type.  The minify
// step joins this file into index.js; Node loads it on its own for the tests
// (test/js).
class ProtoViews {
    // A settings section's reply with what the device left out at its default
    // filled in, so a form shows what is saved rather than what it showed
    // before.
    static settings(schema, section, d) {
        return schema.fill(Object.assign({}, d), ProtoViews.SETTINGS[section]);
    }
    // A group with its shades whole, where the device sends their ids.
    static group(model, group) {
        if (!group) return group;
        const view = Object.assign({}, group);
        view.linkedShades = group.linkedShades.map((id) => model.shades.get(id) || { shadeId: id, name: `Shade ${id}`, remoteAddress: 0 });
        return view;
    }
    // A received frame as the frames list and remote linking read it.
    static frame(schema, d) {
        const f = schema.fill(Object.assign({}, d), ['encKey', 'remoteAddress', 'lastRollingCode', 'command', 'rawCmd', 'rssi', 'bits', 'proto',
            'valid', 'pulses']);
        return { encKey: f.encKey, address: f.remoteAddress, rcode: f.lastRollingCode, command: f.command, rawCmd: f.rawCmd, rssi: f.rssi,
            bits: f.bits, proto: f.proto, valid: f.valid, stepSize: d.stepSize, pulses: f.pulses };
    }
    static scan(schema, d) {
        const s = schema.fill(Object.assign({}, d), ['scanning', 'testFreq', 'testRssi', 'frequency', 'rssi']);
        return { scanning: s.scanning, testFreq: s.testFreq, testRSSI: s.testRssi, frequency: s.frequency, RSSI: s.rssi };
    }
    // The network section as the panel binds it: WiFi, Ethernet and the
    // address each in an object of their own, the connection as connType.
    static network(schema, d) {
        const n = ProtoViews.settings(schema, 'network', d);
        const view = { connType: n.connMode, hasEthernet: n.hasEthernet, wifi: {}, ethernet: {}, ip: {} };
        for (const name of ProtoViews.WIFI) view.wifi[name] = n[name];
        for (const name of ProtoViews.ETHERNET) view.ethernet[name] = n[name];
        for (const name of ProtoViews.IP) view.ip[name] = n[name];
        return view;
    }
    // The radio section as the panel binds it, where the bit length is the
    // radio's type.
    static radio(schema, d) {
        const config = ProtoViews.settings(schema, 'radio', d);
        config.type = config.bitLength;
        delete config.bitLength;
        return config;
    }
    // A save of the radio panel's config: everything but what is only shown.
    static radioSave(config) {
        const fields = { section: 'radio', bitLength: config.type };
        for (const name of ProtoViews.SETTINGS.radio) {
            if (name !== 'bitLength' && name !== 'radioInit') fields[name] = config[name];
        }
        return fields;
    }
    // An op's refusal as the UI's error box shows it.  A shade whose settings
    // don't add up carries the reason's number, which the errors table
    // describes.
    static opError(name, err) {
        const e = { htmlError: err.code, service: name, desc: err.desc };
        if (err.data && typeof err.data.validation === 'number' && err.data.validation !== 0) e.code = err.data.validation;
        return e;
    }
}
ProtoViews.WIFI = ['ssid', 'hasPassphrase', 'roaming', 'hidden'];
ProtoViews.ETHERNET = ['boardType', 'phyType', 'CLKMode', 'phyAddress', 'PWRPin', 'MDCPin', 'MDIOPin'];
ProtoViews.IP = ['dhcp', 'ip', 'gateway', 'subnet', 'dns1', 'dns2'];
// The fields of each settings section, as getSettings sends them
// (src/DeviceOps.cpp).
ProtoViews.SETTINGS = {
    general: ['hostname', 'ssdpBroadcast', 'ntpServer', 'posixZone'],
    network: ['connMode', 'hasEthernet'].concat(ProtoViews.WIFI, ProtoViews.ETHERNET, ProtoViews.IP),
    mqtt: ['enabled', 'pubDisco', 'protocol', 'hostname', 'port', 'username', 'hasPassword', 'rootTopic', 'discoTopic'],
    security: ['securityType', 'username', 'hasPassword', 'hasPin'],
    radio: ['bitLength', 'proto', 'enabled', 'TXPin', 'RXPin', 'SCKPin', 'MOSIPin', 'MISOPin', 'CSNPin', 'frequency', 'rxBandwidth',
        'deviation', 'txPower', 'noiseDetection', 'radioInit']
};
if (typeof module !== 'undefined' && module.exports) module.exports = { ProtoViews };
