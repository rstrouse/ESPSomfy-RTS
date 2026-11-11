// Socket protocol 2's schema (docs/PROTOCOL.md): the device's table of message
// types, fields, error codes and feeds, as the sync sends it.  It turns the
// short keys of a frame into names and back.  The minify step joins this file
// into index.js; Node loads it on its own for the tests (test/js).
class ProtoSchema {
    constructor() {
        this.hash = null;
        this.types = {};        // number -> name
        this.typeNumbers = {};  // name -> number
        this.fields = {};       // key -> { name, kind, def }
        this.keys = {};         // name -> key
        this.errors = {};
        this.errorNumbers = {};
        this.feedNumbers = {};
        this.rows = { T: [], F: [], E: [], Q: [] };
    }
    // A sync frame's payload without the schema's rows: the device's sections.
    static withoutRows(d) {
        const out = {};
        for (const key of Object.keys(d)) if (!ProtoSchema.ROWS.includes(key)) out[key] = d[key];
        return out;
    }
    // The schema's rows in a sync frame's payload: T types, F fields, E error
    // codes, Q feeds.  They may span several frames.
    add(d) {
        for (const [number, name] of d.T || []) {
            this.types[number] = name;
            this.typeNumbers[name] = number;
        }
        for (const [key, name, kind, def] of d.F || []) {
            this.fields[key] = { name: name, kind: kind, def: def };
            this.keys[name] = key;
        }
        for (const [number, name] of d.E || []) {
            this.errors[number] = name;
            this.errorNumbers[name] = number;
        }
        for (const [number, name] of d.Q || []) this.feedNumbers[name] = number;
        for (const k of ProtoSchema.ROWS) if (d[k]) this.rows[k].push(...d[k]);
    }
    get empty() { return Object.keys(this.fields).length === 0; }
    // What a field left out of a frame is: its default.
    defaultOf(name) {
        const f = this.fields[this.keys[name]];
        if (!f) return undefined;
        switch (f.kind) {
            case 'b': return f.def !== 0;
            case 's': return '';
            case 'l': return [];
            case 'o': return {};
            default: return f.def;
        }
    }
    // Short keys to names, and 1/0 to true/false, all the way down.  A field
    // left out stays out; fill() puts the defaults in.
    expand(value) {
        if (Array.isArray(value)) return value.map((v) => this.expand(v));
        if (value === null || typeof value !== 'object') return value;
        const out = {};
        for (const key of Object.keys(value)) {
            const f = this.fields[key];
            let v = value[key];
            if (f && f.kind === 'b' && typeof v === 'number') v = v !== 0;
            out[f ? f.name : key] = this.expand(v);
        }
        return out;
    }
    // Names to short keys, and booleans to 1/0, for an op's payload.  A name
    // the schema doesn't have is an error: the device would refuse it anyway.
    pack(value) {
        if (Array.isArray(value)) return value.map((v) => this.pack(v));
        if (value === null || typeof value !== 'object') return typeof value === 'boolean' ? (value ? 1 : 0) : value;
        const out = {};
        for (const name of Object.keys(value)) {
            if (typeof value[name] === 'undefined') continue;
            const key = this.keys[name];
            if (typeof key === 'undefined') throw new Error(`The device has no field ${name}`);
            out[key] = this.pack(value[name]);
        }
        return out;
    }
    // Sets each of names that obj lacks to its default, as a frame leaves out
    // a field at its default.  Returns obj.
    fill(obj, names) {
        for (const name of names) if (typeof obj[name] === 'undefined') obj[name] = this.defaultOf(name);
        return obj;
    }
    // What a cache keeps, and the schema back from it.
    save() { return { hash: this.hash, rows: this.rows }; }
    static load(saved) {
        if (!saved || typeof saved.hash !== 'string' || !saved.rows) return null;
        const s = new ProtoSchema();
        s.add(saved.rows);
        s.hash = saved.hash;
        return s.empty ? null : s;
    }
}
// The keys of the schema's rows in a sync frame, fixed as a client needs them
// before it has the schema.
ProtoSchema.ROWS = ['T', 'F', 'E', 'Q'];
if (typeof module !== 'undefined' && module.exports) module.exports = { ProtoSchema };
