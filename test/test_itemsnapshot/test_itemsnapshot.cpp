#include <unity.h>
#include <string.h>
#include <string>
#include "ItemSnapshot.h"

static shade_snapshot_t shade(uint8_t id, const char *name) {
  shade_snapshot_t s{};
  s.shadeId = id;
  s.roomId = 2;
  strcpy(s.name, name);
  s.remoteAddress = 1000 + id;
  s.paired = true;
  s.shadeType = 1;
  s.bitLength = 56;
  s.repeats = 1;
  return s;
}
static group_snapshot_t group(uint8_t id, const char *name) {
  group_snapshot_t g{};
  g.groupId = id;
  strcpy(g.name, name);
  g.remoteAddress = 2000 + id;
  g.lastRollingCode = 5;
  g.bitLength = 80;
  g.repeats = 2;
  return g;
}
static std::string shadeRefJson(const shade_snapshot_t &s) {
  static char buf[1024];
  BufferedJsonFormatter fmt;
  fmt.setBuffer(buf, sizeof(buf));
  fmt.beginObject();
  writeShadeRef(fmt, s);
  fmt.endObject();
  return std::string(buf, fmt.length());
}
static std::string groupRefJson(const group_snapshot_t &g) {
  static char buf[1024];
  BufferedJsonFormatter fmt;
  fmt.setBuffer(buf, sizeof(buf));
  fmt.beginObject();
  writeGroupRef(fmt, g);
  fmt.endObject();
  return std::string(buf, fmt.length());
}

void setUp() {}
void tearDown() {}

/*********************************************************************
 * The command replies, as the routes have always written them
 ********************************************************************/
static void test_a_shade_ref() {
  TEST_ASSERT_EQUAL_STRING(
    "{\"shadeId\":1,\"roomId\":2,\"name\":\"Kitchen\",\"remoteAddress\":1001,\"paired\":true,\"shadeType\":1,"
    "\"flipCommands\":false,\"flipPosition\":false,\"bitLength\":56,\"proto\":0,\"flags\":0,\"sunSensor\":false,"
    "\"hasLight\":false,\"repeats\":1}",
    shadeRefJson(shade(1, "Kitchen")).c_str());
}
static void test_a_group_ref() {
  TEST_ASSERT_EQUAL_STRING(
    "{\"groupId\":6,\"roomId\":0,\"name\":\"All\",\"remoteAddress\":2006,\"lastRollingCode\":5,\"bitLength\":80,"
    "\"proto\":0,\"sunSensor\":false,\"flipCommands\":false,\"flags\":0,\"repeats\":2,\"sortOrder\":0}",
    groupRefJson(group(6, "All")).c_str());
}
static void test_names_are_escaped() {
  const std::string json = shadeRefJson(shade(1, "A \"big\" one"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"name\":\"A \\\"big\\\" one\""));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_shade_ref);
  RUN_TEST(test_a_group_ref);
  RUN_TEST(test_names_are_escaped);
  return UNITY_END();
}
