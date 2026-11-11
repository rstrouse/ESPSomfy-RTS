// A socket protocol 2 client (docs/PROTOCOL.md): ws://host/ws, hello, the sync
// with next after each frame, ops answered by their reply, events and feeds.
// It keeps the shade model (ProtoModel) up to date and connects again when the
// socket closes.  The WebSocket, the timers and the schema's storage can be
// handed in, so the tests (test/js) run it without a device.  The minify step
// joins this file into index.js after schema.js and model.js.
class ProtoError extends Error {
    // code is an error name from the schema (badRequest, busy ...), or
    // notConnected, closed or timeout from the client itself.
    constructor(code, desc, data) {
        super(desc || code);
        this.code = code;
        this.desc = desc || code;
        this.data = data || {};
    }
}
class ProtoClient {
    // opts: host, token, WebSocket, storage (getItem/setItem for the schema;
    // localStorage when there is one), setTimeout/clearTimeout, opTimeoutMs.
    constructor(opts) {
        const deps = ProtoClient.deps();
        this.Schema = deps.ProtoSchema;
        this.host = opts.host;
        this.token = opts.token || '';
        this.WebSocket = opts.WebSocket || (typeof WebSocket !== 'undefined' ? WebSocket : null);
        this.storage = typeof opts.storage !== 'undefined' ? opts.storage : ProtoClient.browserStorage();
        this.setTimeout = opts.setTimeout || ((fn, ms) => setTimeout(fn, ms));
        this.clearTimeout = opts.clearTimeout || ((t) => clearTimeout(t));
        this.opTimeoutMs = opts.opTimeoutMs || 10000;
        this.schema = this.loadSchema() || new this.Schema();
        this.model = new deps.ProtoModel(this.schema);
        this.model.onChange((kind, id, item) => this.emit('change', { kind: kind, id: id, item: item }));
        this.listeners = {};
        this.pending = new Map();
        this.feeds = new Set();
        this.nextId = 1;
        this.ws = null;
        this.ready = false;        // synced: ops may go
        this.schemaReady = false;  // frames can be read
        this.helloHash = null;
        this.opened = false;
        this.stopped = true;
        this.retries = 0;
        this.reconnectTimer = null;
        this.held = [];
        this.fw = null;
    }
    // In the browser the classes are the ones index.js was joined with; in
    // Node they are loaded.
    static deps() {
        if (typeof require === 'function' && typeof module !== 'undefined' && module.exports)
            return { ProtoSchema: require('./schema.js').ProtoSchema, ProtoModel: require('./model.js').ProtoModel };
        return { ProtoSchema: ProtoSchema, ProtoModel: ProtoModel };
    }
    static browserStorage() {
        try {
            return typeof localStorage !== 'undefined' ? localStorage : null;
        } catch (err) {
            return null;
        }
    }

    // cb(d) for a device event by its name (shadeState, frame ...), and for
    // the client's own: open, hello, synced, change, closed, versionMismatch,
    // error (a refusal the device couldn't tie to an op) and refused.
    // refused: the socket closed before it opened.  A browser can't tell a
    // refused token from a device that is down, so the client tries again
    // unless a listener calls close(), as the UI does once /loginContext says
    // a login is needed (docs/SECURITY_PLAN.md).
    on(name, cb) { (this.listeners[name] = this.listeners[name] || []).push(cb); }
    off(name, cb) { this.listeners[name] = (this.listeners[name] || []).filter((x) => x !== cb); }
    emit(name, d) { for (const cb of (this.listeners[name] || []).slice()) cb(d); }
    // The next event of that name that match(d) accepts, or null after ms.
    // An edit's events follow its reply, so this is how a caller gets the
    // item the edit left: the model has it by then.
    waitFor(name, match, ms) {
        return new Promise((resolve) => {
            let timer = null;
            const cb = (d) => {
                if (match && !match(d)) return;
                this.off(name, cb);
                this.clearTimeout(timer);
                resolve(d);
            };
            this.on(name, cb);
            timer = this.setTimeout(() => {
                this.off(name, cb);
                resolve(null);
            }, ms);
        });
    }

