#include <unity.h>
#include "TxBuffer.h"

void setUp() {}
void tearDown() {}

// Pushes a frame whose payload is all `tag` so frames can be told apart.
static void pushTagged(somfy_tx_queue_t &q, uint8_t tag, uint8_t bitLength = 56) {
  uint8_t payload[10];
  memset(payload, tag, sizeof(payload));
  q.push(bitLength == 56 ? 2 : 12, payload, bitLength);
}
static void assertTagged(const somfy_tx_t &tx, uint8_t tag) {
  uint8_t expected[10];
  memset(expected, tag, sizeof(expected));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, tx.payload, sizeof(expected));
}

static void test_push_then_pop_returns_the_frame() {
  somfy_tx_queue_t q;
  pushTagged(q, 0x11, 80);
  TEST_ASSERT_EQUAL_UINT8(1, q.length);
  somfy_tx_t tx;
  TEST_ASSERT_TRUE(q.pop(&tx));
  TEST_ASSERT_EQUAL_UINT8(12, tx.hwsync);
  TEST_ASSERT_EQUAL_UINT8(80, tx.bit_length);
  assertTagged(tx, 0x11);
  TEST_ASSERT_EQUAL_UINT8(0, q.length);
}
static void test_frames_come_out_oldest_first() {
  somfy_tx_queue_t q;
  pushTagged(q, 1);
  pushTagged(q, 2);
  pushTagged(q, 3);
  somfy_tx_t tx;
  for(uint8_t tag = 1; tag <= 3; tag++) {
    TEST_ASSERT_TRUE(q.pop(&tx));
    assertTagged(tx, tag);
  }
  TEST_ASSERT_FALSE(q.pop(&tx));
}
static void test_peek_leaves_the_frame_queued() {
  somfy_tx_queue_t q;
  pushTagged(q, 1);
  pushTagged(q, 2);
  somfy_tx_t tx;
  TEST_ASSERT_TRUE(q.peek(&tx));
  assertTagged(tx, 1);
  TEST_ASSERT_TRUE(q.peek(&tx));
  assertTagged(tx, 1);
  TEST_ASSERT_EQUAL_UINT8(2, q.length);
  TEST_ASSERT_TRUE(q.pop(&tx));
  assertTagged(tx, 1);
  TEST_ASSERT_TRUE(q.peek(&tx));
  assertTagged(tx, 2);
}
static void test_empty_buffer_has_nothing() {
  somfy_tx_queue_t q;
  somfy_tx_t tx;
  TEST_ASSERT_FALSE(q.peek(&tx));
  TEST_ASSERT_FALSE(q.pop(&tx));
  TEST_ASSERT_EQUAL_UINT8(0, q.length);
}
static void test_overflow_drops_the_oldest() {
  somfy_tx_queue_t q;
  for(uint8_t tag = 1; tag <= MAX_TX_BUFFER + 2; tag++) pushTagged(q, tag);
  TEST_ASSERT_EQUAL_UINT8(MAX_TX_BUFFER, q.length);
  somfy_tx_t tx;
  // The first two fell off.
  for(uint8_t tag = 3; tag <= MAX_TX_BUFFER + 2; tag++) {
    TEST_ASSERT_TRUE(q.pop(&tx));
    assertTagged(tx, tag);
  }
  TEST_ASSERT_FALSE(q.pop(&tx));
}
static void test_slots_are_reused_after_pop() {
  somfy_tx_queue_t q;
  somfy_tx_t tx;
  // Keep two frames in flight so freed slots are filled again many times over.
  for(uint8_t tag = 1; tag <= 4 * MAX_TX_BUFFER; tag++) {
    pushTagged(q, tag);
    if(tag >= 2) {
      TEST_ASSERT_TRUE(q.pop(&tx));
      assertTagged(tx, tag - 1);
      TEST_ASSERT_EQUAL_UINT8(1, q.length);
    }
  }
}
static void test_clear_empties_it() {
  somfy_tx_queue_t q;
  pushTagged(q, 1);
  pushTagged(q, 2);
  q.clear();
  TEST_ASSERT_EQUAL_UINT8(0, q.length);
  somfy_tx_t tx;
  TEST_ASSERT_FALSE(q.peek(&tx));
  pushTagged(q, 3);
  TEST_ASSERT_TRUE(q.pop(&tx));
  assertTagged(tx, 3);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_push_then_pop_returns_the_frame);
  RUN_TEST(test_frames_come_out_oldest_first);
  RUN_TEST(test_peek_leaves_the_frame_queued);
  RUN_TEST(test_empty_buffer_has_nothing);
  RUN_TEST(test_overflow_drops_the_oldest);
  RUN_TEST(test_slots_are_reused_after_pop);
  RUN_TEST(test_clear_empties_it);
  return UNITY_END();
}
