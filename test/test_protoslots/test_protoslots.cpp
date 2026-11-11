#include <unity.h>
#include "ProtoSlots.h"

// Protocol 2's client slots and the pace of their syncs: what async_tcp and
// loop() each do with a slot, in the orders the two tasks can interleave.

static proto_slot_t slots[PROTO_MAX_CLIENTS];

void setUp() {
  for(proto_slot_t &s : slots) {
    freeSlot(s);
    s.loopId = 0;
    s.cur = sync_cursor_t();
    s.awaitingNext = false;
    s.sentAt = 0;
  }
}
void tearDown() {}

// A client as onConnect sets it up: a slot, then hello, then the sync.
static proto_slot_t &connect(uint32_t id, bool withSchema = true) {
  proto_slot_t *slot = claimSlot(slots, PROTO_MAX_CLIENTS, id, withSchema);
  TEST_ASSERT_NOT_NULL(slot);
  startSync(*slot);
  return *slot;
}
// What sendSyncFrame does with the cursor: on to the end, or one frame in.
static void sendAll(proto_slot_t &slot, uint32_t now) {
  slot.cur.section = sync_section_t::done;
  syncFrameSent(slot, now);
}
static void sendSome(proto_slot_t &slot, uint32_t now) {
  slot.cur.section = sync_section_t::shades;
  syncFrameSent(slot, now);
}

/*********************************************************************
 * Slots
 ********************************************************************/
static void test_a_sixth_client_gets_no_slot() {
  for(uint32_t id = 1; id <= PROTO_MAX_CLIENTS; id++) connect(id);
  TEST_ASSERT_NULL(claimSlot(slots, PROTO_MAX_CLIENTS, 99, true));
}
static void test_a_freed_slot_is_taken_again_clean() {
  proto_slot_t &a = connect(1);
  a.feeds.fetch_or(0x02);
  a.wants.fetch_or(WANTS_NEXT);
  freeSlot(a);
  TEST_ASSERT_NULL(slotOf(slots, PROTO_MAX_CLIENTS, 1));
  proto_slot_t *b = claimSlot(slots, PROTO_MAX_CLIENTS, 2, false);
  TEST_ASSERT_EQUAL_PTR(&a, b);
  TEST_ASSERT_EQUAL_UINT8(0, b->feeds.load());
  TEST_ASSERT_EQUAL_UINT32(0, b->wants.load());
  TEST_ASSERT_FALSE(b->withSchema.load());
}
static void test_no_sync_before_hello() {
  // claimSlot alone, as onConnect has it before hello has gone out.
  proto_slot_t *slot = claimSlot(slots, PROTO_MAX_CLIENTS, 1, true);
  TEST_ASSERT_EQUAL(sync_step_t::none, nextSyncStep(*slot, 0));
  startSync(*slot);
  TEST_ASSERT_EQUAL(sync_step_t::send, nextSyncStep(*slot, 0));
}

/*********************************************************************
 * The sync's pace
 ********************************************************************/
static void test_a_new_client_gets_the_schema_unless_it_has_it() {
  proto_slot_t &a = connect(1, true);
  proto_slot_t &b = connect(2, false);
  TEST_ASSERT_EQUAL(sync_step_t::send, nextSyncStep(a, 0));
  TEST_ASSERT_EQUAL(sync_section_t::types, a.cur.section);
  TEST_ASSERT_EQUAL(sync_step_t::send, nextSyncStep(b, 0));
  TEST_ASSERT_EQUAL(sync_section_t::header, b.cur.section);
  TEST_ASSERT_EQUAL_UINT32(2, b.loopId);
}
static void test_the_next_frame_waits_for_next() {
  proto_slot_t &a = connect(1);
  nextSyncStep(a, 0);
  sendSome(a, 100);
  TEST_ASSERT_EQUAL(sync_step_t::none, nextSyncStep(a, 200));
  a.wants.fetch_or(WANTS_NEXT);
  TEST_ASSERT_EQUAL(sync_step_t::send, nextSyncStep(a, 300));
}
static void test_next_after_the_sync_has_ended_does_nothing() {
  proto_slot_t &a = connect(1);
  nextSyncStep(a, 0);
  sendAll(a, 100);
  a.wants.fetch_or(WANTS_NEXT);
  TEST_ASSERT_EQUAL(sync_step_t::none, nextSyncStep(a, 200));
}
static void test_a_sync_again_starts_over_without_the_schema() {
  proto_slot_t &a = connect(1);
  nextSyncStep(a, 0);
  sendSome(a, 100);
  a.wants.fetch_or(WANTS_SYNC_AGAIN);
  TEST_ASSERT_EQUAL(sync_step_t::send, nextSyncStep(a, 200));
  TEST_ASSERT_EQUAL(sync_section_t::header, a.cur.section);
}
static void test_no_next_for_30s_closes_the_client_once() {
  proto_slot_t &a = connect(1);
  nextSyncStep(a, 0);
  sendSome(a, 1000);
  TEST_ASSERT_EQUAL(sync_step_t::none, nextSyncStep(a, 1000 + PROTO_SYNC_TIMEOUT_MS));
  TEST_ASSERT_EQUAL(sync_step_t::timedOut, nextSyncStep(a, 1001 + PROTO_SYNC_TIMEOUT_MS));
  TEST_ASSERT_EQUAL(sync_step_t::none, nextSyncStep(a, 2000 + PROTO_SYNC_TIMEOUT_MS));
}
static void test_a_finished_sync_never_times_out() {
  proto_slot_t &a = connect(1);
  nextSyncStep(a, 0);
  sendAll(a, 0);
  TEST_ASSERT_EQUAL(sync_step_t::none, nextSyncStep(a, 10 * PROTO_SYNC_TIMEOUT_MS));
}

