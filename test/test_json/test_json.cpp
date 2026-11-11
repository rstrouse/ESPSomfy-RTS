#include <unity.h>
#include "JsonFormatter.h"

// The formatter only gets told about the first BUFF_SIZE bytes.  The rest is a
// canary that must never be written.
#define STORAGE_SIZE 160
#define BUFF_SIZE 128
#define CANARY 'X'

static char storage[STORAGE_SIZE];
static BufferedJsonFormatter fmt;

static void useBuffer(size_t size) {
  memset(storage, CANARY, sizeof(storage));
  fmt.setBuffer(storage, size);
}
static void assertCanary(size_t from) {
  for(size_t i = from; i < STORAGE_SIZE; i++) {
    char msg[48];
    snprintf(msg, sizeof(msg), "wrote past the buffer at byte %u", (unsigned)i);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(CANARY, storage[i], msg);
  }
}
// The JSON written for a single top level string value.
static const char *escaped(const char *val) {
  useBuffer(BUFF_SIZE);
  fmt.addElem(val);
  return storage;
}

void setUp() { useBuffer(BUFF_SIZE); }
void tearDown() { assertCanary(BUFF_SIZE); }

/*********************************************************************
 * Structure
 ********************************************************************/
static void test_empty_object() {
  fmt.beginObject();
  fmt.endObject();
  TEST_ASSERT_EQUAL_STRING("{}", storage);
}
static void test_nested_objects_and_arrays() {
  fmt.beginObject();
  fmt.addElem("a", (uint8_t)1);
  fmt.beginObject("b");
  fmt.addElem("c", true);
  fmt.endObject();
  fmt.beginArray("d");
  fmt.addElem((uint8_t)1);
  fmt.addElem((uint8_t)2);
  fmt.beginObject();
  fmt.addElem("e", "x");
  fmt.endObject();
  fmt.endArray();
  fmt.endObject();
  TEST_ASSERT_EQUAL_STRING("{\"a\":1,\"b\":{\"c\":true},\"d\":[1,2,{\"e\":\"x\"}]}", storage);
}
static void test_array_of_objects() {
  fmt.beginArray();
  fmt.beginObject();
  fmt.endObject();
  fmt.beginObject();
  fmt.endObject();
  fmt.endArray();
  TEST_ASSERT_EQUAL_STRING("[{},{}]", storage);
}
static void test_named_object_after_element() {
  fmt.beginObject();
  fmt.addElem("a", (uint8_t)1);
  fmt.beginObject("o");
  fmt.endObject();
  fmt.endObject();
  TEST_ASSERT_EQUAL_STRING("{\"a\":1,\"o\":{}}", storage);
}

/*********************************************************************
 * Values
 ********************************************************************/
static void test_number_and_bool_values() {
  fmt.beginArray();
  fmt.addElem((int8_t)-5);
  fmt.addElem((uint8_t)255);
  fmt.addElem((int32_t)-100000);
  fmt.addElem((uint32_t)4000000000u);
  fmt.addElem(1.5f);
  fmt.addElem(true);
  fmt.addElem(false);
  fmt.endArray();
  TEST_ASSERT_EQUAL_STRING("[-5,255,-100000,4000000000,1.5000,true,false]", storage);
}
static void test_named_uint64() {
  fmt.beginObject();
  fmt.addElem("u", (uint64_t)18446744073709551615ULL);
  fmt.endObject();
  TEST_ASSERT_EQUAL_STRING("{\"u\":18446744073709551615}", storage);
}
static void test_null_string_is_skipped() {
  fmt.beginObject();
  fmt.addElem("a", (uint8_t)1);
  fmt.addElem("n", (const char *)nullptr);
  fmt.addElem("b", (uint8_t)2);
  fmt.endObject();
  TEST_ASSERT_EQUAL_STRING("{\"a\":1,\"b\":2}", storage);
}

/*********************************************************************
 * Escaping
 ********************************************************************/
