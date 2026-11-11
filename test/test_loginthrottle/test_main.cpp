#include <unity.h>
#include "LoginThrottle.h"

void setUp() {}
void tearDown() {}

static const uint32_t IP_A = 0x0A01A8C0;  // 192.168.1.10
static const uint32_t IP_B = 0x0B01A8C0;  // 192.168.1.11

// Fails ip n times at now, as fast as the throttle lets it.
static void failTimes(LoginThrottle &t, uint32_t ip, int n, uint64_t now) {
  for(int i = 0; i < n; i++) t.failed(ip, now);
}

/*********************************************************************
 * The schedule
 ********************************************************************/
static void test_the_first_failures_are_free() {
  for(uint16_t f = 0; f < LOGIN_FREE_FAILURES; f++)
    TEST_ASSERT_EQUAL_UINT32(0, LoginThrottle::lockoutFor(f));
}
static void test_the_fifth_failure_locks_out_for_30s() {
  TEST_ASSERT_EQUAL_UINT32(30000, LoginThrottle::lockoutFor(5));
}
static void test_each_later_failure_doubles_up_to_15_minutes() {
  TEST_ASSERT_EQUAL_UINT32(60000, LoginThrottle::lockoutFor(6));
  TEST_ASSERT_EQUAL_UINT32(120000, LoginThrottle::lockoutFor(7));
  TEST_ASSERT_EQUAL_UINT32(240000, LoginThrottle::lockoutFor(8));
  TEST_ASSERT_EQUAL_UINT32(480000, LoginThrottle::lockoutFor(9));
  TEST_ASSERT_EQUAL_UINT32(900000, LoginThrottle::lockoutFor(10));
  TEST_ASSERT_EQUAL_UINT32(900000, LoginThrottle::lockoutFor(11));
  TEST_ASSERT_EQUAL_UINT32(900000, LoginThrottle::lockoutFor(UINT16_MAX));
}

/*********************************************************************
 * One IP
 ********************************************************************/
static void test_an_ip_with_no_failures_may_try() {
  LoginThrottle t;
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(IP_A, 1000));
}
static void test_four_failures_cost_nothing() {
  LoginThrottle t;
  for(int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT32(0, t.failed(IP_A, 1000));
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(IP_A, 1000));
}
static void test_the_fifth_failure_refuses_the_ip_for_30s() {
  LoginThrottle t;
  failTimes(t, IP_A, 4, 1000);
  TEST_ASSERT_EQUAL_UINT32(30000, t.failed(IP_A, 1000));
  TEST_ASSERT_EQUAL_UINT32(30000, t.retryAfter(IP_A, 1000));
  TEST_ASSERT_EQUAL_UINT32(1, t.retryAfter(IP_A, 30999));
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(IP_A, 31000));
}
static void test_after_a_lockout_the_next_failure_locks_out_twice_as_long() {
  LoginThrottle t;
  failTimes(t, IP_A, 5, 0);
  TEST_ASSERT_EQUAL_UINT32(60000, t.failed(IP_A, 30000));
  TEST_ASSERT_EQUAL_UINT32(60000, t.retryAfter(IP_A, 30000));
  TEST_ASSERT_EQUAL_UINT32(120000, t.failed(IP_A, 90000));
}
static void test_a_success_resets_the_ip() {
  LoginThrottle t;
  failTimes(t, IP_A, 6, 0);
  t.succeeded(IP_A);
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(IP_A, 0));
  // Its free failures are back.
  for(int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT32(0, t.failed(IP_A, 0));
  TEST_ASSERT_EQUAL_UINT32(30000, t.failed(IP_A, 0));
}
static void test_an_hour_without_a_failure_forgets_the_ip() {
  LoginThrottle t;
  failTimes(t, IP_A, 7, 0);
  // Still counted just before the hour: the next failure doubles.
  TEST_ASSERT_EQUAL_UINT32(240000, t.failed(IP_A, LOGIN_FORGET_MS - 1));
  uint64_t later = 2 * LOGIN_FORGET_MS;
  TEST_ASSERT_EQUAL_UINT32(0, t.failed(IP_A, later));
}
static void test_a_lockout_spans_where_millis_would_wrap() {
  LoginThrottle t;
  uint64_t now = UINT32_MAX - 10000ULL;
  failTimes(t, IP_A, 5, now);
  TEST_ASSERT_EQUAL_UINT32(10000, t.retryAfter(IP_A, now + 20000));
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(IP_A, now + 30000));
}
static void test_a_slot_left_alone_for_50_days_stays_forgotten() {
  LoginThrottle t;
  failTimes(t, IP_A, 10, 1000);
  // Where a 32-bit millis() would have come round to the same value.
  uint64_t wrapped = 1000 + (1ULL << 32);
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(IP_A, wrapped));
  TEST_ASSERT_EQUAL_UINT32(0, t.failed(IP_A, wrapped));
}

