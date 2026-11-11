#include <unity.h>
#include <string.h>
#include <ArduinoJson.h>
#include "ProtoOps.h"

// Socket protocol 2's command ops: what each payload asks for, and what is
// refused before anything reaches loop() (docs/PROTOCOL.md).

void setUp() {}
void tearDown() {}

static JsonDocument doc;
static shade_request_t req;

static const char *parse(proto_type_t type, const char *payload) {
  doc.clear();
  TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(doc, payload).code());
  return parseShadeOp(static_cast<uint8_t>(type), doc.as<JsonObjectConst>(), req);
}
#define TEST_ASSERT_GOOD(why) TEST_ASSERT_NULL_MESSAGE(why, why)

/*********************************************************************
 * Which ops are shade requests
 ********************************************************************/
static void test_every_command_op_but_the_remote_frame_is_a_shade_request() {
  const proto_type_t shadeOps[] = {
    proto_type_t::shadeCommand, proto_type_t::groupCommand, proto_type_t::tiltCommand, proto_type_t::repeatCommand,
    proto_type_t::setPositions, proto_type_t::setSensor, proto_type_t::setMyPosition, proto_type_t::unpairShade,
    proto_type_t::setRollingCode
  };
  for(proto_type_t t : shadeOps) TEST_ASSERT_EQUAL(proto_op_t::shadeRequest, protoOpKind(static_cast<uint8_t>(t)));
  TEST_ASSERT_EQUAL(proto_op_t::remoteFrame, protoOpKind(static_cast<uint8_t>(proto_type_t::sendRemoteCommand)));
  TEST_ASSERT_EQUAL(proto_op_t::none, protoOpKind(static_cast<uint8_t>(proto_type_t::subscribe)));
  TEST_ASSERT_EQUAL(proto_op_t::none, protoOpKind(static_cast<uint8_t>(proto_type_t::shadeCommanded)));
}

/*********************************************************************
 * Commands
 ********************************************************************/
