#include <unity.h>
#include "ShadeMotion.h"

#define ASSERT_POS(expected, actual) TEST_ASSERT_FLOAT_WITHIN(0.01f, (expected), (actual))

static const int8_t DOWN = 1;
static const int8_t UP = -1;
static const bool IDLE = false;
static const bool MOVING = true;

static somfy_tx_event_t evt(tx_event_t type, uint32_t ticket, uint32_t at = 0) {
  somfy_tx_event_t e;
  e.type = type;
  e.ticket = ticket;
  e.at = at;
  return e;
}
// A shade whose idle motor was set moving by radio job ticket.
static ShadeTxState heldBy(uint32_t ticket) {
  ShadeTxState tx;
  tx.hold.mark(ticket, false, 0);
  return tx;
}

void setUp() {}
void tearDown() {}

/*********************************************************************
 * The job that set the shade moving
 ********************************************************************/
static void test_held_job_started_starts_the_movement() {
  ShadeTxState tx = heldBy(7);
  ShadeTxActions act = tx.onEvent(evt(tx_event_t::started, 7, 1234), MOVING);
  TEST_ASSERT_TRUE(act.startMovement);
  TEST_ASSERT_EQUAL_UINT32(1234, act.startAt);
  TEST_ASSERT_FALSE(act.cancelMovement);
  TEST_ASSERT_FALSE(tx.hold.held());
}
static void test_held_job_finished_after_start_does_nothing() {
  ShadeTxState tx = heldBy(7);
  tx.onEvent(evt(tx_event_t::started, 7, 1234), MOVING);
  ShadeTxActions act = tx.onEvent(evt(tx_event_t::finished, 7, 2000), MOVING);
  TEST_ASSERT_FALSE(act.any());
}
static void test_held_job_dropped_cancels_the_movement() {
  ShadeTxState tx = heldBy(7);
  ShadeTxActions act = tx.onEvent(evt(tx_event_t::dropped, 7), MOVING);
  TEST_ASSERT_TRUE(act.cancelMovement);
  TEST_ASSERT_FALSE(act.startMovement);
  TEST_ASSERT_FALSE(tx.hold.held());
}
static void test_held_job_aborted_before_start_cancels_the_movement() {
  ShadeTxState tx = heldBy(7);
  ShadeTxActions act = tx.onEvent(evt(tx_event_t::aborted, 7), MOVING);
  TEST_ASSERT_TRUE(act.cancelMovement);
}

/*********************************************************************
 * The favorite (long press My)
 ********************************************************************/
static void test_favorite_started_waits_for_the_end() {
  ShadeTxState tx;
  tx.setMySent(9, 40.0f, 10.0f);
  ShadeTxActions act = tx.onEvent(evt(tx_event_t::started, 9), IDLE);
  TEST_ASSERT_FALSE(act.any());
  TEST_ASSERT_EQUAL_UINT32(9, tx.myTicket);
}
static void test_favorite_finished_is_committed() {
  ShadeTxState tx;
  tx.setMySent(9, 40.0f, 10.0f);
  tx.onEvent(evt(tx_event_t::started, 9), IDLE);
  ShadeTxActions act = tx.onEvent(evt(tx_event_t::finished, 9), IDLE);
  TEST_ASSERT_TRUE(act.commitMy);
  TEST_ASSERT_FALSE(act.restoreMy);
  TEST_ASSERT_EQUAL_UINT32(0, tx.myTicket);
}
static void test_favorite_aborted_restores_the_previous_one() {
  // The motor only stores the favorite when it gets the whole long press.
  ShadeTxState tx;
  tx.setMySent(9, 40.0f, 10.0f);
  tx.onEvent(evt(tx_event_t::started, 9), IDLE);
  ShadeTxActions act = tx.onEvent(evt(tx_event_t::aborted, 9), IDLE);
  TEST_ASSERT_TRUE(act.restoreMy);
  TEST_ASSERT_FALSE(act.commitMy);
  ASSERT_POS(40.0f, act.myOldPos);
  ASSERT_POS(10.0f, act.myOldTiltPos);
  TEST_ASSERT_EQUAL_UINT32(0, tx.myTicket);
}
static void test_favorite_dropped_restores_the_previous_one() {
  ShadeTxState tx;
  tx.setMySent(9, -1.0f, -1.0f);
  ShadeTxActions act = tx.onEvent(evt(tx_event_t::dropped, 9), IDLE);
  TEST_ASSERT_TRUE(act.restoreMy);
  ASSERT_POS(-1.0f, act.myOldPos);
  ASSERT_POS(-1.0f, act.myOldTiltPos);
}

