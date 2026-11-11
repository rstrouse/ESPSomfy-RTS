// The web UI's ProtoClient (data-src/js/client.js): the recorded hello and
// sync replayed through a stand-in socket.
const test = require('node:test');
const assert = require('node:assert/strict');
const { ProtoClient, ProtoError } = require('../../data-src/js/client.js');
const { hello, sync, syncWithoutSchema, recordedItems, recordedSchema, FakeSocket, FakeTimers, FakeStorage } = require('./helpers.js');

function client(opts) {
    FakeSocket.reset();
    const timers = new FakeTimers();
    const c = new ProtoClient(Object.assign({ host: 'device', WebSocket: FakeSocket, setTimeout: timers.setTimeout,
        clearTimeout: timers.clearTimeout, storage: null }, opts || {}));
    c.timers = timers;
    return c;
}
// Connects and plays the recorded hello and sync.
function connected(opts) {
    const c = client(opts);
    c.connect();
    const ws = FakeSocket.last;
    ws.open();
    ws.deliver(hello);
    for (const f of sync) ws.deliver(f);
    return { c, ws };
}
const schema = recordedSchema();
const T = schema.typeNumbers;
function reply(id, d) { return { v: 2, t: T.reply, id: id, d: d }; }

test('the client connects with its token and syncs, sending next after each sync frame', () => {
    const c = client({ token: 'abc 123' });
    c.connect();
    const ws = FakeSocket.last;
    assert.equal(ws.url, 'ws://device/ws?apikey=abc%20123');
    ws.open();
    ws.deliver(hello);
    for (const f of sync) ws.deliver(f);
    const syncFrames = sync.filter((f) => f.t === T.sync).length;
    assert.equal(ws.sent.filter((f) => f.t === T.next).length, syncFrames);
    assert.ok(c.ready);
    assert.equal(c.fw, hello.d.fw);
    assert.equal(c.model.shades.size, recordedItems('S').length);
});

test('synced comes once the sync has ended', () => {
    const c = client();
    let synced = 0;
    c.on('synced', () => synced++);
    c.connect();
    const ws = FakeSocket.last;
    ws.open();
    ws.deliver(hello);
    for (const f of sync.filter((f) => f.t === T.sync)) ws.deliver(f);
    assert.equal(synced, 0);
    ws.deliver(sync.find((f) => f.t === T.syncEnd));
    assert.equal(synced, 1);
});

test('an op resolves with its reply', async () => {
    const { c, ws } = connected();
    const p = c.op('getSettings', { section: 'radio' });
    const sent = ws.sent[ws.sent.length - 1];
    assert.deepEqual(sent, { v: 2, t: T.getSettings, id: sent.id, d: { se: 'radio' } });
    ws.deliver(reply(sent.id, { k: 1, bl: 80, en: 1 }));
    assert.deepEqual(await p, { ok: true, bitLength: 80, enabled: true });
});

test('a refused op rejects with the error by its name', async () => {
    const { c, ws } = connected();
    const p = c.op('saveSettings', { section: 'mqtt' });
    const id = ws.sent[ws.sent.length - 1].id;
    ws.deliver(reply(id, { e: schema.errorNumbers.badRequest, x: 'Nothing to save' }));
    await assert.rejects(p, (err) => err instanceof ProtoError && err.code === 'badRequest' && err.desc === 'Nothing to save');
});

test('an op is refused before the sync, and when its reply never comes', async () => {
    const c = client();
    await assert.rejects(c.op('reboot'), (err) => err.code === 'notConnected');
    const { c: c2 } = connected();
    const p = c2.op('reboot');
    c2.timers.runAll();
    await assert.rejects(p, (err) => err.code === 'timeout');
});

test('an op of a name or field the device lacks is refused here', async () => {
    const { c } = connected();
    await assert.rejects(c.op('noSuchOp'), (err) => err.code === 'unknownOp');
    await assert.rejects(c.op('reboot', { noSuchField: 1 }), (err) => err.code === 'badRequest');
});

test('ops waiting when the socket closes are refused, and the client connects again', async () => {
    const { c, ws } = connected();
    const p = c.op('scanWifi');
    let closed = 0;
    c.on('closed', () => closed++);
    ws.drop();
    await assert.rejects(p, (err) => err.code === 'closed');
    assert.equal(closed, 1);
    assert.equal(c.ready, false);
    assert.deepEqual(c.timers.runAll(), [500]);
    assert.notEqual(FakeSocket.last, ws);
});

