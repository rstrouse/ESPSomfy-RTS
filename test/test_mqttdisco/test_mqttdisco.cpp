#include <unity.h>
#include <string>
#include <vector>
#include "MqttDisco.h"
#include "MqttTopics.h"
#include "ShadeChanges.h"

void setUp() {}
void tearDown() {}

static shade_disco_t shade(shade_types type = shade_types::roller) {
  shade_disco_t s;
  s.shadeId = 3;
  s.name = "Kitchen";
  s.shadeType = type;
  s.rootTopic = "somfy";
  s.discoTopic = "homeassistant";
  s.serverId = "abc123";
  s.hostname = "ESPSomfyRTS";
  s.fwVersion = "v4.0.0";
  return s;
}
// The config Home Assistant gets and the topic it goes to.
struct disco_t {
  JsonDocument doc;
  char topic[128] = "";
};
static void build(const shade_disco_t &s, disco_t &d) {
  shadeDisco(s, d.doc.to<JsonObject>(), d.topic, sizeof(d.topic));
}

static void test_a_roller_is_a_cover_under_the_discovery_topic() {
  disco_t d;
  build(shade(), d);
  TEST_ASSERT_EQUAL_STRING("homeassistant/cover/3/config", d.topic);
  TEST_ASSERT_EQUAL_STRING("somfy/shades/3", d.doc["~"]);
  TEST_ASSERT_EQUAL_STRING("Kitchen", d.doc["name"]);
  TEST_ASSERT_EQUAL_STRING("mqtt_abc123_shade3", d.doc["unique_id"]);
  TEST_ASSERT_EQUAL_STRING("shade", d.doc["device_class"]);
  TEST_ASSERT_TRUE(d.doc["enabled_by_default"].as<bool>());
}
// Every shade of a device is grouped under it in Home Assistant.
static void test_the_device_is_the_esp() {
  disco_t d;
  build(shade(), d);
  JsonObject device = d.doc["device"];
  TEST_ASSERT_EQUAL_STRING("mqtt_espsomfyrts_abc123", device["identifiers"][0]);
  TEST_ASSERT_EQUAL_STRING("ESPSomfyRTS", device["name"]);
  TEST_ASSERT_EQUAL_STRING("v4.0.0", device["hw_version"]);
  TEST_ASSERT_EQUAL_STRING("ESPSomfy-RTS MQTT", device["model"]);
  TEST_ASSERT_EQUAL_STRING("cjkas", device["mf"]);
  TEST_ASSERT_EQUAL_STRING("somfy/status", d.doc["availability_topic"]);
  TEST_ASSERT_EQUAL_STRING("online", d.doc["payload_available"]);
  TEST_ASSERT_EQUAL_STRING("offline", d.doc["payload_not_available"]);
}
// The topics under ~ are the ones MqttCommands listens on and the shade
// publishes.
static void test_a_cover_moves_through_the_shade_topics() {
  disco_t d;
  build(shade(), d);
  TEST_ASSERT_EQUAL_STRING("~/direction/set", d.doc["command_topic"]);
  TEST_ASSERT_EQUAL_STRING("~/target/set", d.doc["set_position_topic"]);
  TEST_ASSERT_EQUAL_STRING("~/position", d.doc["position_topic"]);
  TEST_ASSERT_EQUAL_STRING("~/direction", d.doc["state_topic"]);
  TEST_ASSERT_EQUAL_STRING("0", d.doc["payload_stop"]);
  TEST_ASSERT_EQUAL_STRING("0", d.doc["state_stopped"]);
  TEST_ASSERT_FALSE(d.doc["tilt_command_topic"].is<const char *>());
}
// 0 is open: an upward move opens.
static void test_a_roller_opens_up() {
  disco_t d;
  build(shade(), d);
  TEST_ASSERT_EQUAL_STRING("-1", d.doc["payload_open"]);
  TEST_ASSERT_EQUAL_STRING("1", d.doc["payload_close"]);
  TEST_ASSERT_EQUAL(0, d.doc["position_open"].as<int>());
  TEST_ASSERT_EQUAL(100, d.doc["position_closed"].as<int>());
  TEST_ASSERT_EQUAL_STRING("-1", d.doc["state_opening"]);
  TEST_ASSERT_EQUAL_STRING("1", d.doc["state_closing"]);
}
static void test_a_flipped_position_opens_at_100() {
  shade_disco_t s = shade();
  s.flipPosition = true;
  disco_t d;
  build(s, d);
  TEST_ASSERT_EQUAL_STRING("1", d.doc["payload_open"]);
  TEST_ASSERT_EQUAL_STRING("-1", d.doc["payload_close"]);
  TEST_ASSERT_EQUAL(100, d.doc["position_open"].as<int>());
  TEST_ASSERT_EQUAL(0, d.doc["position_closed"].as<int>());
  TEST_ASSERT_EQUAL_STRING("1", d.doc["state_opening"]);
  TEST_ASSERT_EQUAL_STRING("-1", d.doc["state_closing"]);
}
// An awning opens downward, and flipped opens up again.
static void test_an_awning_opens_down() {
  disco_t d;
  build(shade(shade_types::awning), d);
  TEST_ASSERT_EQUAL_STRING("awning", d.doc["device_class"]);
  TEST_ASSERT_EQUAL_STRING("1", d.doc["payload_open"]);
  TEST_ASSERT_EQUAL(100, d.doc["position_open"].as<int>());
  shade_disco_t s = shade(shade_types::awning);
  s.flipPosition = true;
  disco_t flipped;
  build(s, flipped);
  TEST_ASSERT_EQUAL_STRING("-1", flipped.doc["payload_open"]);
  TEST_ASSERT_EQUAL(0, flipped.doc["position_open"].as<int>());
}
static void test_each_kind_of_shade_has_its_device_class() {
  const struct { shade_types type; const char *deviceClass; } kinds[] = {
    {shade_types::roller, "shade"},
    {shade_types::blind, "blind"},
    {shade_types::ldrapery, "curtain"},
    {shade_types::rdrapery, "curtain"},
    {shade_types::cdrapery, "curtain"},
    {shade_types::lgate, "curtain"},
    {shade_types::cgate1, "curtain"},
    {shade_types::garage1, "garage"},
    {shade_types::garage3, "garage"},
    {shade_types::shutter, "shutter"},
    {shade_types::awning, "awning"},
  };
  for(const auto &kind : kinds) {
    disco_t d;
    build(shade(kind.type), d);
    TEST_ASSERT_EQUAL_STRING(kind.deviceClass, d.doc["device_class"]);
  }
}
static void test_a_tilting_blind_has_the_tilt_topics() {
  shade_disco_t s = shade(shade_types::blind);
  s.tiltType = tilt_types::tiltmotor;
  disco_t d;
  build(s, d);
  TEST_ASSERT_EQUAL_STRING("~/tiltTarget/set", d.doc["tilt_command_topic"]);
  TEST_ASSERT_EQUAL_STRING("~/tiltPosition", d.doc["tilt_status_topic"]);
  TEST_ASSERT_EQUAL_STRING("~/direction/set", d.doc["command_topic"]);
}
// A tilt only blind doesn't open or close, so it has only the tilt.
static void test_a_tilt_only_blind_has_only_the_tilt() {
  shade_disco_t s = shade(shade_types::blind);
  s.tiltType = tilt_types::tiltonly;
  disco_t d;
  build(s, d);
  TEST_ASSERT_EQUAL_STRING("~/tiltTarget/set", d.doc["tilt_command_topic"]);
  TEST_ASSERT_FALSE(d.doc["command_topic"].is<const char *>());
  TEST_ASSERT_FALSE(d.doc["set_position_topic"].is<const char *>());
  // Sent as null so Home Assistant drops its defaults.
  std::string json;
  serializeJson(d.doc, json);
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"payload_open\":null"));
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"payload_close\":null"));
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"payload_stop\":null"));
}
// A dry contact is on (100) or off (0).
static void test_a_dry_contact_is_a_switch() {
  for(shade_types type : {shade_types::drycontact, shade_types::drycontact2}) {
    disco_t d;
    build(shade(type), d);
    TEST_ASSERT_EQUAL_STRING("homeassistant/switch/3/config", d.topic);
    TEST_ASSERT_FALSE(d.doc["device_class"].is<const char *>());
    TEST_ASSERT_EQUAL(100, d.doc["payload_on"].as<int>());
    TEST_ASSERT_EQUAL(0, d.doc["payload_off"].as<int>());
    TEST_ASSERT_EQUAL(100, d.doc["state_on"].as<int>());
    TEST_ASSERT_EQUAL(0, d.doc["state_off"].as<int>());
    TEST_ASSERT_EQUAL_STRING("~/position", d.doc["state_topic"]);
    TEST_ASSERT_EQUAL_STRING("~/target/set", d.doc["command_topic"]);
    TEST_ASSERT_FALSE(d.doc["payload_open"].is<const char *>());
  }
}
// Deleting a shade clears both, as its kind may have changed.
static void test_the_config_topic_of_each_kind() {
  char topic[64];
  shadeDiscoTopic("ha", 12, false, topic, sizeof(topic));
  TEST_ASSERT_EQUAL_STRING("ha/cover/12/config", topic);
  shadeDiscoTopic("ha", 12, true, topic, sizeof(topic));
  TEST_ASSERT_EQUAL_STRING("ha/switch/12/config", topic);
  TEST_ASSERT_TRUE(isDiscoSwitch(shade_types::drycontact2));
  TEST_ASSERT_FALSE(isDiscoSwitch(shade_types::garage1));
}

