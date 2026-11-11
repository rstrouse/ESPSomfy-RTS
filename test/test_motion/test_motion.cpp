#include <unity.h>
#include "ShadeMotion.h"
#include "SomfyFrame.h"

#define ASSERT_POS(expected, actual) TEST_ASSERT_FLOAT_WITHIN(0.01f, (expected), (actual))
#define ASSERT_CMD(expected, actual) TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual))

static const int8_t DOWN = 1;
static const int8_t UP = -1;

void setUp() {}
void tearDown() {}

/*********************************************************************
 * travelPosition
 ********************************************************************/
static void test_down_part_way() {
  ASSERT_POS(50.0f, travelPosition(0, DOWN, 5000, 10000));
  // From 30% the first 3s of travel are already done.
  ASSERT_POS(50.0f, travelPosition(30, DOWN, 2000, 10000));
}
static void test_up_part_way() {
  ASSERT_POS(75.0f, travelPosition(100, UP, 2500, 10000));
  ASSERT_POS(25.0f, travelPosition(50, UP, 5000, 20000));
}
static void test_no_time_elapsed_stays_put() {
  // The start is counted in whole milliseconds so allow for that.
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 50.5f, travelPosition(50.5f, DOWN, 0, 10000));
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 50.5f, travelPosition(50.5f, UP, 0, 10000));
}
static void test_stops_at_the_ends() {
  ASSERT_POS(100.0f, travelPosition(0, DOWN, 10000, 10000));
  ASSERT_POS(100.0f, travelPosition(0, DOWN, 20000, 10000));
  ASSERT_POS(0.0f, travelPosition(100, UP, 10000, 10000));
  ASSERT_POS(0.0f, travelPosition(100, UP, 20000, 10000));
}
static void test_starting_at_the_end() {
  ASSERT_POS(100.0f, travelPosition(100, DOWN, 0, 10000));
  ASSERT_POS(100.0f, travelPosition(100, DOWN, 3000, 10000));
  ASSERT_POS(0.0f, travelPosition(0, UP, 0, 10000));
  ASSERT_POS(0.0f, travelPosition(0, UP, 3000, 10000));
}
static void test_always_within_range() {
  const float starts[] = {0.0f, 12.5f, 50.0f, 99.9f, 100.0f};
  for(float start : starts) {
    for(uint32_t elapsed = 0; elapsed <= 30000; elapsed += 250) {
      for(int8_t dir : {DOWN, UP}) {
        float pos = travelPosition(start, dir, elapsed, 10000);
        TEST_ASSERT_TRUE(pos >= 0.0f && pos <= 100.0f);
      }
    }
  }
}
static void test_up_and_down_use_their_own_times() {
  // 2s of a 10s travel down covers 20%, 2s of a 20s travel up only 10%.
  ASSERT_POS(20.0f, travelPosition(0, DOWN, 2000, 10000));
  ASSERT_POS(90.0f, travelPosition(100, UP, 2000, 20000));
}
static void test_zero_travel_time_is_instant() {
  ASSERT_POS(100.0f, travelPosition(0, DOWN, 0, 0));
  ASSERT_POS(100.0f, travelPosition(42, DOWN, 0, 0));
  ASSERT_POS(0.0f, travelPosition(100, UP, 0, 0));
  ASSERT_POS(0.0f, travelPosition(42, UP, 0, 0));
}
static void test_moves_steadily() {
  // Each step of time moves the shade further, never back.
  float last = 0.0f;
  for(uint32_t elapsed = 0; elapsed <= 10000; elapsed += 100) {
    float pos = travelPosition(0, DOWN, elapsed, 10000);
    TEST_ASSERT_TRUE(pos >= last);
    last = pos;
  }
  last = 100.0f;
  for(uint32_t elapsed = 0; elapsed <= 10000; elapsed += 100) {
    float pos = travelPosition(100, UP, elapsed, 10000);
    TEST_ASSERT_TRUE(pos <= last);
    last = pos;
  }
}

/*********************************************************************
 * stepPosition
 ********************************************************************/
static void test_step_down_and_up() {
  // A step of 100ms x 2 is 200ms of a 10s travel, 2%.
  float out = -1;
  TEST_ASSERT_TRUE(stepPosition(40, DOWN, 10000, 100, 2, out));
  ASSERT_POS(42.0f, out);
  TEST_ASSERT_TRUE(stepPosition(40, UP, 10000, 100, 2, out));
  ASSERT_POS(38.0f, out);
}
static void test_step_stops_at_the_ends() {
  float out = -1;
  TEST_ASSERT_TRUE(stepPosition(99.5f, DOWN, 1000, 100, 1, out));
  ASSERT_POS(100.0f, out);
  TEST_ASSERT_TRUE(stepPosition(0.5f, UP, 1000, 100, 1, out));
  ASSERT_POS(0.0f, out);
}
static void test_step_is_ignored_without_a_travel_time_or_step_size() {
  // A shade with no time configured for the direction it steps in.
  float out = -1;
  TEST_ASSERT_FALSE(stepPosition(40, UP, 0, 100, 1, out));
  TEST_ASSERT_FALSE(stepPosition(40, DOWN, 0, 100, 1, out));
  TEST_ASSERT_FALSE(stepPosition(40, DOWN, 10000, 0, 1, out));
  ASSERT_POS(-1.0f, out);
}
static void test_step_of_no_frame_steps_stays_put() {
  float out = -1;
  TEST_ASSERT_TRUE(stepPosition(40, DOWN, 10000, 100, 0, out));
  ASSERT_POS(40.0f, out);
}

