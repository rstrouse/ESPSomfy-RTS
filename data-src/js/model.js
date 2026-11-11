// The shade model as socket protocol 2 keeps a client up to date with it
// (docs/PROTOCOL.md): rooms, shades, groups and repeaters from the sync, then
// changed by the device's events.  Items are inserted or updated by id, so an
// event during the sync does no harm.  It takes frames already expanded to
// names (ProtoSchema.expand) and fills in what a frame leaves out at its
// default.  The minify step joins this file into index.js; Node loads it on
// its own for the tests (test/js).
class ProtoModel {
    constructor(schema) {
        this.schema = schema;
        this.rooms = new Map();
        this.shades = new Map();
        this.groups = new Map();
        this.repeaters = [];
        this.header = {};
        this.status = {};
        this.listeners = [];
        this.seen = null;
    }
    // cb(kind, id, item): kind is room, shade or group for an item added or
    // changed; roomRemoved, shadeRemoved or groupRemoved; roomOrder,
    // shadeOrder or groupOrder (item: the ids in order); repeaters, header or
    // status.
    onChange(cb) { this.listeners.push(cb); }
    changed(kind, id, item) { for (const cb of this.listeners) cb(kind, id, item); }

    // A sync: what it doesn't list is gone once it ends, the repeaters too,
    // which it leaves out when there are none.
    beginSync() { this.seen = { rooms: new Set(), shades: new Set(), groups: new Set(), repeaters: false }; }
    // A sync that won't come after all.
    cancelSync() { this.seen = null; }
    applySync(d) {
        if (d.header) {
            this.header = this.schema.fill(Object.assign({}, d.header), ProtoModel.FIELDS.header);
            this.changed('header', 0, this.header);
        }
        for (const kind of ProtoModel.ITEMS) for (const item of d[kind.list] || []) this.put(kind, item);
        if (d.repeaters) this.putRepeaters(d.repeaters);
        if (d.status) {
            this.putStatus(d.status, ProtoModel.FIELDS.memory);
            this.putStatus(d.status, this.header.connType === 'Ethernet' ? ProtoModel.FIELDS.ethernet : ProtoModel.FIELDS.wifi);
        }
    }
    endSync() {
        if (!this.seen) return;
        for (const kind of ProtoModel.ITEMS) {
            const map = this[kind.list];
            for (const id of [...map.keys()]) {
                if (this.seen[kind.list].has(id)) continue;
                map.delete(id);
                this.changed(kind.removed, id);
            }
        }
        if (!this.seen.repeaters && this.repeaters.length) this.putRepeaters([]);
        this.seen = null;
    }
    // An event by its type's name; false for one that isn't about the model,
    // or that changed nothing.
    applyEvent(type, d) {
        for (const kind of ProtoModel.ITEMS) {
            const id = d[kind.id] || 0;
            switch (type) {
                case kind.name: this.put(kind, d); return true;
                case kind.state: return this.mergeState(kind, id, d);
                case kind.removed: return this.remove(kind, id);
                case kind.order: return this.order(kind, d.ids || []);
                default: break;
            }
        }
        switch (type) {
            case 'repeaters': this.putRepeaters(d.repeaters || []); return true;
            case 'memory': this.putStatus(d, ProtoModel.FIELDS.memory); return true;
            case 'wifi': this.putStatus(d, ProtoModel.FIELDS.wifi); return true;
            case 'ethernet': this.putStatus(d, ProtoModel.FIELDS.ethernet); return true;
            default: return false;
        }
    }

    // The lists in their sort order.
    roomList() { return ProtoModel.sorted(this.rooms); }
    shadeList() { return ProtoModel.sorted(this.shades); }
    groupList() { return ProtoModel.sorted(this.groups); }
    static sorted(map) { return [...map.values()].sort((a, b) => a.sortOrder - b.sortOrder); }

