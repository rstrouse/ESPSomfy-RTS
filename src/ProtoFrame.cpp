#include "ProtoFrame.h"

void beginFrame(JsonFormatter &json, proto_type_t type, uint32_t id) {
  json.beginObject();
  json.addElem("v", (uint8_t)PROTO_VERSION);
  json.addElem("t", static_cast<uint8_t>(type));
  if(id) json.addElem("id", id);
  json.beginObject("d");
}
void endFrame(JsonFormatter &json) {
  json.endObject();
  json.endObject();
}
void writeOkReply(JsonFormatter &json, uint32_t id) {
  beginFrame(json, proto_type_t::reply, id);
  ProtoWriter(json).add(proto_field_t::ok, true);
  endFrame(json);
}
void writeErrorReply(JsonFormatter &json, uint32_t id, proto_error_t code, const char *desc) {
  beginFrame(json, proto_type_t::reply, id);
  ProtoWriter w(json);
  w.add(proto_field_t::ok, false);
  w.add(proto_field_t::code, static_cast<uint8_t>(code));
  w.add(proto_field_t::desc, desc);
  endFrame(json);
}
void writeHello(JsonFormatter &json, const char *fw) {
  beginFrame(json, proto_type_t::hello);
  ProtoWriter w(json);
  w.add(proto_field_t::fw, fw);
  w.add(proto_field_t::schemaHash, protoSchemaHash());
  endFrame(json);
}
