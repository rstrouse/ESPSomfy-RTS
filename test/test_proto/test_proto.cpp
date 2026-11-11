#include <unity.h>
#include <set>
#include <string>
#include <vector>
#include <ArduinoJson.h>
#include "ProtoSchema.h"
#include "ProtoFrame.h"
#include "ProtoSync.h"
#include "ProtoParse.h"

// Socket protocol 2: the schema table, the writer, the sync and reading what
// clients send (docs/PROTOCOL.md).

void setUp() {}
void tearDown() {}

static char buff[PROTO_MAX_FRAME];
static BufferedJsonFormatter fmt;

/*********************************************************************
 * The schema table
 ********************************************************************/
static void test_field_keys_and_names_are_unique() {
  std::set<std::string> keys, names;
  for(uint8_t i = 0; i < static_cast<uint8_t>(proto_field_t::count); i++) {
    const proto_field_info_t &f = protoField(static_cast<proto_field_t>(i));
    TEST_ASSERT_TRUE_MESSAGE(keys.insert(f.key).second, f.key);
    TEST_ASSERT_TRUE_MESSAGE(names.insert(f.name).second, f.name);
    TEST_ASSERT_TRUE_MESSAGE(strlen(f.key) >= 1 && strlen(f.key) <= 2, f.key);
  }
}
static void test_every_field_has_a_known_kind() {
  for(uint8_t i = 0; i < static_cast<uint8_t>(proto_field_t::count); i++) {
    const proto_field_info_t &f = protoField(static_cast<proto_field_t>(i));
    TEST_ASSERT_NOT_NULL_MESSAGE(strchr("binslo", f.kind), f.name);
  }
}
static void test_type_numbers_are_unique_and_in_their_sender_range() {
  std::set<uint8_t> numbers;
  for(uint8_t i = 0; i < protoTypeCount(); i++) {
    const proto_named_t &t = protoTypeAt(i);
    TEST_ASSERT_TRUE_MESSAGE(numbers.insert(t.number).second, t.name);
    if(t.number >= 128) TEST_ASSERT_EQUAL_CHAR_MESSAGE('d', t.sender, t.name);
    else if(t.number >= 16) TEST_ASSERT_EQUAL_CHAR_MESSAGE('c', t.sender, t.name);
    else TEST_ASSERT_TRUE_MESSAGE(t.number >= 1, t.name);
  }
}
static void test_type_names_are_unique() {
  // Clients look types up by name.
  std::set<std::string> names;
  for(uint8_t i = 0; i < protoTypeCount(); i++) TEST_ASSERT_TRUE_MESSAGE(names.insert(protoTypeAt(i).name).second, protoTypeAt(i).name);
}
static void test_every_op_says_what_becomes_of_it() {
  for(uint8_t i = 0; i < protoTypeCount(); i++) {
    const proto_named_t &t = protoTypeAt(i);
    const bool op = t.number >= 16 && t.number <= 127;
    TEST_ASSERT_EQUAL_MESSAGE(op, protoOpKind(t.number) != proto_op_t::none, t.name);
  }
  TEST_ASSERT_EQUAL(proto_op_t::shadeRequest, protoOpKind(static_cast<uint8_t>(proto_type_t::shadeCommand)));
  TEST_ASSERT_EQUAL(proto_op_t::remoteFrame, protoOpKind(static_cast<uint8_t>(proto_type_t::sendRemoteCommand)));
  TEST_ASSERT_EQUAL(proto_op_t::edit, protoOpKind(static_cast<uint8_t>(proto_type_t::saveShade)));
  TEST_ASSERT_EQUAL(proto_op_t::read, protoOpKind(static_cast<uint8_t>(proto_type_t::groupOptions)));
  TEST_ASSERT_EQUAL(proto_op_t::device, protoOpKind(static_cast<uint8_t>(proto_type_t::getSettings)));
  TEST_ASSERT_EQUAL(proto_op_t::device, protoOpKind(static_cast<uint8_t>(proto_type_t::saveSettings)));
  TEST_ASSERT_EQUAL(proto_op_t::device, protoOpKind(static_cast<uint8_t>(proto_type_t::scanWifi)));
  TEST_ASSERT_EQUAL(proto_op_t::device, protoOpKind(static_cast<uint8_t>(proto_type_t::beginFrequencyScan)));
  TEST_ASSERT_EQUAL(proto_op_t::device, protoOpKind(static_cast<uint8_t>(proto_type_t::endFrequencyScan)));
  TEST_ASSERT_EQUAL(proto_op_t::device, protoOpKind(static_cast<uint8_t>(proto_type_t::reboot)));
  TEST_ASSERT_EQUAL(proto_op_t::none, protoOpKind(250));
}
static void test_a_field_by_its_key() {
  proto_field_t f;
  TEST_ASSERT_TRUE(protoFieldOfKey("s", f));
  TEST_ASSERT_EQUAL(proto_field_t::shadeId, f);
  TEST_ASSERT_TRUE(protoFieldOfKey("vc", f));
  TEST_ASSERT_EQUAL(proto_field_t::validation, f);
  TEST_ASSERT_FALSE(protoFieldOfKey("zz", f));
}
static void test_error_and_feed_numbers_are_unique() {
  std::set<uint8_t> errors, feeds;
  for(uint8_t i = 0; i < protoErrorCount(); i++) TEST_ASSERT_TRUE(errors.insert(protoErrorAt(i).number).second);
  for(uint8_t i = 0; i < protoFeedCount(); i++) TEST_ASSERT_TRUE(feeds.insert(protoFeedAt(i).number).second);
  TEST_ASSERT_FALSE(errors.count(0));
}
static void test_bootstrap_keys_are_fixed() {
  // A client needs these before it has the schema.
  TEST_ASSERT_EQUAL_STRING("fw", protoField(proto_field_t::fw).key);
  TEST_ASSERT_EQUAL_STRING("sh", protoField(proto_field_t::schemaHash).key);
  TEST_ASSERT_EQUAL_STRING("T", protoField(proto_field_t::types).key);
  TEST_ASSERT_EQUAL_STRING("F", protoField(proto_field_t::fields).key);
  TEST_ASSERT_EQUAL_STRING("E", protoField(proto_field_t::errors).key);
  TEST_ASSERT_EQUAL_STRING("Q", protoField(proto_field_t::feeds).key);
}
static void test_schema_hash_is_eight_hex_digits_and_stable() {
  const std::string first = protoSchemaHash();
  TEST_ASSERT_EQUAL_UINT32(8, first.size());
  TEST_ASSERT_EQUAL_UINT32(std::string::npos, first.find_first_not_of("0123456789abcdef"));
  TEST_ASSERT_EQUAL_STRING(first.c_str(), protoSchemaHash());
}
static void test_field_by_key() {
  TEST_ASSERT_EQUAL_STRING("shadeId", protoFieldByKey("s")->name);
  TEST_ASSERT_NULL(protoFieldByKey("??"));
}

