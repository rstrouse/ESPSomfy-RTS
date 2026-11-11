#include <unity.h>
#include "ShadeSensors.h"

#define ASSERT_MOVE(expected, actual) TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual))

static const bool SUN_FLAG = true;
static const uint32_t T0 = 1000;

void setUp() {}
void tearDown() {}

// Sensors that have just reported sun, with no wind, at T0.
static ShadeSensors sunnyAt(uint32_t now) {
  ShadeSensors s;
  s.onSensorFrame(false, false, true, false, now);
  return s;
}

/*********************************************************************
 * sun
 ********************************************************************/
static void test_sun_extends_after_its_timeout() {
  ShadeSensors s = sunnyAt(T0);
  ASSERT_MOVE(SensorMove::none, s.check(SUN_FLAG, true, false, T0 + SOMFY_SUN_TIMEOUT - 1));
  ASSERT_MOVE(SensorMove::extend, s.check(SUN_FLAG, true, false, T0 + SOMFY_SUN_TIMEOUT));
}
static void test_sun_extends_once() {
  ShadeSensors s = sunnyAt(T0);
  ASSERT_MOVE(SensorMove::extend, s.check(SUN_FLAG, true, false, T0 + SOMFY_SUN_TIMEOUT));
  ASSERT_MOVE(SensorMove::none, s.check(SUN_FLAG, true, false, T0 + SOMFY_SUN_TIMEOUT + 60000));
}
static void test_sun_needs_the_sun_flag() {
  ShadeSensors s = sunnyAt(T0);
  ASSERT_MOVE(SensorMove::none, s.check(!SUN_FLAG, true, false, T0 + SOMFY_SUN_TIMEOUT));
  // The timer is still due once the flag goes on.
  ASSERT_MOVE(SensorMove::extend, s.check(SUN_FLAG, true, false, T0 + SOMFY_SUN_TIMEOUT + 1));
}
static void test_sun_does_not_extend_in_wind() {
  ShadeSensors s = sunnyAt(T0);
  ASSERT_MOVE(SensorMove::none, s.check(SUN_FLAG, true, true, T0 + SOMFY_SUN_TIMEOUT));
}
static void test_steady_sun_does_not_restart_the_timer() {
  ShadeSensors s = sunnyAt(T0);
  s.onSensorFrame(true, false, true, false, T0 + 60000);
  ASSERT_MOVE(SensorMove::extend, s.check(SUN_FLAG, true, false, T0 + SOMFY_SUN_TIMEOUT));
}
static void test_no_sun_retracts_after_its_timeout() {
  ShadeSensors s = sunnyAt(T0);
  s.onSensorFrame(true, false, false, false, T0 + 10);
  ASSERT_MOVE(SensorMove::none, s.check(SUN_FLAG, false, false, T0 + 10 + SOMFY_NO_SUN_TIMEOUT - 1));
  ASSERT_MOVE(SensorMove::retract, s.check(SUN_FLAG, false, false, T0 + 10 + SOMFY_NO_SUN_TIMEOUT));
  ASSERT_MOVE(SensorMove::none, s.check(SUN_FLAG, false, false, T0 + 10 + SOMFY_NO_SUN_TIMEOUT + 1));
}
static void test_cloud_cancels_the_sun_timer() {
  ShadeSensors s = sunnyAt(T0);
  s.onSensorFrame(true, false, false, false, T0 + 10);
  s.onSensorFrame(false, false, true, false, T0 + 20);
  // Sun came back before the no-sun timeout: neither fires early.
  ASSERT_MOVE(SensorMove::none, s.check(SUN_FLAG, true, false, T0 + 20 + SOMFY_SUN_TIMEOUT - 1));
  ASSERT_MOVE(SensorMove::extend, s.check(SUN_FLAG, true, false, T0 + 20 + SOMFY_SUN_TIMEOUT));
}

/*********************************************************************
 * wind
 ********************************************************************/