static void test_shade_command_by_name() {
  TEST_ASSERT_GOOD(parse(proto_type_t::shadeCommand, R"({"s":3,"cm":"Down"})"));
  TEST_ASSERT_EQUAL(shade_request_type_t::shade_command, req.type);
  TEST_ASSERT_EQUAL_UINT8(3, req.id);
  TEST_ASSERT_EQUAL(somfy_commands::Down, req.command);
  TEST_ASSERT_EQUAL_INT8(-1, req.target);
  // Left out: the shade's own repeats.
  TEST_ASSERT_EQUAL_INT8(-1, req.repeat);
}
static void test_shade_command_to_a_target() {
  TEST_ASSERT_GOOD(parse(proto_type_t::shadeCommand, R"({"s":3,"t":0})"));
  TEST_ASSERT_EQUAL_INT8(0, req.target);
}
static void test_shade_command_with_repeat_and_step() {
  TEST_ASSERT_GOOD(parse(proto_type_t::shadeCommand, R"({"s":3,"cm":"StepUp","re":0,"z":5})"));
  TEST_ASSERT_EQUAL(somfy_commands::StepUp, req.command);
  TEST_ASSERT_EQUAL_INT8(0, req.repeat);
  TEST_ASSERT_EQUAL_UINT8(5, req.stepSize);
}
static void test_shade_command_needs_a_command_or_a_target_not_both() {
  TEST_ASSERT_EQUAL_STRING("Give command or target", parse(proto_type_t::shadeCommand, R"({"s":3})"));
  TEST_ASSERT_EQUAL_STRING("Give command or target", parse(proto_type_t::shadeCommand, R"({"s":3,"cm":"Up","t":10})"));
}
static void test_shade_command_values_are_checked() {
  TEST_ASSERT_EQUAL_STRING("shadeId is missing", parse(proto_type_t::shadeCommand, R"({"cm":"Up"})"));
  TEST_ASSERT_EQUAL_STRING("shadeId must be 0-255", parse(proto_type_t::shadeCommand, R"({"s":"3","cm":"Up"})"));
  TEST_ASSERT_EQUAL_STRING("target must be 0-100", parse(proto_type_t::shadeCommand, R"({"s":3,"t":101})"));
  TEST_ASSERT_EQUAL_STRING("Not a command", parse(proto_type_t::shadeCommand, R"({"s":3,"cm":"Upp"})"));
  TEST_ASSERT_EQUAL_STRING("Not a command", parse(proto_type_t::shadeCommand, R"({"s":3,"cm":2})"));
  TEST_ASSERT_EQUAL_STRING("repeat must be 0-127", parse(proto_type_t::shadeCommand, R"({"s":3,"cm":"Up","re":-1})"));
  TEST_ASSERT_EQUAL_STRING("stepSize must be 0-127", parse(proto_type_t::shadeCommand, R"({"s":3,"cm":"Up","z":128})"));
}
static void test_only_the_first_problem_is_reported() {
  TEST_ASSERT_EQUAL_STRING("shadeId is missing", parse(proto_type_t::shadeCommand, R"({"t":101,"re":-1})"));
}
static void test_a_left_out_payload_is_missing_everything() {
  doc.clear();
  TEST_ASSERT_EQUAL_STRING("shadeId is missing", parseShadeOp(static_cast<uint8_t>(proto_type_t::unpairShade), JsonObjectConst(), req));
}
static void test_tilt_command() {
  TEST_ASSERT_GOOD(parse(proto_type_t::tiltCommand, R"({"s":2,"t":40})"));
  TEST_ASSERT_EQUAL(shade_request_type_t::tilt_command, req.type);
  TEST_ASSERT_EQUAL_INT8(40, req.target);
}
static void test_group_command_needs_a_command() {
  TEST_ASSERT_GOOD(parse(proto_type_t::groupCommand, R"({"g":1,"cm":"my","re":4})"));
  TEST_ASSERT_EQUAL(shade_request_type_t::group_command, req.type);
  TEST_ASSERT_EQUAL_UINT8(1, req.id);
  TEST_ASSERT_EQUAL(somfy_commands::My, req.command);
  TEST_ASSERT_EQUAL_INT8(4, req.repeat);
  TEST_ASSERT_EQUAL_STRING("command is missing", parse(proto_type_t::groupCommand, R"({"g":1,"t":10})"));
}
static void test_repeat_takes_a_shade_or_a_group() {
  TEST_ASSERT_GOOD(parse(proto_type_t::repeatCommand, R"({"s":5,"cm":"Prog"})"));
  TEST_ASSERT_EQUAL(shade_request_type_t::shade_repeat, req.type);
  TEST_ASSERT_EQUAL_UINT8(5, req.id);
  TEST_ASSERT_GOOD(parse(proto_type_t::repeatCommand, R"({"g":2,"cm":"Up"})"));
  TEST_ASSERT_EQUAL(shade_request_type_t::group_repeat, req.type);
  TEST_ASSERT_EQUAL_UINT8(2, req.id);
  TEST_ASSERT_EQUAL_STRING("Give shadeId or groupId", parse(proto_type_t::repeatCommand, R"({"s":5,"g":2,"cm":"Up"})"));
  TEST_ASSERT_EQUAL_STRING("Give shadeId or groupId", parse(proto_type_t::repeatCommand, R"({"cm":"Up"})"));
}
static void test_command_names_read_back_as_shade_commanded_reports_them() {
  somfy_commands cmd;
  TEST_ASSERT_TRUE(protoCommand("Step Down", cmd));
  TEST_ASSERT_EQUAL(somfy_commands::StepDown, cmd);
  TEST_ASSERT_TRUE(protoCommand("My+Up+Down", cmd));
  TEST_ASSERT_EQUAL(somfy_commands::MyUpDown, cmd);
  TEST_ASSERT_TRUE(protoCommand("SUNFLAG", cmd));
  TEST_ASSERT_EQUAL(somfy_commands::SunFlag, cmd);
  // The HTTP routes' guesses from the first letter are not names.
  TEST_ASSERT_FALSE(protoCommand("u", cmd));
  TEST_ASSERT_FALSE(protoCommand("", cmd));
  TEST_ASSERT_FALSE(protoCommand(nullptr, cmd));
}

/*********************************************************************
 * Positions, sensors, favorites, pairing
 ********************************************************************/