/*********************************************************************
 * The writer
 ********************************************************************/
static std::string writeObject(void (*fill)(ProtoWriter &)) {
  fmt.setBuffer(buff, sizeof(buff));
  ProtoWriter w(fmt);
  fmt.beginObject();
  fill(w);
  fmt.endObject();
  return buff;
}
static void test_values_at_their_default_are_left_out() {
  TEST_ASSERT_EQUAL_STRING("{}", writeObject([](ProtoWriter &w) {
    w.add(proto_field_t::position, (int8_t)0);
    w.add(proto_field_t::myPos, (int8_t)-1);
    w.add(proto_field_t::repeats, (uint8_t)1);
    w.add(proto_field_t::paired, false);
    w.add(proto_field_t::hasOtaSlot, true);  // a bool whose default is true
    w.add(proto_field_t::name, "");
    w.add(proto_field_t::name, (const char *)nullptr);
  }).c_str());
}
static void test_values_off_their_default_are_written_by_key() {
  TEST_ASSERT_EQUAL_STRING("{\"p\":50,\"m\":0,\"i\":1,\"os\":0,\"n\":\"Office\",\"a\":4294967295,\"q\":-5}", writeObject([](ProtoWriter &w) {
    w.add(proto_field_t::position, (int8_t)50);
    w.add(proto_field_t::myPos, (int8_t)0);
    w.add(proto_field_t::paired, true);
    w.add(proto_field_t::hasOtaSlot, false);
    w.add(proto_field_t::name, "Office");
    w.add(proto_field_t::remoteAddress, (uint32_t)0xFFFFFFFF);
    w.add(proto_field_t::tiltPosition, (int8_t)-5);
  }).c_str());
}

