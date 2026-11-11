#include <unity.h>
#include "RollingCode.h"

// Stands in for NVS and counts how often it is read.
struct FakeStore {
  uint16_t code = 0;
  int reads = 0;
  uint16_t operator()() { this->reads++; return this->code; }
};
// Passes the store by reference so the test sees the read count.
static uint16_t next(bool synced, uint16_t last, FakeStore &store) {
  return nextRollingCode(synced, last, [&store]() { return store(); });
}

void setUp() {}
void tearDown() {}

static void test_unsynced_reads_the_store() {
  FakeStore store;
  store.code = 41;
  TEST_ASSERT_EQUAL_UINT16(42, next(false, 7, store));
  TEST_ASSERT_EQUAL_INT(1, store.reads);
}
static void test_synced_never_reads_the_store() {
  FakeStore store;
  store.code = 41;
  TEST_ASSERT_EQUAL_UINT16(8, next(true, 7, store));
  TEST_ASSERT_EQUAL_INT(0, store.reads);
}
static void test_reading_does_not_use_up_a_code() {
  // A command the radio refuses must leave the remote on the same code.
  FakeStore store;
  store.code = 41;
  TEST_ASSERT_EQUAL_UINT16(42, next(false, 0, store));
  TEST_ASSERT_EQUAL_UINT16(42, next(false, 0, store));
  TEST_ASSERT_EQUAL_UINT16(8, next(true, 7, store));
  TEST_ASSERT_EQUAL_UINT16(8, next(true, 7, store));
}
static void test_wraps_to_zero_after_0xFFFF() {
  // Current behaviour, kept on purpose for now: the code after 0xFFFF is 0.
  // Our own decoder rejects a zero rolling code (see
  // test_rejects_zero_rolling_code_except_sensor in test_frame), so a remote
  // that gets this far sends one frame other receivers may ignore.
  FakeStore store;
  store.code = 0xFFFF;
  TEST_ASSERT_EQUAL_UINT16(0, next(false, 0, store));
  TEST_ASSERT_EQUAL_UINT16(0, next(true, 0xFFFF, store));
}

// NVS is newer than shades.cfg, so a load keeps the higher code.
static void test_a_load_keeps_the_higher_rolling_code() {
  TEST_ASSERT_EQUAL_UINT16(500, rollingCodeAfterLoad(500, 300, false));
  TEST_ASSERT_EQUAL_UINT16(700, rollingCodeAfterLoad(500, 700, false));
}
static void test_a_restore_takes_the_backups_rolling_code() {
  TEST_ASSERT_EQUAL_UINT16(300, rollingCodeAfterLoad(500, 300, true));
  TEST_ASSERT_EQUAL_UINT16(700, rollingCodeAfterLoad(500, 700, true));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_unsynced_reads_the_store);
  RUN_TEST(test_synced_never_reads_the_store);
  RUN_TEST(test_reading_does_not_use_up_a_code);
  RUN_TEST(test_wraps_to_zero_after_0xFFFF);
  RUN_TEST(test_a_load_keeps_the_higher_rolling_code);
  RUN_TEST(test_a_restore_takes_the_backups_rolling_code);
  return UNITY_END();
}