/*********************************************************************
 * displayPosition
 ********************************************************************/
static void test_displayPosition() {
  TEST_ASSERT_EQUAL_INT8(-1, displayPosition(-1, false));
  TEST_ASSERT_EQUAL_INT8(-1, displayPosition(-1, true));
  TEST_ASSERT_EQUAL_INT8(37, displayPosition(37.9f, false));
  TEST_ASSERT_EQUAL_INT8(62, displayPosition(37.9f, true));
  TEST_ASSERT_EQUAL_INT8(100, displayPosition(0, true));
  TEST_ASSERT_EQUAL_INT8(0, displayPosition(100, true));
  TEST_ASSERT_EQUAL_INT8(100, displayPosition(100, false));
}

/*********************************************************************
 * flipCommand
 ********************************************************************/
static void test_flipCommand_swaps_directions() {
  ASSERT_CMD(somfy_commands::Down, flipCommand(somfy_commands::Up));
  ASSERT_CMD(somfy_commands::Up, flipCommand(somfy_commands::Down));
  ASSERT_CMD(somfy_commands::MyDown, flipCommand(somfy_commands::MyUp));
  ASSERT_CMD(somfy_commands::MyUp, flipCommand(somfy_commands::MyDown));
  ASSERT_CMD(somfy_commands::StepDown, flipCommand(somfy_commands::StepUp));
  ASSERT_CMD(somfy_commands::StepUp, flipCommand(somfy_commands::StepDown));
}
static void test_flipCommand_keeps_the_rest() {
  const somfy_commands same[] = {
    somfy_commands::My, somfy_commands::Prog, somfy_commands::Toggle,
    somfy_commands::UpDown, somfy_commands::MyUpDown, somfy_commands::Favorite
  };
  for(somfy_commands cmd : same) ASSERT_CMD(cmd, flipCommand(cmd));
}

/*********************************************************************
 * TxHold
 ********************************************************************/
static void test_txhold_idle_shade_is_held() {
  TxHold hold;
  hold.mark(7, false, 1000);
  TEST_ASSERT_TRUE(hold.held());
  TEST_ASSERT_EQUAL_UINT32(7, hold.ticket);
  TEST_ASSERT_EQUAL_UINT32(1000, hold.since);
}
static void test_txhold_moving_shade_is_not_held() {
  TxHold hold;
  hold.mark(7, true, 1000);
  TEST_ASSERT_FALSE(hold.held());
}
static void test_txhold_ignores_ticket_zero() {
  TxHold hold;
  hold.mark(0, false, 1000);
  TEST_ASSERT_FALSE(hold.held());
  hold.mark(7, false, 1000);
  TEST_ASSERT_FALSE(hold.release(0));
  TEST_ASSERT_TRUE(hold.held());
}
static void test_txhold_first_job_wins() {
  // simMy Favorite: the Up/Down queued from inside processFrame (N+1) is
  // marked before the outer Favorite (N), and the hold stays on the Up/Down.
  TxHold hold;
  hold.mark(11, false, 1000);
  hold.mark(10, false, 1005);
  TEST_ASSERT_EQUAL_UINT32(11, hold.ticket);
  TEST_ASSERT_EQUAL_UINT32(1000, hold.since);
  TEST_ASSERT_FALSE(hold.release(10));
  TEST_ASSERT_TRUE(hold.held());
  TEST_ASSERT_TRUE(hold.release(11));
  TEST_ASSERT_FALSE(hold.held());
}
static void test_txhold_release_only_matches_the_held_job() {
  TxHold hold;
  hold.mark(5, false, 0);
  TEST_ASSERT_FALSE(hold.release(4));
  TEST_ASSERT_FALSE(hold.release(6));
  TEST_ASSERT_TRUE(hold.held());
  TEST_ASSERT_TRUE(hold.release(5));
  TEST_ASSERT_FALSE(hold.release(5));
}
static void test_txhold_new_hold_after_release() {
  TxHold hold;
  hold.mark(5, false, 0);
  hold.release(5);
  hold.mark(6, false, 200);
  TEST_ASSERT_EQUAL_UINT32(6, hold.ticket);
  TEST_ASSERT_EQUAL_UINT32(200, hold.since);
}
static void test_txhold_times_out() {
  TxHold hold;
  hold.mark(5, false, 1000);
  TEST_ASSERT_FALSE(hold.timedOut(1000, 30000));
  TEST_ASSERT_FALSE(hold.timedOut(30999, 30000));
  TEST_ASSERT_TRUE(hold.timedOut(31000, 30000));
}
static void test_txhold_times_out_across_millis_wrap() {
  TxHold hold;
  hold.mark(5, false, 0xFFFFF000u);
  TEST_ASSERT_FALSE(hold.timedOut(0x00001000u, 30000));  // 8192ms later
  TEST_ASSERT_FALSE(hold.timedOut(0x00006000u, 30000));  // 28672ms later
  TEST_ASSERT_TRUE(hold.timedOut(0x00007000u, 30000));   // 32768ms later
}