/*********************************************************************
 * Frames
 ********************************************************************/
static void test_hello_frame() {
  fmt.setBuffer(buff, sizeof(buff));
  writeHello(fmt, "3.1.0");
  const std::string expected = std::string("{\"v\":2,\"t\":1,\"d\":{\"fw\":\"3.1.0\",\"sh\":\"") + protoSchemaHash() + "\"}}";
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), buff);
}
static void test_error_reply_frame() {
  fmt.setBuffer(buff, sizeof(buff));
  writeErrorReply(fmt, 17, proto_error_t::notFound, "Shade not found");
  // ok is 0, its default, so it is left out.
  TEST_ASSERT_EQUAL_STRING("{\"v\":2,\"t\":2,\"id\":17,\"d\":{\"e\":4,\"x\":\"Shade not found\"}}", buff);
}
static void test_ok_reply_frame() {
  fmt.setBuffer(buff, sizeof(buff));
  writeOkReply(fmt, 9);
  TEST_ASSERT_EQUAL_STRING("{\"v\":2,\"t\":2,\"id\":9,\"d\":{\"k\":1}}", buff);
}

/*********************************************************************
 * Items
 ********************************************************************/
static shade_snapshot_t restingShade(uint8_t id) {
  shade_snapshot_t s = {};
  s.shadeId = id;
  s.myPos = s.myTiltPos = -1;
  s.stepSize = 100;
  s.bitLength = 56;
  s.repeats = 1;
  return s;
}
static void test_shade_state_of_a_resting_shade_is_its_id() {
  const shade_snapshot_t s = restingShade(3);
  fmt.setBuffer(buff, sizeof(buff));
  ProtoWriter w(fmt);
  fmt.beginObject();
  protoShadeState(w, s);
  fmt.endObject();
  TEST_ASSERT_EQUAL_STRING("{\"s\":3}", buff);
}
static void test_group_lists_shade_ids() {
  group_snapshot_t g = {};
  g.groupId = 2;
  g.bitLength = 56;
  g.repeats = 1;
  g.linkedShadeCount = 3;
  g.linkedShades[0] = 4;
  g.linkedShades[1] = 7;
  g.linkedShades[2] = 9;
  fmt.setBuffer(buff, sizeof(buff));
  ProtoWriter w(fmt);
  fmt.beginObject();
  protoGroup(w, g);
  fmt.endObject();
  TEST_ASSERT_EQUAL_STRING("{\"g\":2,\"ls\":[4,7,9]}", buff);
}

/*********************************************************************
 * Sync
 ********************************************************************/
