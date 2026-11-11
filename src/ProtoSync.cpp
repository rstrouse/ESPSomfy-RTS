#include "esp_log.h"
#include "ProtoSync.h"
#include "ProtoFrame.h"

static const char *TAG = "Proto";

/*********************************************************************
 * Items
 ********************************************************************/
void protoRoom(ProtoWriter &w, const room_snapshot_t &room) {
  w.add(proto_field_t::roomId, room.roomId);
  w.add(proto_field_t::name, room.name);
  w.add(proto_field_t::sortOrder, room.sortOrder);
}
void protoShadeState(ProtoWriter &w, const shade_snapshot_t &shade) {
  w.add(proto_field_t::shadeId, shade.shadeId);
  w.add(proto_field_t::position, shade.position);
  w.add(proto_field_t::target, shade.target);
  w.add(proto_field_t::direction, shade.direction);
  w.add(proto_field_t::myPos, shade.myPos);
  w.add(proto_field_t::tiltPosition, shade.tiltPosition);
  w.add(proto_field_t::tiltTarget, shade.tiltTarget);
  w.add(proto_field_t::tiltDirection, shade.tiltDirection);
  w.add(proto_field_t::myTiltPos, shade.myTiltPos);
  w.add(proto_field_t::flags, shade.flags);
  w.add(proto_field_t::lastRollingCode, shade.lastRollingCode);
}
void protoShade(ProtoWriter &w, const shade_snapshot_t &shade) {
  protoShadeState(w, shade);
  w.add(proto_field_t::roomId, shade.roomId);
  w.add(proto_field_t::name, shade.name);
  w.add(proto_field_t::remoteAddress, shade.remoteAddress);
  w.add(proto_field_t::upTime, shade.upTime);
  w.add(proto_field_t::downTime, shade.downTime);
  w.add(proto_field_t::tiltTime, shade.tiltTime);
  w.add(proto_field_t::stepSize, shade.stepSize);
  w.add(proto_field_t::paired, shade.paired);
  w.add(proto_field_t::tiltType, shade.tiltType);
  w.add(proto_field_t::shadeType, shade.shadeType);
  w.add(proto_field_t::bitLength, shade.bitLength);
  w.add(proto_field_t::proto, shade.proto);
  w.add(proto_field_t::flipCommands, shade.flipCommands);
  w.add(proto_field_t::flipPosition, shade.flipPosition);
  w.add(proto_field_t::inGroup, shade.inGroup);
  w.add(proto_field_t::sunSensor, shade.sunSensor);
  w.add(proto_field_t::light, shade.light);
  w.add(proto_field_t::simMy, shade.simMy);
  w.add(proto_field_t::repeats, shade.repeats);
  w.add(proto_field_t::sortOrder, shade.sortOrder);
  w.add(proto_field_t::gpioUp, shade.gpioUp);
  w.add(proto_field_t::gpioDown, shade.gpioDown);
  w.add(proto_field_t::gpioMy, shade.gpioMy);
  w.add(proto_field_t::gpioLLTrigger, shade.gpioLLTrigger);
  if(shade.linkedRemoteCount == 0) return;
  w.beginList(proto_field_t::linkedRemotes);
  for(uint8_t i = 0; i < shade.linkedRemoteCount; i++) {
    w.beginItem();
    w.add(proto_field_t::remoteAddress, shade.linkedRemotes[i].remoteAddress);
    w.add(proto_field_t::lastRollingCode, shade.linkedRemotes[i].lastRollingCode);
    w.endItem();
  }
  w.endList();
}
void protoGroup(ProtoWriter &w, const group_snapshot_t &group) {
  w.add(proto_field_t::groupId, group.groupId);
  w.add(proto_field_t::roomId, group.roomId);
  w.add(proto_field_t::name, group.name);
  w.add(proto_field_t::remoteAddress, group.remoteAddress);
  w.add(proto_field_t::lastRollingCode, group.lastRollingCode);
  w.add(proto_field_t::bitLength, group.bitLength);
  w.add(proto_field_t::proto, group.proto);
  w.add(proto_field_t::sunSensor, group.sunSensor);
  w.add(proto_field_t::flipCommands, group.flipCommands);
  w.add(proto_field_t::flags, group.flags);
  w.add(proto_field_t::repeats, group.repeats);
  w.add(proto_field_t::sortOrder, group.sortOrder);
  if(group.linkedShadeCount == 0) return;
  w.beginList(proto_field_t::linkedShades);
  for(uint8_t i = 0; i < group.linkedShadeCount; i++) w.item(group.linkedShades[i]);
  w.endList();
}
void protoGroupState(ProtoWriter &w, uint8_t groupId, uint8_t flags, int8_t direction) {
  w.add(proto_field_t::groupId, groupId);
  w.add(proto_field_t::flags, flags);
  w.add(proto_field_t::direction, direction);
}

/*********************************************************************
 * Sync
 ********************************************************************/
// What closing a frame cut short takes: the list's ], the }} of d and the
// frame, and the terminator.
#define SYNC_CLOSE_RESERVE 4

sync_cursor_t beginSync(bool withSchema) {
  sync_cursor_t cur;
  cur.section = withSchema ? sync_section_t::types : sync_section_t::header;
  cur.index = 0;
  return cur;
}