    // A whole item: what it had before is replaced.
    put(kind, item) {
        const id = item[kind.id] || 0;
        const whole = this.schema.fill(Object.assign({}, item), ProtoModel.FIELDS[kind.name]);
        // A shade's linked remotes leave out their defaults too.
        if (kind.name === 'shade')
            whole.linkedRemotes = whole.linkedRemotes.map((r) => this.schema.fill(Object.assign({}, r), ProtoModel.FIELDS.linkedRemote));
        this[kind.list].set(id, whole);
        if (this.seen) this.seen[kind.list].add(id);
        this.changed(kind.name, id, whole);
    }
    // Part of an item: only its state.  A state of an item not synced yet
    // waits for the item.
    mergeState(kind, id, d) {
        const item = this[kind.list].get(id);
        if (!item) return false;
        Object.assign(item, this.schema.fill(Object.assign({}, d), ProtoModel.FIELDS[kind.state]));
        this.changed(kind.name, id, item);
        return true;
    }
    remove(kind, id) {
        if (!this[kind.list].delete(id)) return false;
        this.changed(kind.removed, id);
        return true;
    }
    order(kind, ids) {
        ids.forEach((id, i) => {
            const item = this[kind.list].get(id);
            if (item) item.sortOrder = i;
        });
        this.changed(kind.order, 0, ids);
        return true;
    }
    putRepeaters(list) {
        this.repeaters = list.slice();
        if (this.seen) this.seen.repeaters = true;
        this.changed('repeaters', 0, this.repeaters);
    }
    putStatus(d, fields) {
        const part = {};
        for (const name of fields) part[name] = typeof d[name] === 'undefined' ? this.schema.defaultOf(name) : d[name];
        Object.assign(this.status, part);
        this.changed('status', 0, this.status);
    }
}
// Rooms, shades and groups: their list, id, and the events about them.  Rooms
// have no state of their own.
ProtoModel.ITEMS = [
    { name: 'room', list: 'rooms', id: 'roomId', state: null, removed: 'roomRemoved', order: 'roomOrder' },
    { name: 'shade', list: 'shades', id: 'shadeId', state: 'shadeState', removed: 'shadeRemoved', order: 'shadeOrder' },
    { name: 'group', list: 'groups', id: 'groupId', state: 'groupState', removed: 'groupRemoved', order: 'groupOrder' }
];
// The fields of each kind of item, as the device writes them
// (src/ProtoSync.cpp, src/outputs/ProtoSocket.cpp): what a frame that leaves
// one out means by it.  The schema has each field's default, not which items
// have it.
ProtoModel.FIELDS = {
    header: ['maxRooms', 'maxShades', 'maxGroups', 'maxGroupedShades', 'maxLinkedRemotes', 'startingAddress', 'bitLength',
        'proto', 'serverId', 'hostname', 'model', 'chipModel', 'hasOtaSlot', 'fw', 'connType'],
    room: ['roomId', 'name', 'sortOrder'],
    shadeState: ['shadeId', 'position', 'target', 'direction', 'myPos', 'tiltPosition', 'tiltTarget', 'tiltDirection',
        'myTiltPos', 'flags', 'lastRollingCode'],
    groupState: ['groupId', 'flags', 'direction'],
    linkedRemote: ['remoteAddress', 'lastRollingCode'],
    group: ['groupId', 'roomId', 'name', 'remoteAddress', 'lastRollingCode', 'bitLength', 'proto', 'sunSensor',
        'flipCommands', 'flags', 'repeats', 'sortOrder', 'linkedShades'],
    memory: ['freeHeap', 'maxHeap', 'minHeap', 'totalHeap', 'uptime', 'loopRate', 'loopMaxUs'],
    wifi: ['ssid', 'strength', 'channel'],
    ethernet: ['connected', 'speed', 'fullDuplex']
};
ProtoModel.FIELDS.shade = ProtoModel.FIELDS.shadeState.concat(['roomId', 'name', 'remoteAddress', 'upTime', 'downTime',
    'tiltTime', 'stepSize', 'paired', 'tiltType', 'shadeType', 'bitLength', 'proto', 'flipCommands', 'flipPosition',
    'inGroup', 'sunSensor', 'light', 'simMy', 'repeats', 'sortOrder', 'gpioUp', 'gpioDown', 'gpioMy', 'gpioLLTrigger',
    'linkedRemotes']);
if (typeof module !== 'undefined' && module.exports) module.exports = { ProtoModel };