test('a socket refused before it opened says so, and the retries slow down', () => {
    const c = client();
    let refused = 0;
    c.on('refused', () => refused++);
    c.connect();
    const delays = [];
    for (let i = 0; i < 6; i++) {
        FakeSocket.last.drop();
        delays.push(...c.timers.runAll());
    }
    assert.equal(refused, 6);
    assert.deepEqual(delays, [500, 1000, 2000, 4000, 8000, 10000]);
});

test('close stops the reconnecting', () => {
    const { c } = connected();
    c.close();
    assert.deepEqual(c.timers.runAll(), []);
});

test('events update the model, and are told by name', () => {
    const { c, ws } = connected();
    const shade = c.model.shadeList()[0];
    const seen = [];
    c.on('shadeState', (d) => seen.push(d));
    ws.deliver({ v: 2, t: T.shadeState, d: { s: shade.shadeId, t: 30, d: 1 } });
    assert.equal(c.model.shades.get(shade.shadeId).target, 30);
    assert.deepEqual(seen, [{ shadeId: shade.shadeId, target: 30, direction: 1 }]);
});

test('an event before the schema waits for it, and goes before the sync frames formatted after it', () => {
    const c = client();
    const states = [];
    c.on('shadeState', (d) => states.push(d));
    c.connect();
    const ws = FakeSocket.last;
    ws.open();
    ws.deliver(hello);
    // Before the schema: the client can't read it yet.
    const recorded = recordedItems('S')[0];
    ws.deliver({ v: 2, t: T.shadeState, d: { s: recorded.s, t: 55 } });
    assert.deepEqual(states, []);
    for (const f of sync) ws.deliver(f);
    assert.deepEqual(states, [{ shadeId: recorded.s, target: 55 }]);
    // The shade's sync frame was formatted after the event, so it wins.
    assert.equal(c.model.shades.get(recorded.s).target, recorded.t || 0);
});

test('an event after the schema goes at once, during the sync', () => {
    const c = client();
    c.connect();
    const ws = FakeSocket.last;
    ws.open();
    ws.deliver(hello);
    const syncFrames = sync.filter((f) => f.t === T.sync);
    for (const f of syncFrames) ws.deliver(f);
    const shadeId = recordedItems('S')[0].s;
    ws.deliver({ v: 2, t: T.shadeState, d: { s: shadeId, t: 66 } });
    assert.equal(c.ready, false);
    assert.equal(c.model.shades.get(shadeId).target, 66);
});

test('the schema is kept, and the device is told it has it', () => {
    const storage = new FakeStorage();
    connected({ storage: storage });
    const c = client({ storage: storage });
    c.connect();
    const ws = FakeSocket.last;
    assert.equal(ws.url, `ws://device/ws?schema=${hello.d.sh}`);
    ws.open();
    ws.deliver(hello);
    for (const f of syncWithoutSchema()) ws.deliver(f);
    assert.ok(c.ready);
    assert.equal(c.model.shades.size, recordedItems('S').length);
});

test('a schema cut off by a lost socket is not kept, so it comes whole again', () => {
    const c = client();
    c.connect();
    const ws = FakeSocket.last;
    ws.open();
    ws.deliver(hello);
    ws.deliver(sync[0]);
    ws.drop();
    c.timers.runAll();
    assert.equal(FakeSocket.last.url, 'ws://device/ws');
    FakeSocket.last.open();
    FakeSocket.last.deliver(hello);
    for (const f of sync) FakeSocket.last.deliver(f);
    assert.equal(Object.keys(c.schema.fields).length, Object.keys(schema.fields).length);
    assert.equal(c.schema.hash, hello.d.sh);
});

test('a socket is replaced, never doubled', () => {
    const { c, ws } = connected();
    c.connect();
    assert.equal(FakeSocket.count, 1);
    // A new token while a reconnect is waiting: one socket, and no timer left.
    ws.drop();
    c.setToken('new');
    assert.equal(FakeSocket.count, 2);
    assert.deepEqual(c.timers.runAll(), []);
    // While it connects: the old one goes without a refused.
    let refused = 0;
    c.on('refused', () => refused++);
    c.setToken('newer');
    assert.equal(refused, 0);
    assert.equal(FakeSocket.count, 3);
    assert.match(FakeSocket.last.url, /apikey=newer/);
});

test('a listener can stop the retries after a refusal', () => {
    const c = client();
    c.on('refused', () => c.close());
    c.connect();
    FakeSocket.last.drop();
    assert.deepEqual(c.timers.runAll(), []);
    assert.equal(FakeSocket.count, 1);
});

