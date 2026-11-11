#include <unity.h>
#include "TaskWdt.h"

void setUp() { fakeTaskWdt = FakeTaskWdt(); }
void tearDown() {}

/*********************************************************************
 * resetTaskWdt
 ********************************************************************/
static void test_resetTaskWdt_resets_a_subscribed_task() {
  fakeTaskWdt.subscribed = true;
  resetTaskWdt();
  TEST_ASSERT_EQUAL_INT(1, fakeTaskWdt.resets);
}
static void test_resetTaskWdt_skips_a_task_that_is_not_subscribed() {
  // The WiFi event task and async_tcp.
  resetTaskWdt();
  TEST_ASSERT_EQUAL_INT(0, fakeTaskWdt.resets);
  TEST_ASSERT_FALSE(fakeTaskWdt.subscribed);
}

/*********************************************************************
 * pauseTaskWdt / resumeTaskWdt
 ********************************************************************/
static void test_pause_resume_puts_a_watched_task_back() {
  fakeTaskWdt.subscribed = true;
  const bool paused = pauseTaskWdt();
  TEST_ASSERT_TRUE(paused);
  TEST_ASSERT_FALSE(fakeTaskWdt.subscribed);
  resumeTaskWdt(paused);
  TEST_ASSERT_TRUE(fakeTaskWdt.subscribed);
}
static void test_pause_resume_leaves_an_unwatched_task_unwatched() {
  // /scanaps on async_tcp: adding it would panic once nothing resets it.
  const bool paused = pauseTaskWdt();
  TEST_ASSERT_FALSE(paused);
  resumeTaskWdt(paused);
  TEST_ASSERT_FALSE(fakeTaskWdt.subscribed);
  TEST_ASSERT_EQUAL_INT(0, fakeTaskWdt.adds);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_resetTaskWdt_resets_a_subscribed_task);
  RUN_TEST(test_resetTaskWdt_skips_a_task_that_is_not_subscribed);
  RUN_TEST(test_pause_resume_puts_a_watched_task_back);
  RUN_TEST(test_pause_resume_leaves_an_unwatched_task_unwatched);
  return UNITY_END();
}
