#include <unity.h>
#include <vector>
#include "PulseDecoder.h"

void setUp() {}
void tearDown() {}

static const uint8_t FRAME[10] = {0xA7, 0x5C, 0x13, 0xF0, 0x0F, 0x81, 0x42, 0x99, 0x3C, 0xE1};

// The times between edges a receiver sees for a transmitted waveform.  Levels
// are added the way Transceiver::sendFrame() drives the TX pin; a run of the
// same level is one pulse.
struct Wave {
  std::vector<uint32_t> durations;
  int level = 0;
  uint32_t run = 100000;  // Quiet before the transmission.
  void add(int lvl, uint32_t us) {
    if(lvl == this->level) { this->run += us; return; }
    this->durations.push_back(this->run);
    this->level = lvl;
    this->run = us;
  }
};
// Mirrors Transceiver::sendFrame(): a wakeup pulse on the first frame (sync 2
// or 12), sync hardware sync pulses, the software sync and the Manchester bits.
static Wave transmit(const uint8_t *frame, uint8_t sync, uint8_t bitLength) {
  Wave w;
  if(sync == 2 || sync == 12) {
    w.add(1, 10920);
    w.add(0, 7357);
  }
  for(uint8_t i = 0; i < sync; i++) {
    w.add(1, 4 * SYMBOL);
    w.add(0, 4 * SYMBOL);
  }
  w.add(1, 4850);
  w.add(0, SYMBOL);
  for(uint8_t i = 0; i < bitLength; i++) {
    const bool one = (frame[i / 8] >> (7 - (i % 8))) & 1;
    w.add(one ? 0 : 1, SYMBOL);
    w.add(one ? 1 : 0, SYMBOL);
  }
  w.add(0, 27434);  // Inter-frame silence.
  w.add(1, 0);
  return w;
}
// Feeds durations to a fresh decoder until it reports a frame.
static bool receive(const std::vector<uint32_t> &durations, somfy_rx_t &rx) {
  rx.clear();
  for(uint32_t d : durations)
    if(decodePulse(rx, d) == PulseResult::frame) return true;
  return false;
}
static void assertFrame(uint8_t sync, uint8_t bitLength) {
  somfy_rx_t rx;
  TEST_ASSERT_TRUE(receive(transmit(FRAME, sync, bitLength).durations, rx));
  TEST_ASSERT_EQUAL_UINT8(bitLength, rx.bit_length);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(FRAME, rx.payload, bitLength / 8);
  TEST_ASSERT_EQUAL_UINT8(sync * 2, rx.cpt_synchro_hw);
}

/*********************************************************************
 * whole frames
 ********************************************************************/
static void test_56_bit_first_frame() { assertFrame(2, 56); }
static void test_56_bit_repeat() { assertFrame(7, 56); }
static void test_80_bit_first_frame() { assertFrame(12, 80); }
static void test_80_bit_repeat() { assertFrame(6, 80); }
static void test_records_the_pulses() {
  somfy_rx_t rx;
  const Wave w = transmit(FRAME, 7, 56);
  TEST_ASSERT_TRUE(receive(w.durations, rx));
  // Every pulse from the first hardware sync on is kept for the frame log.
  TEST_ASSERT_TRUE(rx.pulseCount > 14 + 56);
  TEST_ASSERT_EQUAL_UINT32(4 * SYMBOL, rx.pulses[1]);
}

/*********************************************************************
 * noise
 ********************************************************************/
static void test_glitch_is_reported_and_skipped() {
  somfy_rx_t rx;
  rx.clear();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PulseResult::glitch), static_cast<uint8_t>(decodePulse(rx, 100)));
  // The caller times the next edge from the edge before the glitch, so the
  // frame still decodes.
  const Wave w = transmit(FRAME, 7, 56);
  for(uint32_t d : w.durations) {
    if(decodePulse(rx, d) == PulseResult::frame) {
      TEST_ASSERT_EQUAL_HEX8_ARRAY(FRAME, rx.payload, 7);
      return;
    }
  }
  TEST_FAIL_MESSAGE("no frame");
}
static void test_bad_timing_mid_frame_starts_over() {
  somfy_rx_t rx;
  std::vector<uint32_t> d = transmit(FRAME, 7, 56).durations;
  // Corrupt a data pulse: the decoder gives up on this frame.
  d[20] = 3 * SYMBOL;
  TEST_ASSERT_FALSE(receive(d, rx));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(waiting_synchro), static_cast<uint8_t>(rx.status));
  // The next frame is received.
  std::vector<uint32_t> next = transmit(FRAME, 7, 56).durations;
  bool got = false;
  for(uint32_t p : next) if(decodePulse(rx, p) == PulseResult::frame) { got = true; break; }
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(FRAME, rx.payload, 7);
}
static void test_too_few_hardware_syncs_is_not_a_frame() {
  somfy_rx_t rx;
  // One sync pulse is only 2 edges; at least 4 are needed.
  TEST_ASSERT_FALSE(receive(transmit(FRAME, 1, 56).durations, rx));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_56_bit_first_frame);
  RUN_TEST(test_56_bit_repeat);
  RUN_TEST(test_80_bit_first_frame);
  RUN_TEST(test_80_bit_repeat);
  RUN_TEST(test_records_the_pulses);
  RUN_TEST(test_glitch_is_reported_and_skipped);
  RUN_TEST(test_bad_timing_mid_frame_starts_over);
  RUN_TEST(test_too_few_hardware_syncs_is_not_a_frame);
  return UNITY_END();
}
