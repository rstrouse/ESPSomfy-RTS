#include <stdio.h>
#include <string.h>
#include "ProtoEdits.h"
#include "ProtoParse.h"

struct edit_op_t {
  proto_type_t type;
  model_edit_type_t edit;
};
static const edit_op_t EDIT_OPS[] = {
  {proto_type_t::addRoom, model_edit_type_t::add_room},
  {proto_type_t::addShade, model_edit_type_t::add_shade},
  {proto_type_t::addGroup, model_edit_type_t::add_group},
  {proto_type_t::saveRoom, model_edit_type_t::save_room},
  {proto_type_t::saveShade, model_edit_type_t::save_shade},
  {proto_type_t::saveGroup, model_edit_type_t::save_group},
  {proto_type_t::deleteRoom, model_edit_type_t::delete_room},
  {proto_type_t::deleteShade, model_edit_type_t::delete_shade},
  {proto_type_t::deleteGroup, model_edit_type_t::delete_group},
  {proto_type_t::linkRemote, model_edit_type_t::link_remote},
  {proto_type_t::unlinkRemote, model_edit_type_t::unlink_remote},
  {proto_type_t::linkToGroup, model_edit_type_t::link_to_group},
  {proto_type_t::unlinkFromGroup, model_edit_type_t::unlink_from_group},
  {proto_type_t::linkRepeater, model_edit_type_t::link_repeater},
  {proto_type_t::unlinkRepeater, model_edit_type_t::unlink_repeater},
  {proto_type_t::setPaired, model_edit_type_t::set_paired},
  {proto_type_t::roomSortOrder, model_edit_type_t::room_order},
  {proto_type_t::shadeSortOrder, model_edit_type_t::shade_order},
  {proto_type_t::groupSortOrder, model_edit_type_t::group_order}
};
bool protoEditType(uint8_t type, model_edit_type_t &out) {
  for(const edit_op_t &op : EDIT_OPS) {
    if(static_cast<uint8_t>(op.type) != type) continue;
    out = op.edit;
    return true;
  }
  return false;
}
const char *parseReadPayload(uint8_t type, JsonObjectConst d, uint8_t &groupId) {
  groupId = 0;
  if(type != static_cast<uint8_t>(proto_type_t::groupOptions)) return nullptr;
  JsonVariantConst v = d[protoField(proto_field_t::groupId).key];
  if(v.isNull()) return "groupId is missing";
  if(!v.is<uint8_t>()) return "groupId must be 0-255";
  groupId = v.as<uint8_t>();
  return nullptr;
}