// The widest name: 20 control characters, each escaped to \u00XX.
static void widestName(char *name) {
  memset(name, 0x01, 20);
  name[20] = '\0';
}
static shade_snapshot_t widestShade(uint8_t id) {
  shade_snapshot_t s = {};
  s.shadeId = id;
  s.roomId = 255;
  widestName(s.name);
  s.remoteAddress = s.upTime = s.downTime = s.tiltTime = 0xFFFFFFFF;
  s.stepSize = s.lastRollingCode = 0xFFFF;
  s.position = s.target = s.myPos = s.direction = -128;
  s.tiltPosition = s.tiltTarget = s.myTiltPos = s.tiltDirection = -128;
  s.tiltType = s.shadeType = s.bitLength = s.proto = s.flags = s.repeats = 255;
  s.gpioUp = s.gpioDown = s.gpioMy = 255;
  s.sortOrder = -128;
  s.paired = s.flipCommands = s.flipPosition = s.inGroup = true;
  s.sunSensor = s.light = s.simMy = s.gpioLLTrigger = true;
  s.linkedRemoteCount = SOMFY_MAX_LINKED_REMOTES;
  for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) s.linkedRemotes[j] = {0xFFFFFFFF, 0xFFFF};
  return s;
}
// A source over plain arrays; a slot with id 0 is empty.
class ArraySource : public SyncSource {
  public:
    room_snapshot_t rooms[SOMFY_MAX_ROOMS] = {};
    shade_snapshot_t shades[SOMFY_MAX_SHADES] = {};
    group_snapshot_t groups[SOMFY_MAX_GROUPS] = {};
    uint32_t repeaters[SOMFY_MAX_REPEATERS] = {};
    bool room(uint8_t slot, room_snapshot_t &out) override { out = rooms[slot]; return out.roomId != 0; }
    bool shade(uint8_t slot, shade_snapshot_t &out) override { out = shades[slot]; return out.shadeId != 0; }
    bool group(uint8_t slot, group_snapshot_t &out) override { out = groups[slot]; return out.groupId != 0; }
    bool repeater(uint8_t slot, uint32_t &address) override { address = repeaters[slot]; return address != 0; }
    void header(ProtoWriter &w) override { w.add(proto_field_t::maxShades, (uint8_t)SOMFY_MAX_SHADES); }
    void status(ProtoWriter &w) override { w.add(proto_field_t::freeHeap, (uint32_t)80000); }
};
static void fillWidest(ArraySource &src) {
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    src.rooms[i].roomId = i + 1;
    widestName(src.rooms[i].name);
    src.rooms[i].sortOrder = -128;
  }
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) src.shades[i] = widestShade(i + 1);
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    group_snapshot_t &g = src.groups[i];
    g.groupId = i + 1;
    g.roomId = 255;
    widestName(g.name);
    g.remoteAddress = 0xFFFFFFFF;
    g.lastRollingCode = 0xFFFF;
    g.bitLength = g.proto = g.flags = g.repeats = 255;
    g.sortOrder = -128;
    g.linkedShadeCount = SOMFY_MAX_GROUPED_SHADES;
    for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) g.linkedShades[j] = j + 1;
  }
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) src.repeaters[i] = 0xFFFFFFFF;
}
// Every frame of a sync, parsed.  Each must be valid JSON within size bytes.
static std::vector<JsonDocument> runSync(SyncSource &src, bool withSchema, size_t size) {
  std::vector<JsonDocument> frames;
  static char frame[PROTO_MAX_FRAME];
  sync_cursor_t cur = beginSync(withSchema);
  for(int guard = 0; guard < 1000; guard++) {
    const size_t len = writeSyncFrame(src, cur, frame, size);
    if(len == 0) return frames;
    TEST_ASSERT_LESS_THAN_UINT32(size, len);
    JsonDocument doc;
    TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, deserializeJson(doc, frame, len).code(), frame);
    TEST_ASSERT_EQUAL(2, doc["v"].as<int>());
    TEST_ASSERT_EQUAL(static_cast<int>(proto_type_t::sync), doc["t"].as<int>());
    frames.push_back(std::move(doc));
  }
  TEST_FAIL_MESSAGE("the sync never ended");
  return frames;
}
// How many items a section has over every frame.
static size_t count(const std::vector<JsonDocument> &frames, proto_field_t section) {
  size_t n = 0;
  for(const JsonDocument &doc : frames) {
    JsonVariantConst v = doc["d"][protoField(section).key];
    if(v.is<JsonArrayConst>()) n += v.as<JsonArrayConst>().size();
    else if(!v.isNull()) n++;
  }
  return n;
}
static void test_full_sync_sends_every_item_whole() {
  static ArraySource src;
  fillWidest(src);
  const std::vector<JsonDocument> frames = runSync(src, true, PROTO_MAX_FRAME);
  TEST_ASSERT_GREATER_THAN_UINT32(1, frames.size());
  TEST_ASSERT_EQUAL_UINT32(protoTypeCount(), count(frames, proto_field_t::types));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint8_t>(proto_field_t::count), count(frames, proto_field_t::fields));
  TEST_ASSERT_EQUAL_UINT32(protoErrorCount(), count(frames, proto_field_t::errors));
  TEST_ASSERT_EQUAL_UINT32(protoFeedCount(), count(frames, proto_field_t::feeds));
  TEST_ASSERT_EQUAL_UINT32(1, count(frames, proto_field_t::header));
  TEST_ASSERT_EQUAL_UINT32(SOMFY_MAX_ROOMS, count(frames, proto_field_t::rooms));
  TEST_ASSERT_EQUAL_UINT32(SOMFY_MAX_SHADES, count(frames, proto_field_t::shades));
  TEST_ASSERT_EQUAL_UINT32(SOMFY_MAX_GROUPS, count(frames, proto_field_t::groups));
  TEST_ASSERT_EQUAL_UINT32(SOMFY_MAX_REPEATERS, count(frames, proto_field_t::repeaters));
  TEST_ASSERT_EQUAL_UINT32(1, count(frames, proto_field_t::status));
  // Every shade comes out with all its linked remotes.
  for(const JsonDocument &doc : frames)
    for(JsonVariantConst s : doc["d"]["S"].as<JsonArrayConst>())
      TEST_ASSERT_EQUAL_UINT32(SOMFY_MAX_LINKED_REMOTES, s["lk"].as<JsonArrayConst>().size());
}
static void test_a_client_with_the_schema_skips_it() {
  static ArraySource src;
  src = ArraySource();
  src.shades[0] = restingShade(1);
  const std::vector<JsonDocument> frames = runSync(src, false, PROTO_MAX_FRAME);
  TEST_ASSERT_EQUAL_UINT32(0, count(frames, proto_field_t::types));
  TEST_ASSERT_EQUAL_UINT32(0, count(frames, proto_field_t::fields));
  TEST_ASSERT_EQUAL_UINT32(1, frames.size());
  TEST_ASSERT_EQUAL(1, frames[0]["d"]["S"][0]["s"].as<int>());
}
static void test_empty_slots_are_skipped() {
  static ArraySource src;
  src = ArraySource();
  src.shades[5] = restingShade(6);
  src.shades[20] = restingShade(21);
  src.rooms[3].roomId = 4;
  const std::vector<JsonDocument> frames = runSync(src, false, PROTO_MAX_FRAME);
  TEST_ASSERT_EQUAL_UINT32(2, count(frames, proto_field_t::shades));
  TEST_ASSERT_EQUAL_UINT32(1, count(frames, proto_field_t::rooms));
  TEST_ASSERT_EQUAL_UINT32(0, count(frames, proto_field_t::groups));
  TEST_ASSERT_TRUE(frames[0]["d"]["X"].isNull());
}
static void test_a_small_frame_takes_more_frames_and_keeps_items_whole() {
  static ArraySource src;
  fillWidest(src);
  const std::vector<JsonDocument> big = runSync(src, false, PROTO_MAX_FRAME);
  const std::vector<JsonDocument> small = runSync(src, false, 1400);
  TEST_ASSERT_GREATER_THAN_UINT32(big.size(), small.size());
  TEST_ASSERT_EQUAL_UINT32(SOMFY_MAX_SHADES, count(small, proto_field_t::shades));
  TEST_ASSERT_EQUAL_UINT32(SOMFY_MAX_GROUPS, count(small, proto_field_t::groups));
}
static void test_an_item_too_big_for_a_frame_is_left_out_and_the_rest_go() {
  static ArraySource src;
  src = ArraySource();
  src.shades[0] = widestShade(1);
  src.shades[1] = restingShade(2);
  // Too small for the widest shade, big enough for a resting one.
  const std::vector<JsonDocument> frames = runSync(src, false, 200);
  TEST_ASSERT_EQUAL_UINT32(1, count(frames, proto_field_t::shades));
}
static void test_the_largest_shade_fits_one_frame() {
  // test_full_sync covers it in the sync; this is the shade event on its own.
  const shade_snapshot_t s = widestShade(1);
  fmt.setBuffer(buff, sizeof(buff));
  ProtoWriter w(fmt);
  beginFrame(fmt, proto_type_t::shade);
  protoShade(w, s);
  endFrame(fmt);
  TEST_ASSERT_FALSE(fmt.overflowed());
  TEST_ASSERT_LESS_THAN_UINT32(PROTO_MAX_FRAME / 2, fmt.length());
}