    connect() {
        if (!this.stopped && (this.ws || this.reconnectTimer)) return;
        this.stopped = false;
        this.open();
    }
    close() {
        this.stopped = true;
        this.cancelReconnect();
        if (this.ws) this.ws.close();
    }
    // A new token, from a login: the device checks it as the socket opens, so
    // the client connects again with it at once.
    setToken(token) {
        this.token = token || '';
        this.stopped = false;
        this.retries = 0;
        this.open();
    }
    url() {
        const params = [];
        if (this.token) params.push(`apikey=${encodeURIComponent(this.token)}`);
        if (this.schema.hash) params.push(`schema=${encodeURIComponent(this.schema.hash)}`);
        return `ws://${this.host}/ws${params.length ? '?' + params.join('&') : ''}`;
    }
    cancelReconnect() {
        if (this.reconnectTimer) this.clearTimeout(this.reconnectTimer);
        this.reconnectTimer = null;
    }
    // One socket at a time: one being replaced is let go without a word.
    open() {
        this.cancelReconnect();
        if (this.ws) {
            const old = this.ws;
            this.ws = null;
            this.lost();
            old.close();
        }
        this.ready = false;
        this.schemaReady = false;
        this.opened = false;
        this.held = [];
        const ws = new this.WebSocket(this.url());
        this.ws = ws;
        ws.onopen = () => {
            if (this.ws !== ws) return;
            this.opened = true;
            this.retries = 0;
            this.emit('open');
        };
        ws.onmessage = (evt) => {
            if (this.ws === ws) this.receive(evt.data);
        };
        ws.onclose = () => {
            if (this.ws !== ws) return;
            this.ws = null;
            this.lost();
            this.emit(this.opened ? 'closed' : 'refused');
            if (this.stopped) return;
            // Soon after a close, slower after each failure, at most 10 s.
            const ms = Math.min(500 * Math.pow(2, this.retries++), 10000);
            this.reconnectTimer = this.setTimeout(() => this.open(), ms);
        };
    }
    // The socket has gone: nothing waiting gets its reply.
    lost() {
        this.ready = false;
        for (const [, p] of this.pending) {
            this.clearTimeout(p.timer);
            p.reject(new ProtoError('closed', 'The connection closed'));
        }
        this.pending.clear();
    }
    frame(type, d, id) {
        const frame = { v: ProtoClient.VERSION, t: this.schema.typeNumbers[type] };
        if (typeof frame.t === 'undefined') throw new ProtoError('unknownOp', `The device has no message ${type}`);
        if (id) frame.id = id;
        if (d) frame.d = d;
        return JSON.stringify(frame);
    }

    // Sends an op, or a control message that wants a reply, by its name with
    // its fields by name.  Resolves with the reply's fields, rejects with a
    // ProtoError.
    op(name, fields) {
        return new Promise((resolve, reject) => {
            if (!this.ready || !this.ws) {
                reject(new ProtoError('notConnected', 'Not connected to the device'));
                return;
            }
            const id = this.nextId++;
            let text;
            try {
                text = this.frame(name, this.schema.pack(fields || {}), id);
            } catch (err) {
                reject(err instanceof ProtoError ? err : new ProtoError('badRequest', err.message));
                return;
            }
            this.ws.send(text);
            const timer = this.setTimeout(() => {
                this.pending.delete(id);
                reject(new ProtoError('timeout', `No reply to ${name}`));
            }, this.opTimeoutMs);
            this.pending.set(id, { resolve: resolve, reject: reject, timer: timer, text: text, resends: 0 });
        });
    }
    // Feeds stay subscribed across reconnects.
    subscribe(feed) {
        if (typeof this.schema.feedNumbers[feed] === 'undefined') return Promise.reject(new ProtoError('badRequest', `The device has no feed ${feed}`));
        this.feeds.add(feed);
        return this.op('subscribe', { feed: this.schema.feedNumbers[feed] });
    }
    unsubscribe(feed) {
        this.feeds.delete(feed);
        if (typeof this.schema.feedNumbers[feed] === 'undefined') return Promise.resolve();
        return this.op('unsubscribe', { feed: this.schema.feedNumbers[feed] });
    }
    // The shade model and status again, without the schema.
    resync() {
        this.model.beginSync();
        return this.op('sync').catch((err) => {
            this.model.cancelSync();
            throw err;
        });
    }