/*********************************************************************
 * The stop sent at the target
 ********************************************************************/
static void test_stop_dropped_runs_on_to_the_limit() {
  ShadeTxState tx;
  tx.stopSent(12, 5000, DOWN, false);
  ShadeTxActions act = tx.onEvent(evt(tx_event_t::dropped, 12, 5300), IDLE);
  TEST_ASSERT_TRUE(act.runToLimit);
  TEST_ASSERT_EQUAL_INT8(DOWN, act.limitDir);
  TEST_ASSERT_FALSE(act.limitTilt);
  // From when the motor should have stopped, not when the drop was reported.
  TEST_ASSERT_EQUAL_UINT32(5000, act.limitSince);
  TEST_ASSERT_EQUAL_UINT32(0, tx.stopTicket);
}
static void test_tilt_stop_dropped_runs_the_tilt_on() {
  ShadeTxState tx;
  tx.stopSent(12, 5000, UP, true);
  ShadeTxActions act = tx.onEvent(evt(tx_event_t::dropped, 12), IDLE);
  TEST_ASSERT_TRUE(act.runToLimit);
  TEST_ASSERT_EQUAL_INT8(UP, act.limitDir);
  TEST_ASSERT_TRUE(act.limitTilt);
}
static void test_stop_dropped_after_moving_again_does_nothing() {
  // Something set the shade moving since, so that command decides where the
  // motor goes.
  ShadeTxState tx;
  tx.stopSent(12, 5000, DOWN, false);
  ShadeTxActions act = tx.onEvent(evt(tx_event_t::dropped, 12), MOVING);
  TEST_ASSERT_FALSE(act.any());
  TEST_ASSERT_EQUAL_UINT32(0, tx.stopTicket);
}
static void test_stop_on_air_does_nothing() {
  ShadeTxState tx;
  tx.stopSent(12, 5000, DOWN, false);
  TEST_ASSERT_FALSE(tx.onEvent(evt(tx_event_t::started, 12), IDLE).any());
  TEST_ASSERT_EQUAL_UINT32(0, tx.stopTicket);
  // Nothing is waiting on it any more.
  TEST_ASSERT_FALSE(tx.onEvent(evt(tx_event_t::dropped, 12), IDLE).any());
}
static void test_stop_aborted_does_not_run_on() {
  // Part of the stop went on air, which is enough for the motor.
  ShadeTxState tx;
  tx.stopSent(12, 5000, DOWN, false);
  TEST_ASSERT_FALSE(tx.onEvent(evt(tx_event_t::aborted, 12), IDLE).any());
}

/*********************************************************************
 * Edge cases
 ********************************************************************/
static void test_one_event_for_the_held_job_and_the_favorite() {
  // The delayed My that stores the favorite also sets an idle shade moving,
  // so the same job is held and awaited as the favorite.
  ShadeTxState tx = heldBy(9);
  tx.setMySent(9, 40.0f, 10.0f);
  ShadeTxActions act = tx.onEvent(evt(tx_event_t::dropped, 9), MOVING);
  TEST_ASSERT_TRUE(act.cancelMovement);
  TEST_ASSERT_TRUE(act.restoreMy);
  TEST_ASSERT_FALSE(tx.hold.held());
  TEST_ASSERT_EQUAL_UINT32(0, tx.myTicket);
}
static void test_ticket_zero_is_ignored() {
  ShadeTxState tx;
  tx.setMySent(0, 40.0f, 10.0f);
  tx.stopSent(0, 5000, DOWN, false);
  TEST_ASSERT_FALSE(tx.onEvent(evt(tx_event_t::dropped, 0), IDLE).any());
  TEST_ASSERT_FALSE(tx.onEvent(evt(tx_event_t::finished, 0), IDLE).any());
}
static void test_other_jobs_are_ignored() {
  ShadeTxState tx = heldBy(7);
  tx.setMySent(9, 40.0f, 10.0f);
  tx.stopSent(12, 5000, DOWN, false);
  TEST_ASSERT_FALSE(tx.onEvent(evt(tx_event_t::dropped, 8), IDLE).any());
  TEST_ASSERT_FALSE(tx.onEvent(evt(tx_event_t::finished, 13), IDLE).any());
  TEST_ASSERT_EQUAL_UINT32(7, tx.hold.ticket);
  TEST_ASSERT_EQUAL_UINT32(9, tx.myTicket);
  TEST_ASSERT_EQUAL_UINT32(12, tx.stopTicket);
}

