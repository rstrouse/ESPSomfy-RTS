#include <stdio.h>
#include <string.h>
#include "ProtoSchema.h"

#define PROTO_FIELD_ROW(name, key, kind, def) {#name, key, kind, def},
static const proto_field_info_t FIELDS[] = { PROTO_FIELDS(PROTO_FIELD_ROW) };
#undef PROTO_FIELD_ROW
#define PROTO_TYPE_ROW(name, number, sender, op) {number, #name, sender, op},
static const proto_named_t TYPES[] = { PROTO_TYPES(PROTO_TYPE_ROW) };
#undef PROTO_TYPE_ROW
// Errors and feeds have no sender or op.
#define PROTO_NAMED_ROW(name, number) {number, #name, 0, 0},
static const proto_named_t ERRORS[] = { PROTO_ERRORS(PROTO_NAMED_ROW) };
static const proto_named_t FEEDS[] = { PROTO_FEEDS(PROTO_NAMED_ROW) };
#undef PROTO_NAMED_ROW
static_assert(sizeof(FIELDS) / sizeof(FIELDS[0]) == static_cast<size_t>(proto_field_t::count), "a field without a row");

const proto_field_info_t &protoField(proto_field_t field) { return FIELDS[static_cast<uint8_t>(field)]; }
const proto_field_info_t *protoFieldByKey(const char *key) {
  for(const proto_field_info_t &f : FIELDS)
    if(strcmp(f.key, key) == 0) return &f;
  return nullptr;
}
bool protoFieldOfKey(const char *key, proto_field_t &out) {
  const proto_field_info_t *f = protoFieldByKey(key);
  if(!f) return false;
  out = static_cast<proto_field_t>(f - FIELDS);
  return true;
}
uint8_t protoTypeCount() { return sizeof(TYPES) / sizeof(TYPES[0]); }
const proto_named_t &protoTypeAt(uint8_t index) { return TYPES[index]; }
uint8_t protoErrorCount() { return sizeof(ERRORS) / sizeof(ERRORS[0]); }
const proto_named_t &protoErrorAt(uint8_t index) { return ERRORS[index]; }
uint8_t protoFeedCount() { return sizeof(FEEDS) / sizeof(FEEDS[0]); }
const proto_named_t &protoFeedAt(uint8_t index) { return FEEDS[index]; }
const char *protoTypeName(uint8_t number) {
  for(const proto_named_t &t : TYPES)
    if(t.number == number) return t.name;
  return nullptr;
}
proto_op_t protoOpKind(uint8_t type) {
  for(const proto_named_t &t : TYPES)
    if(t.number == type) return static_cast<proto_op_t>(t.op);
  return proto_op_t::none;
}
bool protoClientMaySend(uint8_t type) {
  for(const proto_named_t &t : TYPES)
    if(t.number == type) return t.sender == 'c' || t.sender == 'b';
  return false;
}

static uint32_t fnv(uint32_t hash, const char *text) {
  for(; *text; text++) {
    hash ^= (uint8_t)*text;
    hash *= 16777619u;
  }
  return hash;
}
static uint32_t fnvNamed(uint32_t hash, const proto_named_t &row) {
  char num[12];
  snprintf(num, sizeof(num), "%u:", row.number);
  hash = fnv(hash, num);
  hash = fnv(hash, row.name);
  return fnv(hash, ";");
}
const char *protoSchemaHash() {
  static char hex[9] = {0};
  if(hex[0]) return hex;
  uint32_t hash = 2166136261u;
  for(const proto_named_t &t : TYPES) hash = fnvNamed(hash, t);
  for(const proto_field_info_t &f : FIELDS) {
    char row[64];
    snprintf(row, sizeof(row), "%s:%s:%c:%ld;", f.name, f.key, f.kind, (long)f.def);
    hash = fnv(hash, row);
  }
  for(const proto_named_t &e : ERRORS) hash = fnvNamed(hash, e);
  for(const proto_named_t &q : FEEDS) hash = fnvNamed(hash, q);
  snprintf(hex, sizeof(hex), "%08lx", (unsigned long)hash);
  return hex;
}

void ProtoWriter::addInt(const proto_field_info_t &f, int64_t value) {
  if(value == f.def) return;
  if(value >= INT32_MIN && value <= INT32_MAX) this->json.addElem(f.key, (int32_t)value);
  else this->json.addElem(f.key, (uint32_t)value);
}
void ProtoWriter::item(int64_t value) {
  if(value >= INT32_MIN && value <= INT32_MAX) this->json.addElem((int32_t)value);
  else this->json.addElem((uint32_t)value);
}
