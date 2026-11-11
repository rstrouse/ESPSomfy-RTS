#include <unity.h>
#include "SomfyFrame.h"

#define ASSERT_CMD(expected, actual) TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual))

static const uint32_t ADDR = 0x123456;

// A frame as SomfyRemote::sendCommand builds it.
static somfy_frame_t makeFrame(somfy_commands cmd, uint32_t address, uint16_t rollingCode,
                               uint8_t bitLength = 56, radio_proto proto = radio_proto::RTS) {
  somfy_frame_t f;
  f.cmd = cmd;
  f.remoteAddress = address;
  f.rollingCode = rollingCode;
  f.bitLength = bitLength;
  f.proto = proto;
  f.encKey = 0xA0 | (rollingCode & 0x0F);
  return f;
}
// What a receiver makes of the bytes, given it heard the same frame length.
static somfy_frame_t decode(const byte *payload, uint8_t bitLength) {
  byte buf[10];
  memcpy(buf, payload, sizeof(buf));
  somfy_frame_t d;
  d.bitLength = bitLength;
  d.decodeFrame(buf);
  return d;
}
static somfy_frame_t roundTrip(somfy_frame_t f) {
  byte buf[10];
  f.encodeFrame(buf);
  return decode(buf, f.bitLength);
}

void setUp() {}
void tearDown() {}

/*********************************************************************
 * Encoding
 ********************************************************************/
static void test_encode_56bit_golden_frame() {
  // Pins exactly what goes on air.  If this changes, find out why before
  // updating the bytes: motors reject frames that are encoded differently.
  somfy_frame_t f = makeFrame(somfy_commands::Up, ADDR, 0x0102);
  f.encKey = 0xA7;
  byte buf[10];
  f.encodeFrame(buf);
  const byte expected[10] = {0xA7, 0x8C, 0x8D, 0x8F, 0x9D, 0xA9, 0xFF, 0x84, 0x00, 0x1D};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, 10);

  somfy_frame_t d = decode(expected, 56);
  TEST_ASSERT_TRUE(d.valid);
  ASSERT_CMD(somfy_commands::Up, d.cmd);
  TEST_ASSERT_EQUAL_HEX32(ADDR, d.remoteAddress);
  TEST_ASSERT_EQUAL_HEX16(0x0102, d.rollingCode);
  TEST_ASSERT_EQUAL_HEX8(0xA7, d.encKey);
}
static void test_round_trip_56bit_commands() {
  const somfy_commands cmds[] = {
    somfy_commands::My, somfy_commands::Up, somfy_commands::MyUp, somfy_commands::Down,
    somfy_commands::MyDown, somfy_commands::UpDown, somfy_commands::MyUpDown, somfy_commands::Prog,
    somfy_commands::SunFlag, somfy_commands::Flag, somfy_commands::Toggle, somfy_commands::Sensor
  };
  for(somfy_commands cmd : cmds) {
    somfy_frame_t f = makeFrame(cmd, ADDR, 0x1234);
    somfy_frame_t d = roundTrip(f);
    TEST_ASSERT_TRUE_MESSAGE(d.valid, translateSomfyCommand(cmd).c_str());
    ASSERT_CMD(cmd, d.cmd);
    TEST_ASSERT_EQUAL_HEX32(ADDR, d.remoteAddress);
    TEST_ASSERT_EQUAL_HEX16(0x1234, d.rollingCode);
    TEST_ASSERT_EQUAL_HEX8(f.encKey, d.encKey);
    TEST_ASSERT_EQUAL(radio_proto::RTS, d.proto);
  }
}
static void test_round_trip_80bit_commands_and_repeats() {
  const somfy_commands cmds[] = {somfy_commands::Up, somfy_commands::Down, somfy_commands::My};
  for(somfy_commands cmd : cmds) {
    for(uint8_t repeats : {0, 5}) {
      somfy_frame_t f = makeFrame(cmd, ADDR, 0x0042, 80);
      f.repeats = repeats;
      byte buf[10];
      f.encodeFrame(buf);
      // The repeat counter is carried in byte 7.
      TEST_ASSERT_EQUAL_UINT8(196 + repeats * 4, buf[7]);
      somfy_frame_t d = decode(buf, 80);
      TEST_ASSERT_TRUE_MESSAGE(d.valid, translateSomfyCommand(cmd).c_str());
      ASSERT_CMD(cmd, d.cmd);
      TEST_ASSERT_EQUAL_HEX16(0x0042, d.rollingCode);
    }
  }
}
static void test_round_trip_80bit_favorite_and_stop() {
  // Sent as My with the real command in an extension nibble.
  for(somfy_commands cmd : {somfy_commands::Favorite, somfy_commands::Stop}) {
    somfy_frame_t d = roundTrip(makeFrame(cmd, ADDR, 7, 80));
    TEST_ASSERT_TRUE(d.valid);
    ASSERT_CMD(cmd, d.cmd);
  }
}
static void test_round_trip_80bit_steps() {
  for(somfy_commands cmd : {somfy_commands::StepUp, somfy_commands::StepDown}) {
    for(uint8_t step : {1, 100}) {
      somfy_frame_t f = makeFrame(cmd, ADDR, 7, 80);
      f.stepSize = step;
      somfy_frame_t d = roundTrip(f);
      TEST_ASSERT_TRUE(d.valid);
      ASSERT_CMD(cmd, d.cmd);
      TEST_ASSERT_EQUAL_UINT8(step, d.stepSize);
    }
  }
}
static void test_round_trip_80bit_toggle() {
  somfy_frame_t d = roundTrip(makeFrame(somfy_commands::Toggle, ADDR, 7, 80));
  TEST_ASSERT_TRUE(d.valid);
  ASSERT_CMD(somfy_commands::Toggle, d.cmd);
  TEST_ASSERT_EQUAL_UINT8(164, d.encKey);
}
static void test_round_trip_rtw_and_rtv() {
  // These protocols carry the command in the key byte.
  for(radio_proto proto : {radio_proto::RTW, radio_proto::RTV}) {
    for(somfy_commands cmd : {somfy_commands::Up, somfy_commands::Down}) {
      somfy_frame_t d = roundTrip(makeFrame(cmd, ADDR, 9, 56, proto));
      TEST_ASSERT_TRUE(d.valid);
      ASSERT_CMD(cmd, d.cmd);
      TEST_ASSERT_EQUAL(proto, d.proto);
    }
  }
}