/*********************************************************************
 * stepTargets
 ********************************************************************/
// One step of 100ms: 0.5% of the 20s up travel, 1% of the 10s down travel and
// 2% of the 5s tilt travel.
static StepTargets stepOf(tilt_types tilt, int8_t dir, float pos, float tiltPos, uint8_t frameSteps = 1,
    uint32_t upTime = 20000, uint32_t downTime = 10000, uint32_t tiltTime = 5000, uint16_t stepSize = 100) {
  return stepTargets(tilt, dir, pos, tiltPos, upTime, downTime, tiltTime, stepSize, frameSteps);
}
#define ASSERT_ACTION(expected, actual) TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual))

static void test_stepTargets_lift_shade_steps_its_lift() {
  StepTargets t = stepOf(tilt_types::none, DOWN, 40, 0);
  ASSERT_ACTION(StepAction::move, t.action);
  TEST_ASSERT_TRUE(t.setLift);
  ASSERT_POS(41, t.lift);
  TEST_ASSERT_FALSE(t.setTilt);
  // Up uses the up travel time.
  t = stepOf(tilt_types::none, UP, 40, 0);
  ASSERT_POS(39.5f, t.lift);
}
static void test_stepTargets_frame_steps_multiply_the_step() {
  ASSERT_POS(43, stepOf(tilt_types::none, DOWN, 40, 0, 3).lift);
}
static void test_stepTargets_tilt_motors_step_the_lift() {
  // A separate tilt motor or euromode shade tilts with a long press, not a step.
  ASSERT_POS(41, stepOf(tilt_types::tiltmotor, DOWN, 40, 50).lift);
  TEST_ASSERT_FALSE(stepOf(tilt_types::euromode, DOWN, 40, 50).setTilt);
}
static void test_stepTargets_lift_shade_at_the_end_has_nothing_to_do() {
  ASSERT_ACTION(StepAction::none, stepOf(tilt_types::none, DOWN, 100, 0).action);
  ASSERT_ACTION(StepAction::none, stepOf(tilt_types::none, UP, 0, 0).action);
}
static void test_stepTargets_tilt_only_shade_steps_its_tilt() {
  StepTargets t = stepOf(tilt_types::tiltonly, DOWN, 100, 40);
  ASSERT_ACTION(StepAction::move, t.action);
  TEST_ASSERT_FALSE(t.setLift);
  TEST_ASSERT_TRUE(t.setTilt);
  ASSERT_POS(42, t.tilt);
  // At the end the tilt stays there.
  t = stepOf(tilt_types::tiltonly, UP, 100, 0);
  ASSERT_ACTION(StepAction::move, t.action);
  ASSERT_POS(0, t.tilt);
}
static void test_stepTargets_integrated_tilts_first() {
  StepTargets t = stepOf(tilt_types::integrated, DOWN, 20, 40);
  ASSERT_ACTION(StepAction::move, t.action);
  // The lift holds where it is while the slats turn.
  TEST_ASSERT_TRUE(t.setLift);
  ASSERT_POS(20, t.lift);
  TEST_ASSERT_TRUE(t.setTilt);
  ASSERT_POS(42, t.tilt);
}
static void test_stepTargets_integrated_lifts_once_tilted() {
  StepTargets t = stepOf(tilt_types::integrated, DOWN, 20, 100);
  ASSERT_ACTION(StepAction::move, t.action);
  ASSERT_POS(21, t.lift);
  ASSERT_POS(100, t.tilt);
  t = stepOf(tilt_types::integrated, UP, 50, 0);
  ASSERT_POS(49.5f, t.lift);
  ASSERT_POS(0, t.tilt);
}
static void test_stepTargets_integrated_at_both_ends_is_ignored() {
  ASSERT_ACTION(StepAction::ignore, stepOf(tilt_types::integrated, DOWN, 100, 100).action);
  ASSERT_ACTION(StepAction::ignore, stepOf(tilt_types::integrated, UP, 0, 0).action);
}
static void test_stepTargets_without_a_travel_time_or_step_size_is_ignored() {
  ASSERT_ACTION(StepAction::ignore, stepOf(tilt_types::none, DOWN, 40, 0, 1, 20000, 0).action);
  ASSERT_ACTION(StepAction::ignore, stepOf(tilt_types::none, UP, 40, 0, 1, 0).action);
  ASSERT_ACTION(StepAction::ignore, stepOf(tilt_types::tiltonly, DOWN, 100, 40, 1, 20000, 10000, 0).action);
  ASSERT_ACTION(StepAction::ignore, stepOf(tilt_types::integrated, DOWN, 20, 40, 1, 20000, 10000, 0).action);
  // Even a shade with nothing to do ignores a step it can't size.
  ASSERT_ACTION(StepAction::ignore, stepOf(tilt_types::none, DOWN, 100, 0, 1, 20000, 10000, 5000, 0).action);
}