// The fields each edit's route reads, so a field it would ignore is refused
// rather than dropped without a word.  A shade's flags aren't among them:
// they hold light, which is set on its own, and the sun and wind state, which
// setSensor sets.  A group's sunSensor is worked out from its shades.
static const proto_field_t ROOM_FIELDS[] = {proto_field_t::name, proto_field_t::sortOrder};
static const proto_field_t SHADE_FIELDS[] = {
  proto_field_t::name, proto_field_t::roomId, proto_field_t::remoteAddress, proto_field_t::shadeType,
  proto_field_t::tiltType, proto_field_t::bitLength, proto_field_t::proto, proto_field_t::upTime,
  proto_field_t::downTime, proto_field_t::tiltTime, proto_field_t::stepSize, proto_field_t::flipCommands,
  proto_field_t::flipPosition, proto_field_t::sunSensor, proto_field_t::light, proto_field_t::simMy,
  proto_field_t::repeats, proto_field_t::gpioUp, proto_field_t::gpioDown, proto_field_t::gpioMy,
  proto_field_t::gpioLLTrigger
};
static const proto_field_t GROUP_FIELDS[] = {
  proto_field_t::name, proto_field_t::roomId, proto_field_t::remoteAddress, proto_field_t::bitLength,
  proto_field_t::proto, proto_field_t::flipCommands, proto_field_t::repeats
};
template<size_t N>
static bool in(proto_field_t field, const proto_field_t (&fields)[N]) {
  for(proto_field_t f : fields)
    if(f == field) return true;
  return false;
}
static bool takes(model_edit_type_t type, proto_field_t f) {
  switch(type) {
    case model_edit_type_t::add_room: return in(f, ROOM_FIELDS);
    case model_edit_type_t::save_room: return f == proto_field_t::roomId || in(f, ROOM_FIELDS);
    case model_edit_type_t::add_shade: return in(f, SHADE_FIELDS);
    case model_edit_type_t::save_shade: return f == proto_field_t::shadeId || in(f, SHADE_FIELDS);
    case model_edit_type_t::add_group: return in(f, GROUP_FIELDS);
    case model_edit_type_t::save_group: return f == proto_field_t::groupId || in(f, GROUP_FIELDS);
    case model_edit_type_t::delete_room: return f == proto_field_t::roomId;
    case model_edit_type_t::delete_shade: return f == proto_field_t::shadeId;
    case model_edit_type_t::delete_group: return f == proto_field_t::groupId;
    case model_edit_type_t::link_remote:
      return f == proto_field_t::shadeId || f == proto_field_t::remoteAddress || f == proto_field_t::lastRollingCode;
    case model_edit_type_t::unlink_remote: return f == proto_field_t::shadeId || f == proto_field_t::remoteAddress;
    case model_edit_type_t::link_to_group:
    case model_edit_type_t::unlink_from_group: return f == proto_field_t::groupId || f == proto_field_t::shadeId;
    case model_edit_type_t::link_repeater:
    case model_edit_type_t::unlink_repeater: return f == proto_field_t::remoteAddress;
    case model_edit_type_t::set_paired: return f == proto_field_t::shadeId || f == proto_field_t::paired;
    default: return false;
  }
}
// The name a field has in the edit route's request.  Only linkRemote's rolling
// code is named apart from the dictionary.
static const char *routeName(model_edit_type_t type, proto_field_t f) {
  if(type == model_edit_type_t::link_remote && f == proto_field_t::lastRollingCode) return "rollingCode";
  return protoField(f).name;
}
// What the reply's desc points at; every parse runs on async_tcp, one at a
// time.
static char why[64];
const char *expandEditPayload(model_edit_type_t type, JsonObjectConst d, JsonDocument &doc) {
  doc.clear();
  switch(type) {
    case model_edit_type_t::room_order:
    case model_edit_type_t::shade_order:
    case model_edit_type_t::group_order: {
      // The route takes the ids on their own, in their new order.
      for(JsonPairConst kv : d) {
        if(strcmp(kv.key().c_str(), protoField(proto_field_t::ids).key) == 0) continue;
        return "A sort order takes only ids";
      }
      JsonVariantConst ids = d[protoField(proto_field_t::ids).key];
      if(!ids.is<JsonArrayConst>()) return "ids is missing";
      for(JsonVariantConst id : ids.as<JsonArrayConst>())
        if(!id.is<uint8_t>()) return "ids must be a list of ids";
      doc.set(ids);
      return doc.overflowed() ? "Too large" : nullptr;
    }
    default:
      break;
  }
  JsonObject obj = doc.to<JsonObject>();
  for(JsonPairConst kv : d) {
    proto_field_t field;
    if(!protoFieldOfKey(kv.key().c_str(), field)) {
      snprintf(why, sizeof(why), "Unknown field %s", kv.key().c_str());
      return why;
    }
    const proto_field_info_t &f = protoField(field);
    if(!takes(type, field)) {
      snprintf(why, sizeof(why), "%s can't be set by this edit", f.name);
      return why;
    }
    if(!protoRouteValue(f, kv.value(), obj[routeName(type, field)].to<JsonVariant>())) {
      snprintf(why, sizeof(why), "%s is not a %s", f.name, protoKindName(f.kind));
      return why;
    }
  }
  // A left-out field is not given, so it can't stand for unpaired.
  if(type == model_edit_type_t::set_paired && obj["paired"].isNull()) return "paired is missing";
  return doc.overflowed() ? "Too large" : nullptr;
}