/*********************************************************************
 * Rejected frames
 ********************************************************************/
static void test_rejects_bad_checksum() {
  byte buf[10];
  makeFrame(somfy_commands::Up, ADDR, 0x1234).encodeFrame(buf);
  // Byte 6 is the last obfuscated byte so the flip changes only its decoded value.
  buf[6] ^= 0x01;
  TEST_ASSERT_FALSE(decode(buf, 56).valid);
}
static void test_rejects_out_of_range_address() {
  TEST_ASSERT_FALSE(roundTrip(makeFrame(somfy_commands::Up, 0, 0x1234)).valid);
  TEST_ASSERT_FALSE(roundTrip(makeFrame(somfy_commands::Up, 0xFFFFFF, 0x1234)).valid);
}
static void test_rejects_zero_rolling_code_except_sensor() {
  TEST_ASSERT_FALSE(roundTrip(makeFrame(somfy_commands::Up, ADDR, 0)).valid);
  // Sensor frames carry flags in the rolling code so 0 is fine.
  TEST_ASSERT_TRUE(roundTrip(makeFrame(somfy_commands::Sensor, ADDR, 0)).valid);
}
static void test_rejects_zero_key() {
  somfy_frame_t f = makeFrame(somfy_commands::Up, ADDR, 0x1234);
  f.encKey = 0;
  TEST_ASSERT_FALSE(roundTrip(f).valid);
}
static void test_rejects_bad_80bit_parity() {
  byte buf[10];
  makeFrame(somfy_commands::Up, ADDR, 0x1234, 80).encodeFrame(buf);
  buf[9] ^= 0x01;
  TEST_ASSERT_FALSE(decode(buf, 80).valid);
}

/*********************************************************************
 * Helpers
 ********************************************************************/
static void test_encode80Byte7_wraps() {
  somfy_frame_t f;
  TEST_ASSERT_EQUAL_UINT8(196, f.encode80Byte7(196, 0));
  TEST_ASSERT_EQUAL_UINT8(252, f.encode80Byte7(196, 14));
  // Past 255 the counter steps back by 15 repeats.
  TEST_ASSERT_EQUAL_UINT8(196, f.encode80Byte7(196, 15));
  TEST_ASSERT_EQUAL_UINT8(216, f.encode80Byte7(196, 20));
}
static void test_copy_counts_repeats_and_marks_synonyms() {
  somfy_frame_t last = makeFrame(somfy_commands::Up, ADDR, 10);
  last.processed = true;

  somfy_frame_t repeat = makeFrame(somfy_commands::Up, ADDR, 10);
  last.copy(repeat);
  TEST_ASSERT_EQUAL_UINT8(1, last.repeats);

  // Same press reported as another command: keep what was already processed.
  somfy_frame_t synonym = makeFrame(somfy_commands::My, ADDR, 10);
  last.copy(synonym);
  TEST_ASSERT_TRUE(last.synonym);
  TEST_ASSERT_TRUE(last.processed);
  ASSERT_CMD(somfy_commands::My, last.cmd);

  // A new press replaces it.
  somfy_frame_t next = makeFrame(somfy_commands::Down, ADDR, 11);
  last.copy(next);
  TEST_ASSERT_FALSE(last.synonym);
  TEST_ASSERT_FALSE(last.processed);
  TEST_ASSERT_EQUAL_UINT16(11, last.rollingCode);
  TEST_ASSERT_EQUAL_UINT8(0, last.repeats);
}

