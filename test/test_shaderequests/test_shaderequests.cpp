#include <unity.h>
#include <map>
#include <string>
#include "ShadeRequests.h"

// Records every call as a line so a test reads as "this request, these calls".
class FakeShadeOps : public ShadeOps {
  public:
    std::map<uint8_t, shade_info_t> shades;
    std::map<uint8_t, uint8_t> groups;      // group id -> its repeats
    bool radioTakes = true;                 // false: the radio refuses everything
    bool lastCommand = false;               // what isLastCommand answers
    std::string calls;

    bool findShade(uint8_t id, shade_info_t &info) override {
      auto it = shades.find(id);
      if(it == shades.end()) return false;
      info = it->second;
      return true;
    }
    bool findGroup(uint8_t id, uint8_t &repeats) override {
      auto it = groups.find(id);
      if(it == groups.end()) return false;
      repeats = it->second;
      return true;
    }
    bool moveToTarget(uint8_t id, float pos) override { return log("moveToTarget %u %g", id, pos); }
    bool moveToTiltTarget(uint8_t id, float tilt) override { return log("moveToTiltTarget %u %g", id, tilt); }
    bool sendCommand(uint8_t id, somfy_commands cmd, uint8_t repeat, uint8_t stepSize) override {
      return log("sendCommand %u %s repeat=%u step=%u", id, name(cmd), repeat, stepSize);
    }
    bool sendTiltCommand(uint8_t id, somfy_commands cmd) override { return log("sendTiltCommand %u %s", id, name(cmd)); }
    bool isLastCommand(uint8_t, somfy_commands) override { return lastCommand; }
    bool repeatFrame(uint8_t id, uint8_t repeat) override { return log("repeatFrame %u repeat=%u", id, repeat); }
    bool sendSensorCommand(uint8_t id, int8_t windy, int8_t sunny, uint8_t repeat) override {
      return log("sendSensorCommand %u windy=%d sunny=%d repeat=%u", id, windy, sunny, repeat);
    }
    bool setMyPosition(uint8_t id, int8_t pos, int8_t tilt) override { return log("setMyPosition %u %d %d", id, pos, tilt); }
    void setPositions(uint8_t id, float pos, float tilt) override { log("setPositions %u %g %g", id, pos, tilt); }
    void setUnpaired(uint8_t id) override { log("setUnpaired %u", id); }
    void setRollingCode(uint8_t id, uint16_t code) override { log("setRollingCode %u %u", id, code); }
    bool sendGroupCommand(uint8_t id, somfy_commands cmd, uint8_t repeat, uint8_t stepSize) override {
      return log("sendGroupCommand %u %s repeat=%u step=%u", id, name(cmd), repeat, stepSize);
    }
    bool isLastGroupCommand(uint8_t, somfy_commands) override { return lastCommand; }
    bool repeatGroupFrame(uint8_t id, uint8_t repeat) override { return log("repeatGroupFrame %u repeat=%u", id, repeat); }
    bool sendGroupSensorCommand(uint8_t id, int8_t windy, int8_t sunny, uint8_t repeat) override {
      return log("sendGroupSensorCommand %u windy=%d sunny=%d repeat=%u", id, windy, sunny, repeat);
    }
  private:
    static const char *name(somfy_commands cmd) {
      static std::string s;
      s = translateSomfyCommand(cmd).c_str();
      return s.c_str();
    }
    template<typename... Args>
    bool log(const char *fmt, Args... args) {
      char line[128];
      snprintf(line, sizeof(line), fmt, args...);
      if(!calls.empty()) calls += "; ";
      calls += line;
      return radioTakes;
    }
};

static FakeShadeOps ops;
static ShadeRequests requests(ops);

static shade_info_t rollerShade() {
  shade_info_t s;
  s.repeats = 2;
  return s;
}
static shade_info_t flippedShade() {
  shade_info_t s = rollerShade();
  s.flipPosition = true;
  return s;
}
static shade_info_t tiltShade(tilt_types tilt) {
  shade_info_t s = rollerShade();
  s.tiltType = tilt;
  return s;
}
static shade_request_t request(shade_request_type_t type, uint8_t id = 4) {
  shade_request_t req;
  req.type = type;
  req.id = id;
  return req;
}