/*********************************************************************
 * Several IPs
 ********************************************************************/
static void test_one_ip_locked_out_leaves_the_others_alone() {
  LoginThrottle t;
  failTimes(t, IP_A, 5, 0);
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(IP_B, 0));
  TEST_ASSERT_EQUAL_UINT32(0, t.failed(IP_B, 0));
}
static void test_a_success_resets_only_its_own_ip() {
  LoginThrottle t;
  failTimes(t, IP_A, 5, 0);
  t.succeeded(IP_B);
  TEST_ASSERT_EQUAL_UINT32(30000, t.retryAfter(IP_A, 0));
}
static void test_with_the_table_full_other_ips_share_one_slot() {
  LoginThrottle t;
  for(uint32_t i = 0; i < LOGIN_THROTTLE_SLOTS; i++) failTimes(t, 100 + i, 1, 0);
  // New addresses count against each other: five between them lock all out.
  for(uint32_t i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT32(0, t.failed(200 + i, 0));
  TEST_ASSERT_EQUAL_UINT32(30000, t.failed(204, 0));
  TEST_ASSERT_EQUAL_UINT32(30000, t.retryAfter(205, 0));
  // The IPs that have a slot keep their own count.
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(100, 0));
}
static void test_a_forgotten_slot_is_taken_again() {
  LoginThrottle t;
  for(uint32_t i = 0; i < LOGIN_THROTTLE_SLOTS; i++) failTimes(t, 100 + i, 5, 0);
  // An hour later every slot is vacant: a new IP has its own free failures.
  uint64_t later = LOGIN_FORGET_MS;
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(IP_A, later));
  failTimes(t, IP_A, 4, later);
  TEST_ASSERT_EQUAL_UINT32(30000, t.failed(IP_A, later));
  // Taken by IP_A, not shared: another new IP isn't refused.
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(IP_B, later));
}
static void test_a_success_doesnt_clear_the_shared_slot() {
  LoginThrottle t;
  for(uint32_t i = 0; i < LOGIN_THROTTLE_SLOTS; i++) failTimes(t, 100 + i, 1, 0);
  failTimes(t, 200, 5, 0);
  // 200 is counted in the shared slot, not one of its own.
  t.succeeded(200);
  TEST_ASSERT_EQUAL_UINT32(30000, t.retryAfter(202, 0));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_the_first_failures_are_free);
  RUN_TEST(test_the_fifth_failure_locks_out_for_30s);
  RUN_TEST(test_each_later_failure_doubles_up_to_15_minutes);
  RUN_TEST(test_an_ip_with_no_failures_may_try);
  RUN_TEST(test_four_failures_cost_nothing);
  RUN_TEST(test_the_fifth_failure_refuses_the_ip_for_30s);
  RUN_TEST(test_after_a_lockout_the_next_failure_locks_out_twice_as_long);
  RUN_TEST(test_a_success_resets_the_ip);
  RUN_TEST(test_an_hour_without_a_failure_forgets_the_ip);
  RUN_TEST(test_a_lockout_spans_where_millis_would_wrap);
  RUN_TEST(test_a_slot_left_alone_for_50_days_stays_forgotten);
  RUN_TEST(test_one_ip_locked_out_leaves_the_others_alone);
  RUN_TEST(test_a_success_resets_only_its_own_ip);
  RUN_TEST(test_with_the_table_full_other_ips_share_one_slot);
  RUN_TEST(test_a_forgotten_slot_is_taken_again);
  RUN_TEST(test_a_success_doesnt_clear_the_shared_slot);
  return UNITY_END();
}