/*********************************************************************
 * A slot changing hands under loop()
 ********************************************************************/
static void test_a_client_that_left_mid_sync_is_forgotten() {
  proto_slot_t &a = connect(1);
  nextSyncStep(a, 0);
  sendSome(a, 0);
  freeSlot(a);
  TEST_ASSERT_EQUAL(sync_step_t::none, nextSyncStep(a, 1));
  TEST_ASSERT_FALSE(a.awaitingNext);
  TEST_ASSERT_EQUAL_UINT32(0, a.loopId);
}
static void test_the_next_client_in_the_slot_gets_its_own_sync() {
  proto_slot_t &a = connect(1);
  nextSyncStep(a, 0);
  sendSome(a, 0);
  freeSlot(a);
  connect(2);
  TEST_ASSERT_EQUAL(sync_step_t::send, nextSyncStep(a, 1));
  TEST_ASSERT_EQUAL_UINT32(2, a.loopId);
  TEST_ASSERT_EQUAL(sync_section_t::types, a.cur.section);
}
static void test_wants_taken_for_a_client_that_left_stay_for_the_next() {
  // loop() read client 1, then 1 left and 2 took the slot before loop() took
  // what it wanted: 2's sync must not be used up on 1.
  proto_slot_t &a = connect(1);
  nextSyncStep(a, 0);
  freeSlot(a);
  connect(2);
  TEST_ASSERT_EQUAL_UINT32(0, takeWants(a, 1));
  TEST_ASSERT_EQUAL_UINT32(WANTS_SYNC, a.wants.load());
  TEST_ASSERT_EQUAL(sync_step_t::send, nextSyncStep(a, 1));
  TEST_ASSERT_EQUAL_UINT32(2, a.loopId);
}
static void test_wants_of_the_client_loop_read_are_taken() {
  proto_slot_t &a = connect(1);
  TEST_ASSERT_EQUAL_UINT32(WANTS_SYNC, takeWants(a, 1));
  TEST_ASSERT_EQUAL_UINT32(0, a.wants.load());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_sixth_client_gets_no_slot);
  RUN_TEST(test_a_freed_slot_is_taken_again_clean);
  RUN_TEST(test_no_sync_before_hello);
  RUN_TEST(test_a_new_client_gets_the_schema_unless_it_has_it);
  RUN_TEST(test_the_next_frame_waits_for_next);
  RUN_TEST(test_next_after_the_sync_has_ended_does_nothing);
  RUN_TEST(test_a_sync_again_starts_over_without_the_schema);
  RUN_TEST(test_no_next_for_30s_closes_the_client_once);
  RUN_TEST(test_a_finished_sync_never_times_out);
  RUN_TEST(test_a_client_that_left_mid_sync_is_forgotten);
  RUN_TEST(test_the_next_client_in_the_slot_gets_its_own_sync);
  RUN_TEST(test_wants_taken_for_a_client_that_left_stay_for_the_next);
  RUN_TEST(test_wants_of_the_client_loop_read_are_taken);
  return UNITY_END();
}