void setUp() {
  ops.shades.clear();
  ops.groups.clear();
  ops.radioTakes = true;
  ops.lastCommand = false;
  ops.calls.clear();
}
void tearDown() {}

/*********************************************************************
 * Finding the shade or group
 ********************************************************************/
static void test_a_missing_shade_is_not_found() {
  shade_request_t req = request(shade_request_type_t::shade_command);
  req.target = 30;
  TEST_ASSERT_EQUAL(shade_request_result_t::not_found, requests.run(req));
  TEST_ASSERT_EQUAL_STRING("", ops.calls.c_str());
}
static void test_a_missing_group_is_not_found() {
  TEST_ASSERT_EQUAL(shade_request_result_t::not_found, requests.run(request(shade_request_type_t::group_command, 2)));
  TEST_ASSERT_EQUAL_STRING("", ops.calls.c_str());
}
static void test_a_shade_id_is_not_a_group_id() {
  ops.shades[2] = rollerShade();
  TEST_ASSERT_EQUAL(shade_request_result_t::not_found, requests.run(request(shade_request_type_t::group_command, 2)));
  TEST_ASSERT_EQUAL_STRING("", ops.calls.c_str());
}

/*********************************************************************
 * Positions as the client sees them
 ********************************************************************/
static void test_target_moves_a_shade_there() {
  ops.shades[4] = rollerShade();
  shade_request_t req = request(shade_request_type_t::shade_command);
  req.target = 30;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("moveToTarget 4 30", ops.calls.c_str());
}
static void test_target_on_a_flipped_shade_is_turned_round() {
  ops.shades[4] = flippedShade();
  shade_request_t req = request(shade_request_type_t::shade_command);
  req.target = 30;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("moveToTarget 4 70", ops.calls.c_str());
}
static void test_tilt_target_on_a_flipped_shade_is_turned_round() {
  // MQTT used to send this one straight through.
  ops.shades[4] = flippedShade();
  shade_request_t req = request(shade_request_type_t::tilt_command);
  req.target = 30;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("moveToTiltTarget 4 70", ops.calls.c_str());
}
static void test_without_a_target_the_command_is_sent() {
  ops.shades[4] = rollerShade();
  shade_request_t req = request(shade_request_type_t::shade_command);
  req.command = somfy_commands::Down;
  req.stepSize = 3;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("sendCommand 4 Down repeat=2 step=3", ops.calls.c_str());
}
static void test_tilt_without_a_target_sends_the_tilt_command() {
  ops.shades[4] = rollerShade();
  shade_request_t req = request(shade_request_type_t::tilt_command);
  req.command = somfy_commands::Up;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("sendTiltCommand 4 Up", ops.calls.c_str());
}
static void test_set_positions_on_a_flipped_shade_are_turned_round() {
  // /setPositions used to send these straight through.
  ops.shades[4] = flippedShade();
  ops.shades[4].tiltType = tilt_types::integrated;
  shade_request_t req = request(shade_request_type_t::set_positions);
  req.position = 20;
  req.tiltPosition = 90;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("setPositions 4 80 10", ops.calls.c_str());
}
static void test_set_positions_leaves_the_tilt_of_a_shade_without_one() {
  ops.shades[4] = rollerShade();
  shade_request_t req = request(shade_request_type_t::set_positions);
  req.position = 20;
  req.tiltPosition = 90;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("setPositions 4 20 -1", ops.calls.c_str());
}
static void test_set_positions_tilt_alone_on_a_shade_without_one_is_refused() {
  ops.shades[4] = rollerShade();
  shade_request_t req = request(shade_request_type_t::set_positions);
  req.tiltPosition = 40;
  TEST_ASSERT_EQUAL(shade_request_result_t::no_tilt, requests.run(req));
  TEST_ASSERT_EQUAL_STRING("", ops.calls.c_str());
}
static void test_set_positions_with_nothing_in_range_does_nothing() {
  ops.shades[4] = tiltShade(tilt_types::integrated);
  shade_request_t req = request(shade_request_type_t::set_positions);
  req.position = -1;
  req.tiltPosition = 101;
  TEST_ASSERT_EQUAL(shade_request_result_t::done, requests.run(req));
  TEST_ASSERT_EQUAL_STRING("", ops.calls.c_str());
}
static void test_client_values_are_checked_on_the_way_in() {
  TEST_ASSERT_EQUAL_INT8(0, requestTarget(0));
  TEST_ASSERT_EQUAL_INT8(100, requestTarget(100));
  TEST_ASSERT_EQUAL_INT8(-1, requestTarget(101));
  TEST_ASSERT_EQUAL_INT8(-1, requestTarget(255));
  TEST_ASSERT_EQUAL_INT8(-1, requestRepeat(-5));
  TEST_ASSERT_EQUAL_INT8(0, requestRepeat(0));
  TEST_ASSERT_EQUAL_INT8(127, requestRepeat(200));
}

