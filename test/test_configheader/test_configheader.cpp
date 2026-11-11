#include <unity.h>
#include "ConfigHeader.h"

void setUp() {}
void tearDown() {}

static config_header_t header() {
  config_header_t h;
  h.version = 22;
  h.roomRecords = 3;
  h.roomRecordSize = 29;
  h.shadeRecords = 5;
  h.shadeRecordSize = 276;
  h.groupRecords = 2;
  h.groupRecordSize = 200;
  h.repeaterRecords = 1;
  h.repeaterRecordSize = 77;
  h.settingsRecordSize = 100;
  return h;
}

static void test_shade_model_counts_rooms_shades_and_groups() {
  // A restore that leaves the shades out has to skip the rooms too, or it
  // reads the repeaters and settings from the room records.
  TEST_ASSERT_EQUAL_UINT32(3 * 29 + 5 * 276 + 2 * 200, shadeModelBytes(header()));
}
static void test_shade_model_leaves_out_the_rest() {
  config_header_t h = header();
  const uint32_t before = shadeModelBytes(h);
  h.repeaterRecordSize = 1;
  h.settingsRecordSize = 1;
  h.netRecordSize = 1;
  h.transRecordSize = 1;
  TEST_ASSERT_EQUAL_UINT32(before, shadeModelBytes(h));
}
static void test_an_old_file_without_rooms() {
  config_header_t h = header();
  h.roomRecords = 0;
  TEST_ASSERT_EQUAL_UINT32(5 * 276 + 2 * 200, shadeModelBytes(h));
}
static void test_a_full_model_does_not_overflow() {
  config_header_t h;
  h.roomRecords = h.shadeRecords = h.groupRecords = 255;
  h.roomRecordSize = h.shadeRecordSize = h.groupRecordSize = 0xFFFF;
  TEST_ASSERT_EQUAL_UINT32(3UL * 255 * 0xFFFF, shadeModelBytes(h));
}

// The header of a real backup: version 24, 10 shades, 2 groups, 1 repeater.
static config_header_t backupHeader(uint16_t transRecordSize) {
  config_header_t h;
  h.version = 24;
  h.length = 76;
  h.roomRecordSize = 29;
  h.shadeRecordSize = 276;
  h.shadeRecords = 10;
  h.groupRecordSize = 200;
  h.groupRecords = 2;
  h.repeaterRecordSize = 77;
  h.repeaterRecords = 1;
  h.settingsRecordSize = 94;
  h.netRecordSize = 166;
  h.transRecordSize = transRecordSize;
  return h;
}
static void test_a_backup_adds_up_to_its_file_size() {
  // That file was 3653 bytes: its radio record is 80, the header said 74.
  TEST_ASSERT_EQUAL_UINT32(3647, expectedFileBytes(backupHeader(74)));
  TEST_ASSERT_EQUAL_UINT32(3653, expectedFileBytes(backupHeader(80)));
}
static void test_old_versions_only_count_their_records() {
  config_header_t h = backupHeader(80);
  h.roomRecords = 3;
  h.version = 10;  // header and shades only
  TEST_ASSERT_EQUAL_UINT32(76 + 10 * 276, expectedFileBytes(h));
  h.version = 13;  // + groups
  TEST_ASSERT_EQUAL_UINT32(76 + 10 * 276 + 2 * 200, expectedFileBytes(h));
  h.version = 14;  // + settings, network, radio
  TEST_ASSERT_EQUAL_UINT32(76 + 10 * 276 + 2 * 200 + 94 + 166 + 80, expectedFileBytes(h));
  h.version = 19;  // + rooms
  TEST_ASSERT_EQUAL_UINT32(76 + 10 * 276 + 2 * 200 + 94 + 166 + 80 + 3 * 29, expectedFileBytes(h));
  h.version = 21;  // + repeaters
  TEST_ASSERT_EQUAL_UINT32(76 + 10 * 276 + 2 * 200 + 94 + 166 + 80 + 3 * 29 + 77, expectedFileBytes(h));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_a_backup_adds_up_to_its_file_size);
  RUN_TEST(test_old_versions_only_count_their_records);
  RUN_TEST(test_shade_model_counts_rooms_shades_and_groups);
  RUN_TEST(test_shade_model_leaves_out_the_rest);
  RUN_TEST(test_an_old_file_without_rooms);
  RUN_TEST(test_a_full_model_does_not_overflow);
  return UNITY_END();
}