test('feeds are subscribed again after a reconnect', () => {
    const { c, ws } = connected();
    // Its reply never comes: the socket closes first.
    c.subscribe('frames').catch(() => {});
    ws.drop();
    c.timers.runAll();
    const again = FakeSocket.last;
    again.open();
    again.deliver(hello);
    for (const f of sync) again.deliver(f);
    const sent = again.sent.filter((f) => f.t === T.subscribe);
    assert.deepEqual(sent.map((f) => f.d), [{ fe: schema.feedNumbers.frames }]);
});

test('a feed the device lacks is refused here', async () => {
    const { c } = connected();
    await assert.rejects(c.subscribe('nope'), (err) => err.code === 'badRequest');
});

test('a long op refused as busy without its id goes again', () => {
    const { c, ws } = connected();
    c.op('saveRoom', { roomId: 1, name: 'x'.repeat(1500) }).catch(() => {});
    const sent = ws.sent.length;
    ws.deliver(reply(undefined, { e: schema.errorNumbers.busy, x: 'Another long message is on its way' }));
    c.timers.runAll();
    assert.equal(ws.sent.length, sent + 1);
    assert.equal(ws.sent[ws.sent.length - 1].d.n.length, 1500);
});

test('a refusal without an id for nothing long is reported', () => {
    const { c, ws } = connected();
    let error = null;
    c.on('error', (err) => { error = err; });
    ws.deliver(reply(undefined, { e: schema.errorNumbers.badRequest, x: 'Malformed message' }));
    assert.equal(error.code, 'badRequest');
});

test('a sync asked for and refused is called off', async () => {
    const { c, ws } = connected();
    const p = c.resync();
    const id = ws.sent[ws.sent.length - 1].id;
    ws.deliver(reply(id, { e: schema.errorNumbers.busy, x: 'busy' }));
    await assert.rejects(p);
    assert.equal(c.model.seen, null);
});

test('a kept schema of another firmware is dropped', () => {
    const storage = new FakeStorage();
    connected({ storage: storage });
    const c = client({ storage: storage });
    c.connect();
    const ws = FakeSocket.last;
    ws.open();
    ws.deliver({ v: 2, t: 1, d: { fw: 'v4.0.0', sh: 'ffffffff' } });
    // Its hash only counts once the new one has come whole.
    assert.equal(c.schema.hash, null);
    assert.ok(c.schema.empty);
});

test('a frame of another protocol version is reported', () => {
    const { c, ws } = connected();
    let mismatch = null;
    c.on('versionMismatch', (d) => { mismatch = d; });
    ws.deliver({ v: 3, t: 1, d: {} });
    assert.deepEqual(mismatch, { device: 3, client: 2 });
});

test('a new token connects again with it', () => {
    const { c, ws } = connected();
    c.setToken('new');
    assert.ok(ws.closed);
    c.timers.runAll();
    assert.match(FakeSocket.last.url, /apikey=new/);
});

test('waitFor resolves with the first event that matches, after the model has it', async () => {
    const { c, ws } = connected();
    const shadeId = recordedItems('S')[0].s;
    const p = c.waitFor('shade', (d) => d.shadeId === shadeId, 3000);
    ws.deliver({ v: 2, t: T.shade, d: { s: 250, n: 'Other' } });
    ws.deliver({ v: 2, t: T.shade, d: { s: shadeId, n: 'Renamed' } });
    const d = await p;
    assert.equal(d.name, 'Renamed');
    assert.equal(c.model.shades.get(shadeId).name, 'Renamed');
});

test('waitFor gives null when no event comes, and stops listening', async () => {
    const { c } = connected();
    const p = c.waitFor('room', null, 3000);
    c.timers.runAll();
    assert.equal(await p, null);
    assert.equal((c.listeners.room || []).length, 0);
});

test('off stops a listener, and one that removes itself mid-event leaves the others called', () => {
    const { c, ws } = connected();
    const seen = [];
    const once = (d) => { seen.push('once'); c.off('memory', once); };
    const always = () => seen.push('always');
    const dropped = () => seen.push('dropped');
    c.on('memory', once);
    c.on('memory', always);
    c.on('memory', dropped);
    c.off('memory', dropped);
    ws.deliver({ v: 2, t: T.memory, d: { fr: 1 } });
    ws.deliver({ v: 2, t: T.memory, d: { fr: 2 } });
    assert.deepEqual(seen, ['once', 'always', 'always']);
});

test('subscribe sends the feed number', () => {
    const { c, ws } = connected();
    c.subscribe('frames');
    const sent = ws.sent[ws.sent.length - 1];
    assert.equal(sent.t, T.subscribe);
    assert.deepEqual(sent.d, { fe: schema.feedNumbers.frames });
});