/*********************************************************************
 * limitTarget
 ********************************************************************/
static void test_limitTarget() {
  ASSERT_POS(100.0f, limitTarget(DOWN));
  ASSERT_POS(0.0f, limitTarget(UP));
}

/*********************************************************************
 * TxHold::check
 ********************************************************************/
static void test_check_nothing_held() {
  TxHold hold;
  TEST_ASSERT_TRUE(hold.check(true, 1000, 30000) == TxHoldCheck::none);
}
static void test_check_idle_shade_drops_the_hold() {
  // The command didn't set the shade moving so there is nothing to hold.
  TxHold hold;
  hold.mark(5, false, 1000);
  TEST_ASSERT_TRUE(hold.check(false, 1000, 30000) == TxHoldCheck::none);
  TEST_ASSERT_FALSE(hold.held());
}
static void test_check_holds_a_moving_shade() {
  TxHold hold;
  hold.mark(5, false, 1000);
  TEST_ASSERT_TRUE(hold.check(true, 30999, 30000) == TxHoldCheck::holding);
  TEST_ASSERT_TRUE(hold.held());
}
static void test_check_expires() {
  TxHold hold;
  hold.mark(5, false, 1000);
  TEST_ASSERT_TRUE(hold.check(true, 31000, 30000) == TxHoldCheck::expired);
  TEST_ASSERT_FALSE(hold.held());
  TEST_ASSERT_TRUE(hold.check(true, 31001, 30000) == TxHoldCheck::none);
}
static void test_check_across_millis_wrap() {
  TxHold hold;
  hold.mark(5, false, 0xFFFFF000u);
  TEST_ASSERT_TRUE(hold.check(true, 0x00006000u, 30000) == TxHoldCheck::holding);
  TEST_ASSERT_TRUE(hold.check(true, 0x00007000u, 30000) == TxHoldCheck::expired);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_held_job_started_starts_the_movement);
  RUN_TEST(test_held_job_finished_after_start_does_nothing);
  RUN_TEST(test_held_job_dropped_cancels_the_movement);
  RUN_TEST(test_held_job_aborted_before_start_cancels_the_movement);
  RUN_TEST(test_favorite_started_waits_for_the_end);
  RUN_TEST(test_favorite_finished_is_committed);
  RUN_TEST(test_favorite_aborted_restores_the_previous_one);
  RUN_TEST(test_favorite_dropped_restores_the_previous_one);
  RUN_TEST(test_stop_dropped_runs_on_to_the_limit);
  RUN_TEST(test_tilt_stop_dropped_runs_the_tilt_on);
  RUN_TEST(test_stop_dropped_after_moving_again_does_nothing);
  RUN_TEST(test_stop_on_air_does_nothing);
  RUN_TEST(test_stop_aborted_does_not_run_on);
  RUN_TEST(test_one_event_for_the_held_job_and_the_favorite);
  RUN_TEST(test_ticket_zero_is_ignored);
  RUN_TEST(test_other_jobs_are_ignored);
  RUN_TEST(test_limitTarget);
  RUN_TEST(test_check_nothing_held);
  RUN_TEST(test_check_idle_shade_drops_the_hold);
  RUN_TEST(test_check_holds_a_moving_shade);
  RUN_TEST(test_check_expires);
  RUN_TEST(test_check_across_millis_wrap);
  return UNITY_END();
}