static void test_escapes_special_characters() {
  TEST_ASSERT_EQUAL_STRING("\"plain\"", escaped("plain"));
  TEST_ASSERT_EQUAL_STRING("\"\\\"\"", escaped("\""));
  TEST_ASSERT_EQUAL_STRING("\"\\\\\"", escaped("\\"));
  TEST_ASSERT_EQUAL_STRING("\"\\/\"", escaped("/"));
  TEST_ASSERT_EQUAL_STRING("\"\\n\\t\\r\\b\\f\"", escaped("\n\t\r\b\f"));
  TEST_ASSERT_EQUAL_STRING("\"Living \\\"Room\\\"\"", escaped("Living \"Room\""));
}
static void test_escapes_other_control_characters() {
  // Raw control characters are not allowed in JSON strings.
  TEST_ASSERT_EQUAL_STRING("\"a\\u0001b\"", escaped("a\x01" "b"));
  TEST_ASSERT_EQUAL_STRING("\"\\u001f\"", escaped("\x1f"));
}
static void test_escaped_length() {
  TEST_ASSERT_EQUAL_UINT32(0, fmt.calcEscapedLength(""));
  TEST_ASSERT_EQUAL_UINT32(2, fmt.calcEscapedLength("ab"));
  TEST_ASSERT_EQUAL_UINT32(4, fmt.calcEscapedLength("a\"b"));
}
static void test_escaped_length_counts_first_character() {
  TEST_ASSERT_EQUAL_UINT32(4, fmt.calcEscapedLength("\"ab"));
  TEST_ASSERT_EQUAL_UINT32(2, fmt.calcEscapedLength("/"));
}

/*********************************************************************
 * Buffer limits
 ********************************************************************/
static void test_value_needing_one_byte_too_many_is_refused() {
  // "\/abcdefg" with its quotes is 11 characters, so with the terminator it
  // needs 12 bytes and must not be written into 11.
  useBuffer(11);
  fmt.addElem("/abcdefg");
  TEST_ASSERT_TRUE(strlen(storage) < 11);
  assertCanary(11);
}
static void test_overflow_is_flagged() {
  useBuffer(16);
  fmt.beginObject();
  fmt.addElem("name", "fits");
  TEST_ASSERT_FALSE(fmt.overflowed());
  fmt.addElem("long", "this value does not fit");
  TEST_ASSERT_TRUE(fmt.overflowed());
  // What was already written (here up to the comma) stays intact and terminated.
  TEST_ASSERT_EQUAL_STRING("{\"name\":\"fits\",", storage);
  assertCanary(16);
}
static void test_setBuffer_clears_overflow() {
  useBuffer(4);
  fmt.addElem("too long");
  TEST_ASSERT_TRUE(fmt.overflowed());
  useBuffer(BUFF_SIZE);
  TEST_ASSERT_FALSE(fmt.overflowed());
}

/*********************************************************************
 * Tracked length
 ********************************************************************/
static void test_length_matches_what_was_written() {
  fmt.beginObject();
  fmt.addElem("name", "Living \"Room\"\n");
  fmt.addElem("pos", 41.5f);
  fmt.beginArray("ids");
  fmt.addElem((uint8_t)1);
  fmt.addElem((int32_t)-2);
  fmt.endArray();
  fmt.addElem("on", true);
  fmt.endObject();
  TEST_ASSERT_EQUAL_UINT32(strlen(storage), fmt.length());
  TEST_ASSERT_EQUAL_STRING("{\"name\":\"Living \\\"Room\\\"\\n\",\"pos\":41.5000,\"ids\":[1,-2],\"on\":true}", storage);
}
static void test_refused_append_leaves_length_alone() {
  useBuffer(16);
  fmt.beginObject();
  fmt.addElem("name", "fits");
  const size_t before = fmt.length();
  fmt.addElem("x", "this value does not fit");
  TEST_ASSERT_TRUE(fmt.overflowed());
  // The comma and key still fit; the value was refused whole.
  TEST_ASSERT_EQUAL_UINT32(strlen(storage), fmt.length());
  TEST_ASSERT_TRUE(fmt.length() >= before);
  TEST_ASSERT_TRUE(fmt.length() < 16);
}
static void test_setBuffer_resets_length() {
  fmt.addElem("something");
  TEST_ASSERT_TRUE(fmt.length() > 0);
  useBuffer(BUFF_SIZE);
  TEST_ASSERT_EQUAL_UINT32(0, fmt.length());
  TEST_ASSERT_EQUAL_STRING("", storage);
}

