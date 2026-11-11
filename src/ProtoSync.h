#ifndef PROTO_SYNC_H
#define PROTO_SYNC_H
#include <stddef.h>
#include <stdint.h>
#include "ItemSnapshot.h"
#include "ProtoSchema.h"

// The shade model in socket protocol 2: how each room, shade and group is
// written, and the sync that sends a client all of them (docs/PROTOCOL.md).
// Kept free of the shade model and the web server so it can be unit tested on
// the PC (pio test -e native).

// The largest frame: what a sync message, or any event, is formatted into.
#define PROTO_MAX_FRAME 4096

// The fields of one item, into the object the caller opened.
void protoRoom(ProtoWriter &w, const room_snapshot_t &room);
// Everything about a shade: its settings and its state.
void protoShade(ProtoWriter &w, const shade_snapshot_t &shade);
// Only what changes while a shade moves or is commanded.
void protoShadeState(ProtoWriter &w, const shade_snapshot_t &shade);
// A group, with the ids of its shades.
void protoGroup(ProtoWriter &w, const group_snapshot_t &group);
void protoGroupState(ProtoWriter &w, uint8_t groupId, uint8_t flags, int8_t direction);

// Where a sync gets the shade model from: the live model on the device, a
// filled snapshot in the tests.  Slots run from 0 to the SOMFY_MAX_* of their
// kind; an empty slot returns false.
class SyncSource {
  public:
    virtual ~SyncSource() {}
    virtual bool room(uint8_t slot, room_snapshot_t &out) = 0;
    virtual bool shade(uint8_t slot, shade_snapshot_t &out) = 0;
    virtual bool group(uint8_t slot, group_snapshot_t &out) = 0;
    virtual bool repeater(uint8_t slot, uint32_t &address) = 0;
    // The fields of the header and status objects.
    virtual void header(ProtoWriter &w) = 0;
    virtual void status(ProtoWriter &w) = 0;
};

// The parts of a sync, in the order they are sent.
enum class sync_section_t : uint8_t {
  types, fields, errors, feeds,                         // the schema
  header, rooms, shades, groups, repeaters, status,     // the device
  done
};
struct sync_cursor_t {
  sync_section_t section = sync_section_t::done;
  uint8_t index = 0;
  bool active() const { return this->section != sync_section_t::done; }
};
// A sync from the start: with the schema, or from the header when the client
// already has it.
sync_cursor_t beginSync(bool withSchema);
// Writes the next sync frame into buff: as many whole items as fit in size
// bytes, never one split in two.  An item too big for an empty frame is left
// out.  Returns the frame's length, or 0 once the sync is done and syncEnd is
// due.  Each call reads the source afresh, so a sync shows each item as it is
// when it goes out, not one moment.
size_t writeSyncFrame(SyncSource &src, sync_cursor_t &cur, char *buff, size_t size);
#endif
