#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include "ProtoAssembly.h"

// Putting protocol 2 messages back together from their pieces: one message at
// a time, one reply-worthy result per message.

void setUp() {}
void tearDown() {}

static proto_piece_t piece(uint32_t client, const char *text, bool first, bool last, bool isText = true) {
  proto_piece_t p;
  p.clientId = client;
  p.first = first;
  p.last = last;
  p.text = isText;
  p.data = reinterpret_cast<const uint8_t *>(text);
  p.len = strlen(text);
  return p;
}
// Takes the complete message and checks it.
static void assertTaken(MessageAssembler &a, const char *expected) {
  size_t len = 0;
  char *message = a.take(len);
  TEST_ASSERT_EQUAL_STRING(expected, message);
  TEST_ASSERT_EQUAL_UINT32(strlen(expected), len);
  free(message);
}

static void test_pieces_make_the_message() {
  MessageAssembler a(64);
  TEST_ASSERT_EQUAL(assembly_t::more, a.add(piece(1, "{\"v\":2,", true, false), 0));
  TEST_ASSERT_EQUAL(assembly_t::more, a.add(piece(1, "\"t\":3", false, false), 0));
  TEST_ASSERT_EQUAL(assembly_t::complete, a.add(piece(1, "}", false, true), 0));
  assertTaken(a, "{\"v\":2,\"t\":3}");
}
static void test_nothing_is_held_once_the_message_is_taken() {
  MessageAssembler a(64);
  TEST_ASSERT_FALSE(a.holding());
  a.add(piece(1, "ab", true, false), 0);
  TEST_ASSERT_TRUE(a.holding());
  a.add(piece(1, "c", false, true), 0);
  assertTaken(a, "abc");
  TEST_ASSERT_FALSE(a.holding());
  // A client leaving now can't free what was taken.
  a.drop(1);
}
static void test_another_clients_message_waits() {
  MessageAssembler a(64);
  a.add(piece(1, "ab", true, false), 0);
  // One busy for the other client's message, then its pieces are let go.
  TEST_ASSERT_EQUAL(assembly_t::busy, a.add(piece(2, "xy", true, false), 10));
  TEST_ASSERT_EQUAL(assembly_t::ignored, a.add(piece(2, "z", false, true), 10));
  TEST_ASSERT_EQUAL(assembly_t::complete, a.add(piece(1, "c", false, true), 20));
  assertTaken(a, "abc");
}
static void test_too_large_is_said_once() {
  MessageAssembler a(4);
  TEST_ASSERT_EQUAL(assembly_t::more, a.add(piece(1, "abc", true, false), 0));
  TEST_ASSERT_EQUAL(assembly_t::tooLarge, a.add(piece(1, "de", false, false), 0));
  TEST_ASSERT_EQUAL(assembly_t::ignored, a.add(piece(1, "f", false, true), 0));
  TEST_ASSERT_FALSE(a.holding());
}
static void test_a_binary_message_is_refused_once() {
  MessageAssembler a(64);
  TEST_ASSERT_EQUAL(assembly_t::notText, a.add(piece(1, "ab", true, false, false), 0));
  TEST_ASSERT_EQUAL(assembly_t::ignored, a.add(piece(1, "c", false, true, false), 0));
}
static void test_a_binary_message_leaves_another_clients_alone() {
  MessageAssembler a(64);
  a.add(piece(1, "ab", true, false), 0);
  TEST_ASSERT_EQUAL(assembly_t::notText, a.add(piece(2, "x", true, true, false), 0));
  TEST_ASSERT_EQUAL(assembly_t::complete, a.add(piece(1, "c", false, true), 0));
  assertTaken(a, "abc");
}
static void test_a_stalled_message_expires_and_its_client_is_owed_a_reply() {
  MessageAssembler a(64);
  a.add(piece(1, "ab", true, false), 0);
  TEST_ASSERT_EQUAL_UINT32(0, a.expire(PROTO_ASSEMBLY_TIMEOUT_MS - 1));
  TEST_ASSERT_TRUE(a.holding());
  TEST_ASSERT_EQUAL_UINT32(1, a.expire(PROTO_ASSEMBLY_TIMEOUT_MS));
  TEST_ASSERT_FALSE(a.holding());
  TEST_ASSERT_EQUAL_UINT32(0, a.expire(2 * PROTO_ASSEMBLY_TIMEOUT_MS));
  // Its late pieces were answered by the expiry.
  TEST_ASSERT_EQUAL(assembly_t::ignored, a.add(piece(1, "c", false, true), PROTO_ASSEMBLY_TIMEOUT_MS + 1));
  // And the buffer is free for anyone.
  TEST_ASSERT_EQUAL(assembly_t::complete, a.add(piece(2, "xy", true, true), PROTO_ASSEMBLY_TIMEOUT_MS + 1));
  assertTaken(a, "xy");
}
static void test_a_client_that_leaves_lets_go() {
  MessageAssembler a(64);
  a.add(piece(1, "ab", true, false), 0);
  a.drop(1);
  TEST_ASSERT_FALSE(a.holding());
  TEST_ASSERT_EQUAL_UINT32(0, a.expire(PROTO_ASSEMBLY_TIMEOUT_MS));
  TEST_ASSERT_EQUAL(assembly_t::complete, a.add(piece(2, "xy", true, true), 1));
  assertTaken(a, "xy");
}
static void test_a_message_of_one_piece_is_whole() {
  MessageAssembler a(64);
  TEST_ASSERT_EQUAL(assembly_t::complete, a.add(piece(1, "{}", true, true), 0));
  assertTaken(a, "{}");
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_pieces_make_the_message);
  RUN_TEST(test_nothing_is_held_once_the_message_is_taken);
  RUN_TEST(test_another_clients_message_waits);
  RUN_TEST(test_too_large_is_said_once);
  RUN_TEST(test_a_binary_message_is_refused_once);
  RUN_TEST(test_a_binary_message_leaves_another_clients_alone);
  RUN_TEST(test_a_stalled_message_expires_and_its_client_is_owed_a_reply);
  RUN_TEST(test_a_client_that_leaves_lets_go);
  RUN_TEST(test_a_message_of_one_piece_is_whole);
  return UNITY_END();
}