/*********************************************************************
 * discard
 ********************************************************************/
static void test_discard_writes_nothing() {
  // What a socket event built off loop() goes into.  The buffer the formatter
  // had before must not see a byte of it.
  fmt.beginObject();
  fmt.addElem("kept", true);
  char before[STORAGE_SIZE];
  memcpy(before, storage, sizeof(before));
  fmt.discard();
  fmt.beginObject();
  fmt.addElem("ssid", "Home \"WiFi\"");
  fmt.addElem("strength", (int32_t)-70);
  fmt.beginArray("pulses");
  fmt.addElem((uint32_t)2416);
  fmt.endArray();
  fmt.addElem("big", (uint64_t)18446744073709551615ULL);
  fmt.endObject();
  TEST_ASSERT_EQUAL_MEMORY(before, storage, sizeof(before));
  TEST_ASSERT_EQUAL_UINT32(0, fmt.length());
  TEST_ASSERT_TRUE(fmt.overflowed());
}
static void test_setBuffer_after_discard_writes_again() {
  fmt.discard();
  fmt.addElem("dropped");
  useBuffer(BUFF_SIZE);
  fmt.beginObject();
  fmt.endObject();
  TEST_ASSERT_FALSE(fmt.overflowed());
  TEST_ASSERT_EQUAL_STRING("{}", storage);
}

/*********************************************************************
 * Mark and roll back
 ********************************************************************/
static void test_rollback_drops_what_was_written_since_the_mark() {
  fmt.beginObject();
  fmt.addElem("a", (uint8_t)1);
  const json_mark_t m = fmt.mark();
  fmt.addElem("b", (uint8_t)2);
  fmt.beginArray("c");
  fmt.rollback(m);
  fmt.addElem("d", (uint8_t)3);
  fmt.endObject();
  TEST_ASSERT_EQUAL_STRING("{\"a\":1,\"d\":3}", storage);
}
static void test_rollback_clears_an_overflow() {
  fmt.beginObject();
  const json_mark_t m = fmt.mark();
  char big[BUFF_SIZE + 1];
  memset(big, 'z', BUFF_SIZE);
  big[BUFF_SIZE] = '\0';
  fmt.addElem("big", big);
  TEST_ASSERT_TRUE(fmt.overflowed());
  fmt.rollback(m);
  TEST_ASSERT_FALSE(fmt.overflowed());
  fmt.addElem("ok", true);
  fmt.endObject();
  TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", storage);
}
static void test_rollback_at_the_start_leaves_an_empty_buffer() {
  const json_mark_t m = fmt.mark();
  fmt.beginObject();
  fmt.rollback(m);
  TEST_ASSERT_EQUAL_UINT32(0, fmt.length());
  TEST_ASSERT_EQUAL_STRING("", storage);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_empty_object);
  RUN_TEST(test_nested_objects_and_arrays);
  RUN_TEST(test_array_of_objects);
  RUN_TEST(test_named_object_after_element);
  RUN_TEST(test_number_and_bool_values);
  RUN_TEST(test_named_uint64);
  RUN_TEST(test_null_string_is_skipped);
  RUN_TEST(test_escapes_special_characters);
  RUN_TEST(test_escapes_other_control_characters);
  RUN_TEST(test_escaped_length);
  RUN_TEST(test_escaped_length_counts_first_character);
  RUN_TEST(test_value_needing_one_byte_too_many_is_refused);
  RUN_TEST(test_overflow_is_flagged);
  RUN_TEST(test_setBuffer_clears_overflow);
  RUN_TEST(test_length_matches_what_was_written);
  RUN_TEST(test_refused_append_leaves_length_alone);
  RUN_TEST(test_setBuffer_resets_length);
  RUN_TEST(test_discard_writes_nothing);
  RUN_TEST(test_setBuffer_after_discard_writes_again);
  RUN_TEST(test_rollback_drops_what_was_written_since_the_mark);
  RUN_TEST(test_rollback_clears_an_overflow);
  RUN_TEST(test_rollback_at_the_start_leaves_an_empty_buffer);
  return UNITY_END();
}
