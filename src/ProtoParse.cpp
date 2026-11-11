#include "ProtoParse.h"

proto_error_t parseClientFrame(const char *text, size_t len, JsonDocument &doc, proto_message_t &out) {
  out = proto_message_t();
  if(len > PROTO_MAX_CLIENT_FRAME) return proto_error_t::badRequest;
  if(deserializeJson(doc, text, len) != DeserializationError::Ok) return proto_error_t::badRequest;
  if(!doc.is<JsonObject>()) return proto_error_t::badRequest;
  JsonVariantConst id = doc["id"];
  if(id.is<uint32_t>()) out.id = id.as<uint32_t>();
  else if(!id.isNull()) return proto_error_t::badRequest;
  if(!doc["v"].is<int>() || doc["v"].as<int>() != PROTO_VERSION) return proto_error_t::badRequest;
  JsonVariantConst t = doc["t"];
  if(!t.is<int>() || t.as<int>() < 1 || t.as<int>() > 255) return proto_error_t::badRequest;
  out.type = static_cast<uint8_t>(t.as<int>());
  if(!protoClientMaySend(out.type)) return proto_error_t::unknownOp;
  if(protoIsOp(out.type) && out.id == 0) return proto_error_t::badRequest;
  JsonVariantConst d = doc["d"];
  if(!d.isNull() && !d.is<JsonObjectConst>()) return proto_error_t::badRequest;
  return proto_error_t::none;
}

int32_t protoInt(JsonObjectConst d, proto_field_t field) {
  const proto_field_info_t &f = protoField(field);
  JsonVariantConst v = d[f.key];
  return v.is<int32_t>() ? v.as<int32_t>() : f.def;
}
bool protoBool(JsonObjectConst d, proto_field_t field) {
  const proto_field_info_t &f = protoField(field);
  JsonVariantConst v = d[f.key];
  return v.is<int>() ? v.as<int>() != 0 : f.def != 0;
}
const char *protoString(JsonObjectConst d, proto_field_t field) {
  JsonVariantConst v = d[protoField(field).key];
  return v.is<const char *>() ? v.as<const char *>() : nullptr;
}
bool protoHas(JsonObjectConst d, proto_field_t field) { return !d[protoField(field).key].isNull(); }

bool protoRouteValue(const proto_field_info_t &f, JsonVariantConst v, JsonVariant out) {
  switch(f.kind) {
    case 'b':
      // Booleans are 1 and 0; the routes read true and false.
      if(!v.is<int>() || (v.as<int>() != 0 && v.as<int>() != 1)) return false;
      out.set(v.as<int>() == 1);
      return true;
    case 'i':
      if(v.is<int64_t>() || v.is<uint64_t>()) return out.set(v);
      return false;
    case 'n':
      if(v.is<float>()) return out.set(v);
      return false;
    case 's':
      if(v.is<const char *>()) return out.set(v);
      return false;
    default:
      return false;
  }
}
const char *protoKindName(char kind) {
  switch(kind) {
    case 'b': return "boolean (1 or 0)";
    case 'i': return "whole number";
    case 'n': return "number";
    case 's': return "string";
    case 'l': return "list";
    default: return "object";
  }
}