/*********************************************************************
 * Repeats
 ********************************************************************/
static void test_no_repeat_means_the_shades_own() {
  ops.shades[4] = rollerShade();
  shade_request_t req = request(shade_request_type_t::shade_command);
  req.command = somfy_commands::Up;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("sendCommand 4 Up repeat=2 step=0", ops.calls.c_str());
}
static void test_repeat_zero_means_no_repeats_for_a_shade_too() {
  // It used to mean "the shade's own" for /shadeCommand but not for groups.
  ops.shades[4] = rollerShade();
  ops.groups[2] = 3;
  shade_request_t req = request(shade_request_type_t::shade_command);
  req.command = somfy_commands::Up;
  req.repeat = 0;
  requests.run(req);
  shade_request_t group = request(shade_request_type_t::group_command, 2);
  group.command = somfy_commands::Up;
  group.repeat = 0;
  requests.run(group);
  TEST_ASSERT_EQUAL_STRING("sendCommand 4 Up repeat=0 step=0; sendGroupCommand 2 Up repeat=0 step=0", ops.calls.c_str());
}
static void test_no_repeat_means_the_groups_own() {
  ops.groups[2] = 3;
  shade_request_t req = request(shade_request_type_t::group_command, 2);
  req.command = somfy_commands::Down;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("sendGroupCommand 2 Down repeat=3 step=0", ops.calls.c_str());
}

/*********************************************************************
 * Repeating the last command
 ********************************************************************/
static void test_repeat_sends_a_new_command_when_it_was_not_the_last() {
  ops.shades[4] = rollerShade();
  shade_request_t req = request(shade_request_type_t::shade_repeat);
  req.command = somfy_commands::Prog;
  req.repeat = 10;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("sendCommand 4 Prog repeat=10 step=0", ops.calls.c_str());
}
static void test_repeat_repeats_the_last_frame() {
  ops.shades[4] = rollerShade();
  ops.lastCommand = true;
  shade_request_t req = request(shade_request_type_t::shade_repeat);
  req.command = somfy_commands::Prog;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("repeatFrame 4 repeat=2", ops.calls.c_str());
}
static void test_prog_on_a_one_button_garage_door_is_toggle() {
  ops.shades[4] = rollerShade();
  ops.shades[4].shadeType = shade_types::garage1;
  shade_request_t req = request(shade_request_type_t::shade_repeat);
  req.command = somfy_commands::Prog;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("sendCommand 4 Toggle repeat=2 step=0", ops.calls.c_str());
}
static void test_group_repeat_repeats_the_last_frame() {
  ops.groups[2] = 3;
  ops.lastCommand = true;
  requests.run(request(shade_request_type_t::group_repeat, 2));
  TEST_ASSERT_EQUAL_STRING("repeatGroupFrame 2 repeat=3", ops.calls.c_str());
}

/*********************************************************************
 * Sensor frames
 ********************************************************************/