/*********************************************************************
 * What clients send
 ********************************************************************/
static proto_error_t parse(const char *text, proto_message_t &req) {
  JsonDocument doc;
  return parseClientFrame(text, strlen(text), doc, req);
}
static void test_next_is_read() {
  proto_message_t req;
  TEST_ASSERT_EQUAL(proto_error_t::none, parse("{\"v\":2,\"t\":3}", req));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto_type_t::next), req.type);
  TEST_ASSERT_EQUAL_UINT32(0, req.id);
}
static void test_subscribe_carries_its_id_and_payload() {
  proto_message_t req;
  JsonDocument doc;
  const char *text = "{\"v\":2,\"t\":6,\"id\":9,\"d\":{\"fe\":1}}";
  TEST_ASSERT_EQUAL(proto_error_t::none, parseClientFrame(text, strlen(text), doc, req));
  TEST_ASSERT_EQUAL_UINT32(9, req.id);
  TEST_ASSERT_EQUAL_INT32(1, protoInt(doc["d"], proto_field_t::feed));
}
static void test_bad_frames_are_refused() {
  proto_message_t req;
  TEST_ASSERT_EQUAL(proto_error_t::badRequest, parse("not json", req));
  TEST_ASSERT_EQUAL(proto_error_t::badRequest, parse("[2,3]", req));
  TEST_ASSERT_EQUAL(proto_error_t::badRequest, parse("{\"v\":1,\"t\":3}", req));
  TEST_ASSERT_EQUAL(proto_error_t::badRequest, parse("{\"v\":2}", req));
  TEST_ASSERT_EQUAL(proto_error_t::badRequest, parse("{\"v\":2,\"t\":\"next\"}", req));
  TEST_ASSERT_EQUAL(proto_error_t::badRequest, parse("{\"v\":2,\"t\":3,\"d\":[]}", req));
  TEST_ASSERT_EQUAL(proto_error_t::badRequest, parse("{\"v\":2,\"t\":3,\"id\":\"x\"}", req));
}
static void test_types_a_client_may_not_send_are_unknown() {
  proto_message_t req;
  // A device event, and a number nobody uses.
  TEST_ASSERT_EQUAL(proto_error_t::unknownOp, parse("{\"v\":2,\"t\":128,\"id\":5}", req));
  TEST_ASSERT_EQUAL_UINT32(5, req.id);
  TEST_ASSERT_EQUAL(proto_error_t::unknownOp, parse("{\"v\":2,\"t\":99,\"id\":6}", req));
}
static void test_a_left_out_field_reads_as_its_default() {
  JsonDocument doc;
  deserializeJson(doc, "{\"p\":40}");
  JsonObjectConst d = doc.as<JsonObjectConst>();
  TEST_ASSERT_EQUAL_INT32(40, protoInt(d, proto_field_t::position));
  TEST_ASSERT_EQUAL_INT32(-1, protoInt(d, proto_field_t::myPos));
  TEST_ASSERT_FALSE(protoBool(d, proto_field_t::paired));
  TEST_ASSERT_NULL(protoString(d, proto_field_t::name));
  TEST_ASSERT_TRUE(protoHas(d, proto_field_t::position));
  TEST_ASSERT_FALSE(protoHas(d, proto_field_t::target));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_field_keys_and_names_are_unique);
  RUN_TEST(test_every_field_has_a_known_kind);
  RUN_TEST(test_type_numbers_are_unique_and_in_their_sender_range);
  RUN_TEST(test_type_names_are_unique);
  RUN_TEST(test_every_op_says_what_becomes_of_it);
  RUN_TEST(test_a_field_by_its_key);
  RUN_TEST(test_error_and_feed_numbers_are_unique);
  RUN_TEST(test_bootstrap_keys_are_fixed);
  RUN_TEST(test_schema_hash_is_eight_hex_digits_and_stable);
  RUN_TEST(test_field_by_key);
  RUN_TEST(test_values_at_their_default_are_left_out);
  RUN_TEST(test_values_off_their_default_are_written_by_key);
  RUN_TEST(test_hello_frame);
  RUN_TEST(test_error_reply_frame);
  RUN_TEST(test_ok_reply_frame);
  RUN_TEST(test_shade_state_of_a_resting_shade_is_its_id);
  RUN_TEST(test_group_lists_shade_ids);
  RUN_TEST(test_full_sync_sends_every_item_whole);
  RUN_TEST(test_a_client_with_the_schema_skips_it);
  RUN_TEST(test_empty_slots_are_skipped);
  RUN_TEST(test_a_small_frame_takes_more_frames_and_keeps_items_whole);
  RUN_TEST(test_an_item_too_big_for_a_frame_is_left_out_and_the_rest_go);
  RUN_TEST(test_the_largest_shade_fits_one_frame);
  RUN_TEST(test_next_is_read);
  RUN_TEST(test_subscribe_carries_its_id_and_payload);
  RUN_TEST(test_bad_frames_are_refused);
  RUN_TEST(test_types_a_client_may_not_send_are_unknown);
  RUN_TEST(test_a_left_out_field_reads_as_its_default);
  return UNITY_END();
}