/*********************************************************************
 * Command names
 ********************************************************************/
static void test_parse_command_names() {
  // What the web UI and the Home Assistant integration send.
  ASSERT_CMD(somfy_commands::Up, translateSomfyCommand(String("up")));
  ASSERT_CMD(somfy_commands::Down, translateSomfyCommand(String("DOWN")));
  ASSERT_CMD(somfy_commands::My, translateSomfyCommand(String("my")));
  ASSERT_CMD(somfy_commands::Toggle, translateSomfyCommand(String("toggle")));
  ASSERT_CMD(somfy_commands::Flag, translateSomfyCommand(String("flag")));
  ASSERT_CMD(somfy_commands::SunFlag, translateSomfyCommand(String("sunflag")));
  ASSERT_CMD(somfy_commands::StepUp, translateSomfyCommand(String("StepUp")));
  ASSERT_CMD(somfy_commands::StepDown, translateSomfyCommand(String("StepDown")));
  ASSERT_CMD(somfy_commands::Prog, translateSomfyCommand(String("Prog")));
  // Short forms.
  ASSERT_CMD(somfy_commands::Favorite, translateSomfyCommand(String("fav")));
  ASSERT_CMD(somfy_commands::MyUpDown, translateSomfyCommand(String("mud")));
  // A single hex digit is the raw command.
  ASSERT_CMD(somfy_commands::Down, translateSomfyCommand(String("4")));
  // Anything else falls back to My.
  ASSERT_CMD(somfy_commands::My, translateSomfyCommand(String("xyz")));
  ASSERT_CMD(somfy_commands::My, translateSomfyCommand(String("")));
}
static void test_command_display_names() {
  TEST_ASSERT_EQUAL_STRING("Up", translateSomfyCommand(somfy_commands::Up).c_str());
  TEST_ASSERT_EQUAL_STRING("My+Up", translateSomfyCommand(somfy_commands::MyUp).c_str());
  TEST_ASSERT_EQUAL_STRING("Sun Flag", translateSomfyCommand(somfy_commands::SunFlag).c_str());
  TEST_ASSERT_EQUAL_STRING("Step Up", translateSomfyCommand(somfy_commands::StepUp).c_str());
  TEST_ASSERT_EQUAL_STRING("Favorite", translateSomfyCommand(somfy_commands::Favorite).c_str());
  TEST_ASSERT_EQUAL_STRING("Unknown(13)", translateSomfyCommand(somfy_commands::UnknownD).c_str());
}
static void test_command_names_round_trip() {
  // The names in shadeCommand events (and Home Assistant events) must parse
  // back to the same command.
  const somfy_commands cmds[] = {
    somfy_commands::My, somfy_commands::Up, somfy_commands::MyUp, somfy_commands::Down,
    somfy_commands::MyDown, somfy_commands::UpDown, somfy_commands::MyUpDown, somfy_commands::Prog,
    somfy_commands::SunFlag, somfy_commands::Flag, somfy_commands::StepUp, somfy_commands::StepDown,
    somfy_commands::Sensor, somfy_commands::Toggle, somfy_commands::Favorite, somfy_commands::Stop
  };
  std::string failed;
  for(somfy_commands cmd : cmds) {
    String name = translateSomfyCommand(cmd);
    if(translateSomfyCommand(name) != cmd) failed += std::string(failed.empty() ? "" : ", ") + name.c_str();
  }
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", failed.c_str(), "These names don't parse back to their command");
}
static void test_sun_flag_command() {
  // sunFlag/set 1 must turn the flag on for shades and groups alike.
  TEST_ASSERT_EQUAL_UINT8((uint8_t)somfy_commands::SunFlag, (uint8_t)sunFlagCommand(true));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)somfy_commands::Flag, (uint8_t)sunFlagCommand(false));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_encode_56bit_golden_frame);
  RUN_TEST(test_round_trip_56bit_commands);
  RUN_TEST(test_round_trip_80bit_commands_and_repeats);
  RUN_TEST(test_round_trip_80bit_favorite_and_stop);
  RUN_TEST(test_round_trip_80bit_steps);
  RUN_TEST(test_round_trip_80bit_toggle);
  RUN_TEST(test_round_trip_rtw_and_rtv);
  RUN_TEST(test_rejects_bad_checksum);
  RUN_TEST(test_rejects_out_of_range_address);
  RUN_TEST(test_rejects_zero_rolling_code_except_sensor);
  RUN_TEST(test_rejects_zero_key);
  RUN_TEST(test_rejects_bad_80bit_parity);
  RUN_TEST(test_encode80Byte7_wraps);
  RUN_TEST(test_copy_counts_repeats_and_marks_synonyms);
  RUN_TEST(test_parse_command_names);
  RUN_TEST(test_command_display_names);
  RUN_TEST(test_command_names_round_trip);
  RUN_TEST(test_sun_flag_command);
  return UNITY_END();
}
