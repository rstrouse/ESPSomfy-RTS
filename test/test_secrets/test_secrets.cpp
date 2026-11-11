#include <unity.h>
#include "Secrets.h"

void setUp() {}
void tearDown() {}

/*********************************************************************
 * hasSecret / maskSecret
 ********************************************************************/
static void test_hasSecret_null_and_empty_are_unset() {
  TEST_ASSERT_FALSE(hasSecret(nullptr));
  TEST_ASSERT_FALSE(hasSecret(""));
}
static void test_hasSecret_any_value_is_set() {
  TEST_ASSERT_TRUE(hasSecret("x"));
  TEST_ASSERT_TRUE(hasSecret(" "));
}
static void test_maskSecret_never_returns_the_secret() {
  TEST_ASSERT_EQUAL_STRING("<set>", maskSecret("hunter2"));
  TEST_ASSERT_EQUAL_STRING("<empty>", maskSecret(""));
  TEST_ASSERT_EQUAL_STRING("<empty>", maskSecret(nullptr));
}

/*********************************************************************
 * applySecret
 ********************************************************************/
static void test_applySecret_absent_keeps_stored_value() {
  char dest[16] = "stored";
  TEST_ASSERT_FALSE(applySecret(nullptr, dest, sizeof(dest)));
  TEST_ASSERT_EQUAL_STRING("stored", dest);
}
static void test_applySecret_new_value_replaces_stored() {
  char dest[16] = "stored";
  TEST_ASSERT_TRUE(applySecret("fresh", dest, sizeof(dest)));
  TEST_ASSERT_EQUAL_STRING("fresh", dest);
}
static void test_applySecret_same_value_is_no_change() {
  char dest[16] = "stored";
  TEST_ASSERT_FALSE(applySecret("stored", dest, sizeof(dest)));
  TEST_ASSERT_EQUAL_STRING("stored", dest);
}
static void test_applySecret_prefix_of_stored_is_a_change() {
  char dest[16] = "stored";
  TEST_ASSERT_TRUE(applySecret("sto", dest, sizeof(dest)));
  TEST_ASSERT_EQUAL_STRING("sto", dest);
}
// An older UI sends "" for a field the device no longer fills in.
static void test_applySecret_empty_value_keeps_stored() {
  char dest[16] = "stored";
  TEST_ASSERT_FALSE(applySecret("", dest, sizeof(dest)));
  TEST_ASSERT_EQUAL_STRING("stored", dest);
}
// A new SSID or username with an empty secret, e.g. an open WiFi network.
static void test_applySecret_empty_value_with_new_identity_clears_stored() {
  char dest[16] = "stored";
  TEST_ASSERT_TRUE(applySecret("", dest, sizeof(dest), true));
  TEST_ASSERT_EQUAL_STRING("", dest);
}
static void test_applySecret_empty_value_with_new_identity_and_nothing_stored() {
  char dest[16] = "";
  TEST_ASSERT_FALSE(applySecret("", dest, sizeof(dest), true));
  TEST_ASSERT_EQUAL_STRING("", dest);
}
static void test_applySecret_new_identity_still_takes_a_value() {
  char dest[16] = "stored";
  TEST_ASSERT_TRUE(applySecret("fresh", dest, sizeof(dest), true));
  TEST_ASSERT_EQUAL_STRING("fresh", dest);
}
static void test_applySecret_absent_keeps_stored_even_with_new_identity() {
  char dest[16] = "stored";
  TEST_ASSERT_FALSE(applySecret(nullptr, dest, sizeof(dest), true));
  TEST_ASSERT_EQUAL_STRING("stored", dest);
}
static void test_applySecret_truncates_and_terminates() {
  char storage[8];
  memset(storage, 'X', sizeof(storage));
  storage[0] = '\0';
  TEST_ASSERT_TRUE(applySecret("abcdefgh", storage, 5));
  TEST_ASSERT_EQUAL_STRING("abcd", storage);
  // Nothing past the buffer size is touched.
  TEST_ASSERT_EQUAL_HEX8('X', storage[5]);
}
static void test_applySecret_truncated_same_value_is_no_change() {
  char dest[5] = "abcd";
  TEST_ASSERT_FALSE(applySecret("abcdefgh", dest, sizeof(dest)));
  TEST_ASSERT_EQUAL_STRING("abcd", dest);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_hasSecret_null_and_empty_are_unset);
  RUN_TEST(test_hasSecret_any_value_is_set);
  RUN_TEST(test_maskSecret_never_returns_the_secret);
  RUN_TEST(test_applySecret_absent_keeps_stored_value);
  RUN_TEST(test_applySecret_new_value_replaces_stored);
  RUN_TEST(test_applySecret_same_value_is_no_change);
  RUN_TEST(test_applySecret_prefix_of_stored_is_a_change);
  RUN_TEST(test_applySecret_empty_value_keeps_stored);
  RUN_TEST(test_applySecret_empty_value_with_new_identity_clears_stored);
  RUN_TEST(test_applySecret_empty_value_with_new_identity_and_nothing_stored);
  RUN_TEST(test_applySecret_new_identity_still_takes_a_value);
  RUN_TEST(test_applySecret_absent_keeps_stored_even_with_new_identity);
  RUN_TEST(test_applySecret_truncates_and_terminates);
  RUN_TEST(test_applySecret_truncated_same_value_is_no_change);
  return UNITY_END();
}