static void test_sensor_sends_what_was_given() {
  ops.shades[4] = rollerShade();
  shade_request_t req = request(shade_request_type_t::shade_sensor);
  req.sunny = 1;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("sendSensorCommand 4 windy=-1 sunny=1 repeat=2", ops.calls.c_str());
}
static void test_group_sensor_uses_the_groups_repeats() {
  ops.groups[2] = 3;
  shade_request_t req = request(shade_request_type_t::group_sensor, 2);
  req.windy = 0;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("sendGroupSensorCommand 2 windy=0 sunny=-1 repeat=3", ops.calls.c_str());
}

/*********************************************************************
 * The favorite
 ********************************************************************/
static void test_favorite_on_a_moving_shade_is_refused() {
  ops.shades[4] = rollerShade();
  ops.shades[4].idle = false;
  shade_request_t req = request(shade_request_type_t::set_favorite);
  req.position = 50;
  TEST_ASSERT_EQUAL(shade_request_result_t::shade_moving, requests.run(req));
  TEST_ASSERT_EQUAL_STRING("", ops.calls.c_str());
}
static void test_group_requests_take_a_group_id() {
  TEST_ASSERT_TRUE(shadeRequestIsForGroup(shade_request_type_t::group_command));
  TEST_ASSERT_TRUE(shadeRequestIsForGroup(shade_request_type_t::group_repeat));
  TEST_ASSERT_TRUE(shadeRequestIsForGroup(shade_request_type_t::group_sensor));
  TEST_ASSERT_FALSE(shadeRequestIsForGroup(shade_request_type_t::shade_sensor));
  TEST_ASSERT_FALSE(shadeRequestIsForGroup(shade_request_type_t::unpair));
}
static void test_favorite_on_a_flipped_shade_is_turned_round() {
  ops.shades[4] = flippedShade();
  shade_request_t req = request(shade_request_type_t::set_favorite);
  req.position = 30;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("setMyPosition 4 70 -1", ops.calls.c_str());
}
static void test_favorite_without_a_tilt_keeps_the_favorite_tilt() {
  // MQTT mypos used to set the favorite tilt to 0 here.
  ops.shades[4] = tiltShade(tilt_types::integrated);
  ops.shades[4].myTiltPos = 40.0f;
  shade_request_t req = request(shade_request_type_t::set_favorite);
  req.position = 30;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("setMyPosition 4 30 40", ops.calls.c_str());
}
static void test_favorite_tilt_alone_keeps_the_favorite_lift() {
  ops.shades[4] = tiltShade(tilt_types::integrated);
  ops.shades[4].myPos = 60.0f;
  ops.shades[4].currentPos = 20.0f;
  shade_request_t req = request(shade_request_type_t::set_favorite);
  req.tiltPosition = 25;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("setMyPosition 4 60 25", ops.calls.c_str());
}
static void test_favorite_tilt_alone_without_a_favorite_uses_where_the_shade_is() {
  ops.shades[4] = tiltShade(tilt_types::integrated);
  ops.shades[4].currentPos = 20.0f;
  shade_request_t req = request(shade_request_type_t::set_favorite);
  req.tiltPosition = 25;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("setMyPosition 4 20 25", ops.calls.c_str());
}
static void test_favorite_tilt_on_a_flipped_shade_is_turned_round() {
  ops.shades[4] = flippedShade();
  ops.shades[4].tiltType = tilt_types::integrated;
  shade_request_t req = request(shade_request_type_t::set_favorite);
  req.position = 30;
  req.tiltPosition = 10;
  requests.run(req);
  TEST_ASSERT_EQUAL_STRING("setMyPosition 4 70 90", ops.calls.c_str());
}

/*********************************************************************
 * Pairing and rolling codes
 ********************************************************************/