static void test_wind_retracts_without_the_sun_flag() {
  ShadeSensors s;
  s.onSensorFrame(false, false, false, true, T0);
  ASSERT_MOVE(SensorMove::none, s.check(!SUN_FLAG, false, true, T0 + SOMFY_WIND_TIMEOUT - 1));
  ASSERT_MOVE(SensorMove::retract, s.check(!SUN_FLAG, false, true, T0 + SOMFY_WIND_TIMEOUT));
  ASSERT_MOVE(SensorMove::none, s.check(!SUN_FLAG, false, true, T0 + SOMFY_WIND_TIMEOUT + 1));
}
static void test_wind_dying_down_extends_again_in_sun() {
  ShadeSensors s = sunnyAt(T0);
  ASSERT_MOVE(SensorMove::extend, s.check(SUN_FLAG, true, false, T0 + SOMFY_SUN_TIMEOUT));
  const uint32_t windy = T0 + SOMFY_SUN_TIMEOUT + 10;
  s.onSensorFrame(true, false, true, true, windy);
  ASSERT_MOVE(SensorMove::retract, s.check(SUN_FLAG, true, true, windy + SOMFY_WIND_TIMEOUT));
  const uint32_t calm = windy + SOMFY_WIND_TIMEOUT + 10;
  s.onSensorFrame(true, true, true, false, calm);
  ASSERT_MOVE(SensorMove::none, s.check(SUN_FLAG, true, false, calm + SOMFY_NO_WIND_TIMEOUT - 1));
  ASSERT_MOVE(SensorMove::extend, s.check(SUN_FLAG, true, false, calm + SOMFY_NO_WIND_TIMEOUT));
}
static void test_wind_blocks_a_remote_down_for_a_while() {
  ShadeSensors s;
  TEST_ASSERT_FALSE(s.windBlocksDown(T0));
  s.onSensorFrame(false, false, false, true, T0);
  TEST_ASSERT_TRUE(s.windBlocksDown(T0 + SOMFY_NO_WIND_REMOTE_TIMEOUT - 1));
  TEST_ASSERT_FALSE(s.windBlocksDown(T0 + SOMFY_NO_WIND_REMOTE_TIMEOUT));
}
static void test_every_windy_frame_extends_the_down_block() {
  ShadeSensors s;
  s.onSensorFrame(false, false, false, true, T0);
  s.onSensorFrame(false, true, false, true, T0 + 20000);
  TEST_ASSERT_TRUE(s.windBlocksDown(T0 + SOMFY_NO_WIND_REMOTE_TIMEOUT + 1));
}

/*********************************************************************
 * sun flag turned on
 ********************************************************************/
static void test_sun_flag_acts_on_a_finished_timer() {
  ShadeSensors s;
  // Nothing pending: the shade goes where the weather says.
  ASSERT_MOVE(SensorMove::extend, s.onSunFlag(true, false));
  ASSERT_MOVE(SensorMove::retract, s.onSunFlag(false, false));
}
static void test_sun_flag_waits_for_a_running_timer() {
  ShadeSensors s = sunnyAt(T0);
  ASSERT_MOVE(SensorMove::none, s.onSunFlag(true, false));
  s.onSensorFrame(true, false, false, false, T0 + 10);
  ASSERT_MOVE(SensorMove::none, s.onSunFlag(false, false));
}
static void test_sun_flag_does_nothing_in_wind() {
  ShadeSensors s;
  ASSERT_MOVE(SensorMove::none, s.onSunFlag(true, true));
  ASSERT_MOVE(SensorMove::none, s.onSunFlag(false, true));
}

/*********************************************************************
 * clear and millis() rollover
 ********************************************************************/
static void test_clear_forgets_pending_timers() {
  ShadeSensors s = sunnyAt(T0);
  s.onSensorFrame(false, false, false, true, T0);
  s.clear();
  ASSERT_MOVE(SensorMove::none, s.check(SUN_FLAG, true, false, T0 + SOMFY_NO_SUN_TIMEOUT));
  TEST_ASSERT_FALSE(s.windBlocksDown(T0 + 1));
}
static void test_timers_run_across_a_millis_rollover() {
  const uint32_t start = 0xFFFFFFFFUL - 1000;
  ShadeSensors s = sunnyAt(start);
  ASSERT_MOVE(SensorMove::none, s.check(SUN_FLAG, true, false, start + 2000));
  ASSERT_MOVE(SensorMove::extend, s.check(SUN_FLAG, true, false, start + SOMFY_SUN_TIMEOUT));
  s.onSensorFrame(false, false, false, true, start);
  TEST_ASSERT_TRUE(s.windBlocksDown(start + 2000));
}

