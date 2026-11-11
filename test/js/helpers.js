// What the web UI client tests share: the frames the e2e suite recorded from a
// device (test/fixtures/proto2), and stand-ins for the socket and the timers.
const path = require('node:path');

const FIXTURES = path.join(__dirname, '..', 'fixtures', 'proto2');
const hello = require(path.join(FIXTURES, 'hello.json'));
const sync = require(path.join(FIXTURES, 'sync.json'));
const schemaJson = require(path.join(FIXTURES, 'schema.json'));
const { ProtoSchema } = require('../../data-src/js/schema.js');

// A schema filled from the recorded sync, as a client builds it.
function recordedSchema() {
    const s = new ProtoSchema();
    for (const f of sync) if (f.t === 4) s.add(f.d);
    s.hash = hello.d.sh;
    return s;
}
// The recorded sync's device sections, expanded, frame by frame.
function deviceSections(schema) {
    return sync.filter((f) => f.t === 4).map((f) => schema.expand(ProtoSchema.withoutRows(f.d)));
}
// The recorded sync as the device sends it to a client that has the schema.
function syncWithoutSchema() {
    return sync.map((f) => Object.assign({}, f, { d: ProtoSchema.withoutRows(f.d || {}) }));
}
// The recorded sync's items under their short keys.
function recordedItems(key) {
    return sync.filter((f) => f.t === 4 && f.d[key]).flatMap((f) => f.d[key]);
}

// A WebSocket the test drives: it records what the client sends, and the test
// opens, delivers and closes.
class FakeSocket {
    constructor(url) {
        this.url = url;
        this.sent = [];
        this.closed = false;
        FakeSocket.last = this;
        FakeSocket.count++;
    }
    send(text) { this.sent.push(JSON.parse(text)); }
    close() {
        if (this.closed) return;
        this.closed = true;
        if (this.onclose) this.onclose({});
    }
    open() { if (this.onopen) this.onopen({}); }
    deliver(frame) { this.onmessage({ data: typeof frame === 'string' ? frame : JSON.stringify(frame) }); }
    // The device closing it, or refusing it before it opened.
    drop() { this.close(); }
}
// How many the client has opened.
FakeSocket.count = 0;
FakeSocket.reset = () => { FakeSocket.count = 0; FakeSocket.last = null; };

// Timers the test runs by hand.
class FakeTimers {
    constructor() {
        this.timers = new Map();
        this.next = 1;
        this.setTimeout = (fn, ms) => {
            const id = this.next++;
            this.timers.set(id, { fn: fn, ms: ms });
            return id;
        };
        this.clearTimeout = (id) => { this.timers.delete(id); };
    }
    // Runs every timer waiting, the shortest first, and returns their delays.
    runAll() {
        const due = [...this.timers.entries()].sort((a, b) => a[1].ms - b[1].ms);
        this.timers.clear();
        for (const [, t] of due) t.fn();
        return due.map(([, t]) => t.ms);
    }
}
// localStorage as far as the client uses it.
class FakeStorage {
    constructor() { this.items = {}; }
    getItem(key) { return Object.prototype.hasOwnProperty.call(this.items, key) ? this.items[key] : null; }
    setItem(key, value) { this.items[key] = String(value); }
}

module.exports = { hello, sync, schemaJson, recordedSchema, deviceSections, syncWithoutSchema, recordedItems, FakeSocket,
    FakeTimers, FakeStorage };
