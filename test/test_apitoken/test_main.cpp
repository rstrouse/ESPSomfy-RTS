#include <unity.h>
#include "ApiToken.h"

void setUp() {}
void tearDown() {}

static const char TOKEN[] = "3f9a0c1e5b7d2468ace013579bdf2468ace013579bdf2468ace013579bdf2468";

/*********************************************************************
 * Comparing tokens
 ********************************************************************/
static void test_the_same_token_matches() {
  TEST_ASSERT_TRUE(tokenMatches(TOKEN, "3f9a0c1e5b7d2468ace013579bdf2468ace013579bdf2468ace013579bdf2468"));
}
static void test_a_token_differing_in_the_first_character_does_not_match() {
  TEST_ASSERT_FALSE(tokenMatches(TOKEN, "4f9a0c1e5b7d2468ace013579bdf2468ace013579bdf2468ace013579bdf2468"));
}
static void test_a_token_differing_in_the_last_character_does_not_match() {
  TEST_ASSERT_FALSE(tokenMatches(TOKEN, "3f9a0c1e5b7d2468ace013579bdf2468ace013579bdf2468ace013579bdf2469"));
}
static void test_a_prefix_of_the_token_does_not_match() {
  TEST_ASSERT_FALSE(tokenMatches(TOKEN, "3f9a0c1e5b7d2468ace013579bdf2468ace013579bdf2468ace013579bdf246"));
  TEST_ASSERT_FALSE(tokenMatches(TOKEN, "3f9a"));
}
static void test_the_token_with_more_after_it_does_not_match() {
  TEST_ASSERT_FALSE(tokenMatches(TOKEN, "3f9a0c1e5b7d2468ace013579bdf2468ace013579bdf2468ace013579bdf24680"));
}
static void test_an_empty_token_does_not_match() {
  TEST_ASSERT_FALSE(tokenMatches(TOKEN, ""));
}
static void test_a_missing_token_does_not_match() {
  TEST_ASSERT_FALSE(tokenMatches(TOKEN, nullptr));
}
static void test_nothing_matches_an_empty_expected_token() {
  TEST_ASSERT_FALSE(tokenMatches("", ""));
  TEST_ASSERT_FALSE(tokenMatches(nullptr, TOKEN));
}
static void test_case_matters() {
  TEST_ASSERT_FALSE(tokenMatches(TOKEN, "3F9A0C1E5B7D2468ACE013579BDF2468ACE013579BDF2468ACE013579BDF2468"));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_the_same_token_matches);
  RUN_TEST(test_a_token_differing_in_the_first_character_does_not_match);
  RUN_TEST(test_a_token_differing_in_the_last_character_does_not_match);
  RUN_TEST(test_a_prefix_of_the_token_does_not_match);
  RUN_TEST(test_the_token_with_more_after_it_does_not_match);
  RUN_TEST(test_an_empty_token_does_not_match);
  RUN_TEST(test_a_missing_token_does_not_match);
  RUN_TEST(test_nothing_matches_an_empty_expected_token);
  RUN_TEST(test_case_matters);
  return UNITY_END();
}