/*********************************************************************
 * sensorTargets
 ********************************************************************/
#define ASSERT_POS(expected, actual) TEST_ASSERT_FLOAT_WITHIN(0.01f, (expected), (actual))
static const float NO_FAVORITE = -1.0f;

static void test_lift_shade_extends_to_its_favorite() {
  SensorTargets t = sensorTargets(SensorMove::extend, tilt_types::none, 60.0f, NO_FAVORITE);
  TEST_ASSERT_TRUE(t.setLift);
  ASSERT_POS(60.0f, t.lift);
  TEST_ASSERT_FALSE(t.setTilt);
}
static void test_lift_shade_extends_fully_without_a_favorite() {
  SensorTargets t = sensorTargets(SensorMove::extend, tilt_types::integrated, NO_FAVORITE, 30.0f);
  TEST_ASSERT_TRUE(t.setLift);
  ASSERT_POS(100.0f, t.lift);
  TEST_ASSERT_FALSE(t.setTilt);
}
static void test_lift_shade_retracts_fully() {
  SensorTargets t = sensorTargets(SensorMove::retract, tilt_types::tiltmotor, 60.0f, 30.0f);
  TEST_ASSERT_TRUE(t.setLift);
  ASSERT_POS(0.0f, t.lift);
  TEST_ASSERT_FALSE(t.setTilt);
}
static void test_tilt_only_shade_only_moves_its_tilt() {
  SensorTargets t = sensorTargets(SensorMove::extend, tilt_types::tiltonly, 60.0f, 30.0f);
  TEST_ASSERT_FALSE(t.setLift);
  TEST_ASSERT_TRUE(t.setTilt);
  ASSERT_POS(30.0f, t.tilt);
  t = sensorTargets(SensorMove::extend, tilt_types::tiltonly, 60.0f, NO_FAVORITE);
  TEST_ASSERT_FALSE(t.setLift);
  ASSERT_POS(100.0f, t.tilt);
  t = sensorTargets(SensorMove::retract, tilt_types::tiltonly, 60.0f, 30.0f);
  TEST_ASSERT_FALSE(t.setLift);
  TEST_ASSERT_TRUE(t.setTilt);
  ASSERT_POS(0.0f, t.tilt);
}
static void test_no_move_sets_nothing() {
  SensorTargets t = sensorTargets(SensorMove::none, tilt_types::tiltonly, 60.0f, 30.0f);
  TEST_ASSERT_FALSE(t.setLift);
  TEST_ASSERT_FALSE(t.setTilt);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_sun_extends_after_its_timeout);
  RUN_TEST(test_sun_extends_once);
  RUN_TEST(test_sun_needs_the_sun_flag);
  RUN_TEST(test_sun_does_not_extend_in_wind);
  RUN_TEST(test_steady_sun_does_not_restart_the_timer);
  RUN_TEST(test_no_sun_retracts_after_its_timeout);
  RUN_TEST(test_cloud_cancels_the_sun_timer);
  RUN_TEST(test_wind_retracts_without_the_sun_flag);
  RUN_TEST(test_wind_dying_down_extends_again_in_sun);
  RUN_TEST(test_wind_blocks_a_remote_down_for_a_while);
  RUN_TEST(test_every_windy_frame_extends_the_down_block);
  RUN_TEST(test_sun_flag_acts_on_a_finished_timer);
  RUN_TEST(test_sun_flag_waits_for_a_running_timer);
  RUN_TEST(test_sun_flag_does_nothing_in_wind);
  RUN_TEST(test_clear_forgets_pending_timers);
  RUN_TEST(test_timers_run_across_a_millis_rollover);
  RUN_TEST(test_lift_shade_extends_to_its_favorite);
  RUN_TEST(test_lift_shade_extends_fully_without_a_favorite);
  RUN_TEST(test_lift_shade_retracts_fully);
  RUN_TEST(test_tilt_only_shade_only_moves_its_tilt);
  RUN_TEST(test_no_move_sets_nothing);
  return UNITY_END();
}