static bool hasTopic(const char *const *topics, size_t count, const std::string &topic) {
  for(size_t i = 0; i < count; i++) if(topic == topics[i]) return true;
  return false;
}
static void keepRetained(const char *topic, int32_t value, bool retain, void *ctx) {
  (void)value;
  if(retain) static_cast<std::vector<std::string> *>(ctx)->push_back(topic);
}
// A deleted shade leaves none of its topics at the broker.
static void test_deleting_a_shade_clears_every_state_topic_the_broker_keeps() {
  shade_snapshot_t shade{};
  std::vector<std::string> retained;
  publishShadeTopics(SHADE_CHANGED_TOPICS, shade, keepRetained, &retained);
  TEST_ASSERT_FALSE(retained.empty());
  for(const std::string &topic : retained)
    TEST_ASSERT_TRUE_MESSAGE(hasTopic(SHADE_TOPICS, SHADE_TOPIC_COUNT, topic), topic.c_str());
}
// Group unpublish named it SunSensor, so a deleted group's stayed.
static void test_deleting_a_group_clears_its_sun_sensor_topic() {
  TEST_ASSERT_TRUE(hasTopic(GROUP_TOPICS, GROUP_TOPIC_COUNT, "sunSensor"));
  TEST_ASSERT_FALSE(hasTopic(GROUP_TOPICS, GROUP_TOPIC_COUNT, "SunSensor"));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_a_roller_is_a_cover_under_the_discovery_topic);
  RUN_TEST(test_the_device_is_the_esp);
  RUN_TEST(test_a_cover_moves_through_the_shade_topics);
  RUN_TEST(test_a_roller_opens_up);
  RUN_TEST(test_a_flipped_position_opens_at_100);
  RUN_TEST(test_an_awning_opens_down);
  RUN_TEST(test_each_kind_of_shade_has_its_device_class);
  RUN_TEST(test_a_tilting_blind_has_the_tilt_topics);
  RUN_TEST(test_a_tilt_only_blind_has_only_the_tilt);
  RUN_TEST(test_a_dry_contact_is_a_switch);
  RUN_TEST(test_the_config_topic_of_each_kind);
  RUN_TEST(test_deleting_a_shade_clears_every_state_topic_the_broker_keeps);
  RUN_TEST(test_deleting_a_group_clears_its_sun_sensor_topic);
  return UNITY_END();
}