    receive(text) {
        let frame;
        try {
            frame = JSON.parse(text);
        } catch (err) {
            return;
        }
        if (frame.v !== ProtoClient.VERSION) {
            this.emit('versionMismatch', { device: frame.v, client: ProtoClient.VERSION });
            return;
        }
        const d = frame.d || {};
        // These have fixed numbers: they come before the schema.
        switch (frame.t) {
            case ProtoClient.HELLO: this.hello(d); return;
            case ProtoClient.SYNC: this.syncFrame(d); return;
            case ProtoClient.SYNC_END: this.syncEnd(); return;
            default: break;
        }
        // A frame the client can't read yet waits for the schema.
        if (!this.schemaReady) this.held.push(frame);
        else this.dispatch(frame);
    }
    hello(d) {
        this.fw = d.fw;
        this.helloHash = d.sh;
        // A schema the client kept for this one needn't come again; any other
        // is no use, and the device sends the one it has.
        if (this.schema.hash !== d.sh) {
            this.schema = new this.Schema();
            this.model.schema = this.schema;
        }
        this.model.beginSync();
        this.emit('hello', d);
        if (this.schema.hash === d.sh) this.readable();
    }
    syncFrame(d) {
        this.schema.add(d);
        const device = this.Schema.withoutRows(d);
        // The schema's rows come first, so a frame with the device's sections
        // has the whole of it.
        if (Object.keys(device).length) {
            this.readable();
            this.model.applySync(this.schema.expand(device));
        }
        this.ws.send(JSON.stringify({ v: ProtoClient.VERSION, t: ProtoClient.NEXT }));
    }
    // The schema is whole: frames that waited for it go, in the order they
    // came and ahead of the sync frames formatted after them.
    readable() {
        if (this.schemaReady) return;
        this.schemaReady = true;
        const held = this.held;
        this.held = [];
        for (const frame of held) this.dispatch(frame);
    }
    syncEnd() {
        this.readable();
        this.model.endSync();
        if (!this.ready) {
            this.ready = true;
            // Only a whole schema is kept, so a sync cut short gets it again.
            this.schema.hash = this.helloHash;
            this.saveSchema();
            for (const feed of this.feeds) this.op('subscribe', { feed: this.schema.feedNumbers[feed] }).catch(() => {});
        }
        this.emit('synced', this.model);
    }
    dispatch(frame) {
        const d = this.schema.expand(frame.d || {});
        if (frame.t === ProtoClient.REPLY) this.reply(frame.id, d);
        else {
            const name = this.schema.types[frame.t];
            if (!name) return;
            this.model.applyEvent(name, d);
            this.emit(name, d);
        }
    }
    reply(id, d) {
        const err = d.ok ? null : new ProtoError(this.schema.errors[d.code] || 'internal', d.desc, d);
        const p = typeof id !== 'undefined' ? this.pending.get(id) : null;
        if (p) {
            this.pending.delete(id);
            this.clearTimeout(p.timer);
            if (err) p.reject(err);
            else p.resolve(d);
            return;
        }
        if (!err) return;
        // A long message the device couldn't take while it put another one
        // together is refused without its id: the long ones waiting go again.
        if (err.code === 'busy' && this.resendLong()) return;
        this.emit('error', err);
    }
    resendLong() {
        let any = false;
        for (const [, p] of this.pending) {
            if (p.text.length < ProtoClient.LONG_MESSAGE || p.resends >= 3) continue;
            p.resends++;
            any = true;
            this.setTimeout(() => {
                if (this.ws && [...this.pending.values()].includes(p)) this.ws.send(p.text);
            }, 200 * p.resends);
        }
        return any;
    }

    loadSchema() {
        if (!this.storage) return null;
        try {
            return this.Schema.load(JSON.parse(this.storage.getItem(ProtoClient.SCHEMA_KEY)));
        } catch (err) {
            return null;
        }
    }
    saveSchema() {
        if (!this.storage) return;
        try {
            this.storage.setItem(ProtoClient.SCHEMA_KEY, JSON.stringify(this.schema.save()));
        } catch (err) { /* Private windows may have no storage: the device sends the schema each time. */ }
    }
}
ProtoClient.VERSION = 2;
// The types a client reads before it has the schema, and next, which it sends
// during the sync.
ProtoClient.HELLO = 1;
ProtoClient.REPLY = 2;
ProtoClient.NEXT = 3;
ProtoClient.SYNC = 4;
ProtoClient.SYNC_END = 5;
// A message longer than a TCP segment reaches the device in pieces
// (docs/PROTOCOL.md).
ProtoClient.LONG_MESSAGE = 1400;
ProtoClient.SCHEMA_KEY = 'espsomfyProtoSchema';
if (typeof module !== 'undefined' && module.exports) module.exports = { ProtoClient, ProtoError };