static void test_unpair_holds_prog_longer_on_a_56_bit_motor() {
  ops.shades[4] = rollerShade();
  ops.shades[5] = rollerShade();
  ops.shades[5].bitLength = 80;
  requests.run(request(shade_request_type_t::unpair, 4));
  requests.run(request(shade_request_type_t::unpair, 5));
  TEST_ASSERT_EQUAL_STRING(
    "sendCommand 4 Prog repeat=7 step=0; setUnpaired 4; sendCommand 5 Prog repeat=1 step=0; setUnpaired 5",
    ops.calls.c_str());
}
static void test_unpair_the_radio_refused_leaves_the_shade_paired() {
  ops.shades[4] = rollerShade();
  ops.radioTakes = false;
  TEST_ASSERT_EQUAL(shade_request_result_t::radio_refused, requests.run(request(shade_request_type_t::unpair)));
  TEST_ASSERT_EQUAL_STRING("sendCommand 4 Prog repeat=7 step=0", ops.calls.c_str());
}
static void test_rolling_code_is_set() {
  ops.shades[4] = rollerShade();
  shade_request_t req = request(shade_request_type_t::set_rolling_code);
  req.rollingCode = 1234;
  TEST_ASSERT_EQUAL(shade_request_result_t::done, requests.run(req));
  TEST_ASSERT_EQUAL_STRING("setRollingCode 4 1234", ops.calls.c_str());
}

/*********************************************************************
 * The radio refusing
 ********************************************************************/
static void test_a_refused_command_is_reported() {
  ops.shades[4] = rollerShade();
  ops.radioTakes = false;
  shade_request_t req = request(shade_request_type_t::shade_command);
  req.target = 50;
  TEST_ASSERT_EQUAL(shade_request_result_t::radio_refused, requests.run(req));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_missing_shade_is_not_found);
  RUN_TEST(test_a_missing_group_is_not_found);
  RUN_TEST(test_a_shade_id_is_not_a_group_id);
  RUN_TEST(test_target_moves_a_shade_there);
  RUN_TEST(test_target_on_a_flipped_shade_is_turned_round);
  RUN_TEST(test_tilt_target_on_a_flipped_shade_is_turned_round);
  RUN_TEST(test_without_a_target_the_command_is_sent);
  RUN_TEST(test_tilt_without_a_target_sends_the_tilt_command);
  RUN_TEST(test_set_positions_on_a_flipped_shade_are_turned_round);
  RUN_TEST(test_set_positions_leaves_the_tilt_of_a_shade_without_one);
  RUN_TEST(test_set_positions_tilt_alone_on_a_shade_without_one_is_refused);
  RUN_TEST(test_set_positions_with_nothing_in_range_does_nothing);
  RUN_TEST(test_client_values_are_checked_on_the_way_in);
  RUN_TEST(test_no_repeat_means_the_shades_own);
  RUN_TEST(test_repeat_zero_means_no_repeats_for_a_shade_too);
  RUN_TEST(test_no_repeat_means_the_groups_own);
  RUN_TEST(test_repeat_sends_a_new_command_when_it_was_not_the_last);
  RUN_TEST(test_repeat_repeats_the_last_frame);
  RUN_TEST(test_prog_on_a_one_button_garage_door_is_toggle);
  RUN_TEST(test_group_repeat_repeats_the_last_frame);
  RUN_TEST(test_sensor_sends_what_was_given);
  RUN_TEST(test_group_sensor_uses_the_groups_repeats);
  RUN_TEST(test_favorite_on_a_moving_shade_is_refused);
  RUN_TEST(test_group_requests_take_a_group_id);
  RUN_TEST(test_favorite_on_a_flipped_shade_is_turned_round);
  RUN_TEST(test_favorite_without_a_tilt_keeps_the_favorite_tilt);
  RUN_TEST(test_favorite_tilt_alone_keeps_the_favorite_lift);
  RUN_TEST(test_favorite_tilt_alone_without_a_favorite_uses_where_the_shade_is);
  RUN_TEST(test_favorite_tilt_on_a_flipped_shade_is_turned_round);
  RUN_TEST(test_unpair_holds_prog_longer_on_a_56_bit_motor);
  RUN_TEST(test_unpair_the_radio_refused_leaves_the_shade_paired);
  RUN_TEST(test_rolling_code_is_set);
  RUN_TEST(test_a_refused_command_is_reported);
  return UNITY_END();
}