/*********************************************************************
 * requestedFavoriteTilt
 ********************************************************************/
static void test_favorite_tilt_keeps_the_requested_tilt() {
  TEST_ASSERT_EQUAL_INT8(25, requestedFavoriteTilt(25, tilt_types::integrated, 70.0f, false));
}
static void test_favorite_tilt_defaults_to_the_favorite_tilt() {
  // Not the lift favorite: that was the old default.
  TEST_ASSERT_EQUAL_INT8(70, requestedFavoriteTilt(-1, tilt_types::tiltmotor, 70.0f, false));
  // The request is in display positions, so a flipped shade gets its favorite flipped.
  TEST_ASSERT_EQUAL_INT8(30, requestedFavoriteTilt(-1, tilt_types::tiltmotor, 70.0f, true));
}
static void test_favorite_tilt_without_a_favorite_is_unset() {
  TEST_ASSERT_EQUAL_INT8(-1, requestedFavoriteTilt(-1, tilt_types::integrated, -1.0f, false));
}
static void test_favorite_tilt_of_a_shade_without_tilt_is_unset() {
  TEST_ASSERT_EQUAL_INT8(-1, requestedFavoriteTilt(25, tilt_types::none, 70.0f, false));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_down_part_way);
  RUN_TEST(test_up_part_way);
  RUN_TEST(test_no_time_elapsed_stays_put);
  RUN_TEST(test_stops_at_the_ends);
  RUN_TEST(test_starting_at_the_end);
  RUN_TEST(test_always_within_range);
  RUN_TEST(test_up_and_down_use_their_own_times);
  RUN_TEST(test_zero_travel_time_is_instant);
  RUN_TEST(test_moves_steadily);
  RUN_TEST(test_step_down_and_up);
  RUN_TEST(test_step_stops_at_the_ends);
  RUN_TEST(test_step_is_ignored_without_a_travel_time_or_step_size);
  RUN_TEST(test_step_of_no_frame_steps_stays_put);
  RUN_TEST(test_stepTargets_lift_shade_steps_its_lift);
  RUN_TEST(test_stepTargets_frame_steps_multiply_the_step);
  RUN_TEST(test_stepTargets_tilt_motors_step_the_lift);
  RUN_TEST(test_stepTargets_lift_shade_at_the_end_has_nothing_to_do);
  RUN_TEST(test_stepTargets_tilt_only_shade_steps_its_tilt);
  RUN_TEST(test_stepTargets_integrated_tilts_first);
  RUN_TEST(test_stepTargets_integrated_lifts_once_tilted);
  RUN_TEST(test_stepTargets_integrated_at_both_ends_is_ignored);
  RUN_TEST(test_stepTargets_without_a_travel_time_or_step_size_is_ignored);
  RUN_TEST(test_displayPosition);
  RUN_TEST(test_favorite_tilt_keeps_the_requested_tilt);
  RUN_TEST(test_favorite_tilt_defaults_to_the_favorite_tilt);
  RUN_TEST(test_favorite_tilt_without_a_favorite_is_unset);
  RUN_TEST(test_favorite_tilt_of_a_shade_without_tilt_is_unset);
  RUN_TEST(test_flipCommand_swaps_directions);
  RUN_TEST(test_flipCommand_keeps_the_rest);
  RUN_TEST(test_txhold_idle_shade_is_held);
  RUN_TEST(test_txhold_moving_shade_is_not_held);
  RUN_TEST(test_txhold_ignores_ticket_zero);
  RUN_TEST(test_txhold_first_job_wins);
  RUN_TEST(test_txhold_release_only_matches_the_held_job);
  RUN_TEST(test_txhold_new_hold_after_release);
  RUN_TEST(test_txhold_times_out);
  RUN_TEST(test_txhold_times_out_across_millis_wrap);
  return UNITY_END();
}