static void test_set_positions_leaves_out_what_was_not_given() {
  TEST_ASSERT_GOOD(parse(proto_type_t::setPositions, R"({"s":1,"p":0})"));
  TEST_ASSERT_EQUAL(shade_request_type_t::set_positions, req.type);
  TEST_ASSERT_EQUAL_INT8(0, req.position);
  TEST_ASSERT_EQUAL_INT8(-1, req.tiltPosition);
  TEST_ASSERT_GOOD(parse(proto_type_t::setPositions, R"({"s":1,"q":30})"));
  TEST_ASSERT_EQUAL_INT8(-1, req.position);
  TEST_ASSERT_EQUAL_INT8(30, req.tiltPosition);
  TEST_ASSERT_EQUAL_STRING("Give position, tiltPosition or both", parse(proto_type_t::setPositions, R"({"s":1})"));
  TEST_ASSERT_EQUAL_STRING("position must be 0-100", parse(proto_type_t::setPositions, R"({"s":1,"p":-1})"));
}
static void test_sensor_leaves_out_what_was_not_given() {
  TEST_ASSERT_GOOD(parse(proto_type_t::setSensor, R"({"g":3,"sn":1})"));
  TEST_ASSERT_EQUAL(shade_request_type_t::group_sensor, req.type);
  TEST_ASSERT_EQUAL_INT8(1, req.sunny);
  TEST_ASSERT_EQUAL_INT8(-1, req.windy);
  TEST_ASSERT_GOOD(parse(proto_type_t::setSensor, R"({"s":3,"wn":0,"re":2})"));
  TEST_ASSERT_EQUAL(shade_request_type_t::shade_sensor, req.type);
  TEST_ASSERT_EQUAL_INT8(-1, req.sunny);
  TEST_ASSERT_EQUAL_INT8(0, req.windy);
  TEST_ASSERT_EQUAL_INT8(2, req.repeat);
  TEST_ASSERT_EQUAL_STRING("Give sunny, windy or both", parse(proto_type_t::setSensor, R"({"s":3})"));
  // Booleans are 1 and 0.
  TEST_ASSERT_EQUAL_STRING("sunny must be 0-1", parse(proto_type_t::setSensor, R"({"s":3,"sn":true})"));
}
static void test_favorite() {
  TEST_ASSERT_GOOD(parse(proto_type_t::setMyPosition, R"({"s":4,"m":50,"M":20})"));
  TEST_ASSERT_EQUAL(shade_request_type_t::set_favorite, req.type);
  TEST_ASSERT_EQUAL_INT8(50, req.position);
  TEST_ASSERT_EQUAL_INT8(20, req.tiltPosition);
  TEST_ASSERT_GOOD(parse(proto_type_t::setMyPosition, R"({"s":4,"M":20})"));
  TEST_ASSERT_EQUAL_INT8(-1, req.position);
  TEST_ASSERT_EQUAL_STRING("Give myPos, myTiltPos or both", parse(proto_type_t::setMyPosition, R"({"s":4})"));
}
static void test_unpair_and_rolling_code() {
  TEST_ASSERT_GOOD(parse(proto_type_t::unpairShade, R"({"s":6})"));
  TEST_ASSERT_EQUAL(shade_request_type_t::unpair, req.type);
  TEST_ASSERT_EQUAL_UINT8(6, req.id);
  TEST_ASSERT_GOOD(parse(proto_type_t::setRollingCode, R"({"s":6,"c":0})"));
  TEST_ASSERT_EQUAL(shade_request_type_t::set_rolling_code, req.type);
  TEST_ASSERT_EQUAL_UINT16(0, req.rollingCode);
  TEST_ASSERT_EQUAL_STRING("lastRollingCode is missing", parse(proto_type_t::setRollingCode, R"({"s":6})"));
  TEST_ASSERT_EQUAL_STRING("lastRollingCode must be 0-65535", parse(proto_type_t::setRollingCode, R"({"s":6,"c":65536})"));
}
static void test_a_type_that_is_not_a_shade_op() {
  TEST_ASSERT_EQUAL_STRING("Not a shade op", parse(proto_type_t::sendRemoteCommand, R"({})"));
}

/*********************************************************************
 * A frame as a remote
 ********************************************************************/
