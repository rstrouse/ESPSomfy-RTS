// The web UI's ProtoSchema (data-src/js/schema.js) against the schema a
// device sent (test/fixtures/proto2).
const test = require('node:test');
const assert = require('node:assert/strict');
const { ProtoSchema } = require('../../data-src/js/schema.js');
const { recordedSchema, schemaJson } = require('./helpers.js');

test('the schema is collected from every sync frame it spans', () => {
    const s = recordedSchema();
    assert.equal(Object.keys(s.types).length, Object.keys(schemaJson.types).length);
    assert.equal(Object.keys(s.fields).length, Object.keys(schemaJson.fields).length);
    assert.equal(s.typeNumbers.shadeCommand, 16);
    assert.equal(s.typeNumbers.saveSettings, 50);
    assert.equal(s.keys.shadeId, 's');
    assert.equal(s.errorNumbers.busy, 5);
    assert.equal(s.feedNumbers.frequencyScan, 2);
});

test('short keys become names, and 1/0 booleans, all the way down', () => {
    const s = recordedSchema();
    const shade = s.expand({ s: 3, n: 'Office', i: 1, fp: 0, lk: [{ a: 5, c: 9 }] });
    assert.deepEqual(shade, { shadeId: 3, name: 'Office', paired: true, flipPosition: false,
        linkedRemotes: [{ remoteAddress: 5, lastRollingCode: 9 }] });
    // A whole number that happens to be 1 stays a number.
    assert.deepEqual(s.expand({ p: 1 }), { position: 1 });
});

test('names become short keys for an op', () => {
    const s = recordedSchema();
    assert.deepEqual(s.pack({ shadeId: 3, command: 'Up', paired: true, sunny: false }), { s: 3, cm: 'Up', i: 1, sn: 0 });
    assert.deepEqual(s.pack({ ids: [3, 1, 2] }), { is: [3, 1, 2] });
    // Left out, not sent as undefined.
    assert.deepEqual(s.pack({ shadeId: 3, target: undefined }), { s: 3 });
    assert.throws(() => s.pack({ nonsense: 1 }), /no field nonsense/);
});

test('a field left out is its default', () => {
    const s = recordedSchema();
    assert.equal(s.defaultOf('myPos'), -1);
    assert.equal(s.defaultOf('stepSize'), 100);
    assert.equal(s.defaultOf('bitLength'), 56);
    assert.equal(s.defaultOf('paired'), false);
    assert.equal(s.defaultOf('name'), '');
    assert.deepEqual(s.defaultOf('linkedShades'), []);
    assert.deepEqual(s.fill({ shadeId: 2 }, ['shadeId', 'position', 'myPos']), { shadeId: 2, position: 0, myPos: -1 });
});

test('a cached schema comes back whole, with its hash', () => {
    const s = recordedSchema();
    const back = ProtoSchema.load(JSON.parse(JSON.stringify(s.save())));
    assert.equal(back.hash, s.hash);
    assert.deepEqual(back.fields, s.fields);
    assert.deepEqual(back.types, s.types);
    assert.equal(ProtoSchema.load(null), null);
    assert.equal(ProtoSchema.load({ hash: 'x', rows: {} }), null);
});
