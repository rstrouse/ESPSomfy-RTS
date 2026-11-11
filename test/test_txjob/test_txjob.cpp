#include <unity.h>
#include "TxBuffer.h"

static const uint32_t ADDR = 0x123456;

// A frame as SomfyRemote::sendCommandAfter builds it.
static somfy_frame_t makeFrame(somfy_commands cmd, uint8_t bitLength = 56) {
  somfy_frame_t f;
  f.cmd = cmd;
  f.remoteAddress = ADDR;
  f.rollingCode = 0x0102;
  f.encKey = 0xA2;
  f.bitLength = bitLength;
  f.valid = true;
  return f;
}

void setUp() {}
void tearDown() {}

/*********************************************************************
 * encodeTxJob
 ********************************************************************/
static void test_encode_56bit_job() {
  somfy_frame_t f = makeFrame(somfy_commands::Up);
  somfy_tx_job_t job;
  encodeTxJob(job, f, 3);
  byte expected[10] = {};
  somfy_frame_t copy = makeFrame(somfy_commands::Up);
  copy.encodeFrame(expected);
  TEST_ASSERT_EQUAL_UINT8(2, job.tx.hwsync);
  TEST_ASSERT_EQUAL_UINT8(56, job.tx.bit_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, job.tx.payload, sizeof(expected));
  TEST_ASSERT_EQUAL_UINT8(3, job.repeats);
}
static void test_encode_80bit_job() {
  somfy_frame_t f = makeFrame(somfy_commands::Down, 80);
  somfy_tx_job_t job;
  encodeTxJob(job, f, 1);
  byte expected[10] = {};
  somfy_frame_t copy = makeFrame(somfy_commands::Down, 80);
  copy.encodeFrame(expected);
  TEST_ASSERT_EQUAL_UINT8(12, job.tx.hwsync);
  TEST_ASSERT_EQUAL_UINT8(80, job.tx.bit_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, job.tx.payload, sizeof(expected));
}
static void test_encode_job_copies_the_frame() {
  somfy_frame_t f = makeFrame(somfy_commands::My);
  somfy_tx_job_t job;
  encodeTxJob(job, f, 1);
  TEST_ASSERT_EQUAL_UINT32(ADDR, job.frame.remoteAddress);
  TEST_ASSERT_EQUAL_UINT16(0x0102, job.frame.rollingCode);
  // The radio task uses its own copy, so later changes to the remote's frame
  // don't reach it.
  f.rollingCode = 0x0200;
  TEST_ASSERT_EQUAL_UINT16(0x0102, job.frame.rollingCode);
}
static void test_encode_job_limits_repeats() {
  somfy_frame_t f = makeFrame(somfy_commands::My);
  somfy_tx_job_t job;
  encodeTxJob(job, f, TX_MAX_REPEATS);
  TEST_ASSERT_EQUAL_UINT8(TX_MAX_REPEATS, job.repeats);
  encodeTxJob(job, f, TX_MAX_REPEATS + 1);
  TEST_ASSERT_EQUAL_UINT8(TX_MAX_REPEATS, job.repeats);
  encodeTxJob(job, f, 255);
  TEST_ASSERT_EQUAL_UINT8(TX_MAX_REPEATS, job.repeats);
  encodeTxJob(job, f, 0);
  TEST_ASSERT_EQUAL_UINT8(0, job.repeats);
}

/*********************************************************************
 * Repeat jobs
 ********************************************************************/
static void test_repeat_job_counts_on_from_the_frame() {
  somfy_frame_t f = makeFrame(somfy_commands::Up, 80);
  f.repeats = 4;
  somfy_tx_job_t job;
  encodeRepeatJob(job, f, 2);
  TEST_ASSERT_EQUAL_UINT8(6, job.firstRepeat);
  TEST_ASSERT_EQUAL_UINT8(2, job.repeats);
  // Nothing moves until the radio takes the job.
  TEST_ASSERT_EQUAL_UINT8(4, f.repeats);
}
static void test_repeat_queued_advances_the_counter() {
  somfy_frame_t f = makeFrame(somfy_commands::Up, 80);
  f.repeats = 4;
  somfy_tx_job_t job;
  encodeRepeatJob(job, f, 2);
  noteRepeatQueued(f, job);
  // The first frame plus two repeats went out.
  TEST_ASSERT_EQUAL_UINT8(7, f.repeats);
  // The next repeat job picks up where this one stops.
  somfy_tx_job_t next;
  encodeRepeatJob(next, f, 1);
  TEST_ASSERT_EQUAL_UINT8(9, next.firstRepeat);
}
static void test_repeat_queued_uses_the_limited_repeats() {
  somfy_frame_t f = makeFrame(somfy_commands::Up);
  somfy_tx_job_t job;
  encodeRepeatJob(job, f, 200);
  noteRepeatQueued(f, job);
  TEST_ASSERT_EQUAL_UINT8(TX_MAX_REPEATS + 1, f.repeats);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_encode_56bit_job);
  RUN_TEST(test_encode_80bit_job);
  RUN_TEST(test_encode_job_copies_the_frame);
  RUN_TEST(test_encode_job_limits_repeats);
  RUN_TEST(test_repeat_job_counts_on_from_the_frame);
  RUN_TEST(test_repeat_queued_advances_the_counter);
  RUN_TEST(test_repeat_queued_uses_the_limited_repeats);
  return UNITY_END();
}