static void test_remote_command() {
  doc.clear();
  deserializeJson(doc, R"({"a":1193046,"cm":"Up","c":42,"ek":160,"rp":3})");
  somfy_frame_t frame;
  uint8_t repeats = 9;
  TEST_ASSERT_GOOD(parseRemoteCommandOp(doc.as<JsonObjectConst>(), frame, repeats));
  TEST_ASSERT_EQUAL_UINT32(0x123456, frame.remoteAddress);
  TEST_ASSERT_EQUAL(somfy_commands::Up, frame.cmd);
  TEST_ASSERT_EQUAL_UINT16(42, frame.rollingCode);
  TEST_ASSERT_EQUAL_UINT8(160, frame.encKey);
  TEST_ASSERT_EQUAL_UINT8(3, repeats);
}
static void test_remote_command_defaults() {
  doc.clear();
  deserializeJson(doc, R"({"a":1,"cm":"My","c":1})");
  somfy_frame_t frame;
  uint8_t repeats = 9;
  TEST_ASSERT_GOOD(parseRemoteCommandOp(doc.as<JsonObjectConst>(), frame, repeats));
  TEST_ASSERT_EQUAL_UINT8(0xA7, frame.encKey);
  TEST_ASSERT_EQUAL_UINT8(0, repeats);
}
static void test_remote_command_needs_an_address_and_a_rolling_code() {
  somfy_frame_t frame;
  uint8_t repeats;
  doc.clear();
  deserializeJson(doc, R"({"cm":"Up","c":1})");
  TEST_ASSERT_EQUAL_STRING("remoteAddress is missing", parseRemoteCommandOp(doc.as<JsonObjectConst>(), frame, repeats));
  doc.clear();
  deserializeJson(doc, R"({"a":0,"cm":"Up","c":1})");
  TEST_ASSERT_EQUAL_STRING("remoteAddress must be 1-16777215", parseRemoteCommandOp(doc.as<JsonObjectConst>(), frame, repeats));
  doc.clear();
  deserializeJson(doc, R"({"a":5,"cm":"Up","c":0})");
  TEST_ASSERT_EQUAL_STRING("lastRollingCode must be 1-65535", parseRemoteCommandOp(doc.as<JsonObjectConst>(), frame, repeats));
}

/*********************************************************************
 * Replies once loop() has run a request
 ********************************************************************/
static void test_request_results_become_replies() {
  shade_request_t shade;
  shade_request_t group;
  group.type = shade_request_type_t::group_sensor;
  TEST_ASSERT_EQUAL(proto_error_t::none, protoRequestError(shade_request_result_t::done));
  TEST_ASSERT_NULL(protoRequestErrorDesc(shade, shade_request_result_t::done));
  TEST_ASSERT_EQUAL(proto_error_t::notFound, protoRequestError(shade_request_result_t::not_found));
  TEST_ASSERT_EQUAL_STRING("No shade with that id", protoRequestErrorDesc(shade, shade_request_result_t::not_found));
  TEST_ASSERT_EQUAL_STRING("No group with that id", protoRequestErrorDesc(group, shade_request_result_t::not_found));
  TEST_ASSERT_EQUAL(proto_error_t::radioUnavailable, protoRequestError(shade_request_result_t::radio_refused));
  TEST_ASSERT_EQUAL_STRING("The radio didn't take the command", protoRequestErrorDesc(shade, shade_request_result_t::radio_refused));
  // A favorite waits for the shade to stop: the client may try again.
  TEST_ASSERT_EQUAL(proto_error_t::busy, protoRequestError(shade_request_result_t::shade_moving));
  TEST_ASSERT_EQUAL_STRING("The shade is moving", protoRequestErrorDesc(shade, shade_request_result_t::shade_moving));
  TEST_ASSERT_EQUAL(proto_error_t::badRequest, protoRequestError(shade_request_result_t::no_tilt));
  TEST_ASSERT_EQUAL_STRING("The shade has no tilt", protoRequestErrorDesc(shade, shade_request_result_t::no_tilt));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_every_command_op_but_the_remote_frame_is_a_shade_request);
  RUN_TEST(test_shade_command_by_name);
  RUN_TEST(test_shade_command_to_a_target);
  RUN_TEST(test_shade_command_with_repeat_and_step);
  RUN_TEST(test_shade_command_needs_a_command_or_a_target_not_both);
  RUN_TEST(test_shade_command_values_are_checked);
  RUN_TEST(test_only_the_first_problem_is_reported);
  RUN_TEST(test_a_left_out_payload_is_missing_everything);
  RUN_TEST(test_tilt_command);
  RUN_TEST(test_group_command_needs_a_command);
  RUN_TEST(test_repeat_takes_a_shade_or_a_group);
  RUN_TEST(test_command_names_read_back_as_shade_commanded_reports_them);
  RUN_TEST(test_set_positions_leaves_out_what_was_not_given);
  RUN_TEST(test_sensor_leaves_out_what_was_not_given);
  RUN_TEST(test_favorite);
  RUN_TEST(test_unpair_and_rolling_code);
  RUN_TEST(test_a_type_that_is_not_a_shade_op);
  RUN_TEST(test_remote_command);
  RUN_TEST(test_remote_command_defaults);
  RUN_TEST(test_remote_command_needs_an_address_and_a_rolling_code);
  RUN_TEST(test_request_results_become_replies);
  return UNITY_END();
}