static proto_field_t sectionField(sync_section_t section) {
  switch(section) {
    case sync_section_t::types: return proto_field_t::types;
    case sync_section_t::fields: return proto_field_t::fields;
    case sync_section_t::errors: return proto_field_t::errors;
    case sync_section_t::feeds: return proto_field_t::feeds;
    case sync_section_t::header: return proto_field_t::header;
    case sync_section_t::rooms: return proto_field_t::rooms;
    case sync_section_t::shades: return proto_field_t::shades;
    case sync_section_t::groups: return proto_field_t::groups;
    case sync_section_t::repeaters: return proto_field_t::repeaters;
    default: return proto_field_t::status;
  }
}
static uint8_t sectionSlots(sync_section_t section) {
  switch(section) {
    case sync_section_t::types: return protoTypeCount();
    case sync_section_t::fields: return static_cast<uint8_t>(proto_field_t::count);
    case sync_section_t::errors: return protoErrorCount();
    case sync_section_t::feeds: return protoFeedCount();
    case sync_section_t::rooms: return SOMFY_MAX_ROOMS;
    case sync_section_t::shades: return SOMFY_MAX_SHADES;
    case sync_section_t::groups: return SOMFY_MAX_GROUPS;
    case sync_section_t::repeaters: return SOMFY_MAX_REPEATERS;
    default: return 0;
  }
}
static void namedRow(JsonFormatter &json, const proto_named_t &row) {
  json.beginArray();
  json.addElem(row.number);
  json.addElem(row.name);
  json.endArray();
}
// Writes the item in slot of a list section.  False when the slot is empty.
static bool writeItem(SyncSource &src, ProtoWriter &w, sync_section_t section, uint8_t slot) {
  JsonFormatter &json = w.json;
  switch(section) {
    case sync_section_t::types: namedRow(json, protoTypeAt(slot)); return true;
    case sync_section_t::errors: namedRow(json, protoErrorAt(slot)); return true;
    case sync_section_t::feeds: namedRow(json, protoFeedAt(slot)); return true;
    case sync_section_t::fields: {
      const proto_field_info_t &f = protoField(static_cast<proto_field_t>(slot));
      const char kind[2] = {f.kind, '\0'};
      json.beginArray();
      json.addElem(f.key);
      json.addElem(f.name);
      json.addElem(kind);
      json.addElem(f.def);
      json.endArray();
      return true;
    }
    case sync_section_t::rooms: {
      room_snapshot_t room;
      if(!src.room(slot, room)) return false;
      w.beginItem();
      protoRoom(w, room);
      w.endItem();
      return true;
    }
    case sync_section_t::shades: {
      shade_snapshot_t shade;
      if(!src.shade(slot, shade)) return false;
      w.beginItem();
      protoShade(w, shade);
      w.endItem();
      return true;
    }
    case sync_section_t::groups: {
      group_snapshot_t group;
      if(!src.group(slot, group)) return false;
      w.beginItem();
      protoGroup(w, group);
      w.endItem();
      return true;
    }
    case sync_section_t::repeaters: {
      uint32_t address = 0;
      if(!src.repeater(slot, address)) return false;
      w.item(address);
      return true;
    }
    default:
      return false;
  }
}
static void nextSection(sync_cursor_t &cur) {
  cur.section = static_cast<sync_section_t>(static_cast<uint8_t>(cur.section) + 1);
  cur.index = 0;
}

size_t writeSyncFrame(SyncSource &src, sync_cursor_t &cur, char *buff, size_t size) {
  if(!cur.active()) return 0;
  BufferedJsonFormatter json;
  json.setBuffer(buff, size);
  json.logOverflow(false);
  ProtoWriter w(json);
  beginFrame(json, proto_type_t::sync);
  bool any = false;
  auto fits = [&json, size]() { return !json.overflowed() && json.length() + SYNC_CLOSE_RESERVE < size; };
  while(cur.active()) {
    if(cur.section == sync_section_t::header || cur.section == sync_section_t::status) {
      const json_mark_t m = json.mark();
      w.beginObject(sectionField(cur.section));
      if(cur.section == sync_section_t::header) src.header(w);
      else src.status(w);
      w.endObject();
      if(!fits()) {
        json.rollback(m);
        // Only fixed size values, so this can't happen short of a bug.
        if(!any) { ESP_LOGE(TAG, "Sync section %u doesn't fit in %u bytes", static_cast<uint8_t>(cur.section), (unsigned)size); nextSection(cur); continue; }
        break;
      }
      any = true;
      nextSection(cur);
      continue;
    }
    bool open = false;
    bool full = false;
    const uint8_t slots = sectionSlots(cur.section);
    while(cur.index < slots) {
      const json_mark_t m = json.mark();
      if(!open) w.beginList(sectionField(cur.section));
      if(!writeItem(src, w, cur.section, cur.index)) {
        json.rollback(m);
        cur.index++;
        continue;
      }
      if(!fits()) {
        json.rollback(m);
        if(!any) {
          ESP_LOGE(TAG, "Sync item %u of section %u doesn't fit in %u bytes, leaving it out", cur.index, static_cast<uint8_t>(cur.section), (unsigned)size);
          cur.index++;
          continue;
        }
        full = true;
        break;
      }
      open = true;
      any = true;
      cur.index++;
    }
    if(open) w.endList();
    if(full) break;
    nextSection(cur);
  }
  endFrame(json);
  return json.length();
}
