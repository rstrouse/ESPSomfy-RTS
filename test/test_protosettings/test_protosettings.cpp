#include <unity.h>
#include <string>
#include <ArduinoJson.h>
#include "ProtoSettings.h"

// Socket protocol 2's settings ops: the sections, and the settings route's
// request that a save's payload becomes (docs/PROTOCOL.md).

void setUp() {}
void tearDown() {}

static JsonDocument payload;
static JsonDocument doc;
static settings_section_t section;

static void load(const char *json) {
  payload.clear();
  TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(payload, json).code());
}
static const char *expand(const char *json, bool hasEthernet = true) {
  load(json);
  return expandSettingsPayload(payload.as<JsonObjectConst>(), hasEthernet, section, doc);
}
static const char *sectionOf(const char *json) {
  load(json);
  return parseSettingsSection(payload.as<JsonObjectConst>(), section);
}
static std::string written() {
  std::string out;
  serializeJson(doc, out);
  return out;
}
static uint16_t parts() { return settingsParts(section, doc.as<JsonObjectConst>()); }
static uint16_t bit(settings_part_t part) { return 1u << static_cast<uint8_t>(part); }
#define TEST_ASSERT_GOOD(why) TEST_ASSERT_NULL_MESSAGE(why, why)

/*********************************************************************
 * Sections
 ********************************************************************/
static void test_every_section_by_its_name() {
  const settings_section_t all[] = {settings_section_t::general, settings_section_t::network, settings_section_t::mqtt,
    settings_section_t::security, settings_section_t::radio};
  for(settings_section_t s : all) {
    std::string json = std::string(R"({"se":")") + settingsSectionName(s) + "\"}";
    TEST_ASSERT_GOOD(sectionOf(json.c_str()));
    TEST_ASSERT_EQUAL(s, section);
  }
  TEST_ASSERT_EQUAL_STRING("general", settingsSectionName(settings_section_t::general));
  TEST_ASSERT_EQUAL_STRING("radio", settingsSectionName(settings_section_t::radio));
}
static void test_a_section_must_be_named() {
  TEST_ASSERT_EQUAL_STRING("section is missing", sectionOf(R"({})"));
  TEST_ASSERT_EQUAL_STRING("section is missing", sectionOf(R"({"se":2})"));
  TEST_ASSERT_EQUAL_STRING("Unknown section", sectionOf(R"({"se":"wifi"})"));
  // Names are matched as they are.
  TEST_ASSERT_EQUAL_STRING("Unknown section", sectionOf(R"({"se":"General"})"));
  TEST_ASSERT_EQUAL_STRING("section is missing", expand(R"({"hn":"x"})"));
}

/*********************************************************************
 * A save's payload as the route's request
 ********************************************************************/
static void test_short_keys_become_the_route_names() {
  TEST_ASSERT_GOOD(expand(R"({"se":"general","hn":"Somfy","ns":"pool.ntp.org"})"));
  TEST_ASSERT_EQUAL(settings_section_t::general, section);
  TEST_ASSERT_EQUAL_STRING(R"({"hostname":"Somfy","ntpServer":"pool.ntp.org"})", written().c_str());
}
static void test_fields_named_apart_from_the_dictionary() {
  // connType in the header is the connection in use; the setting is connMode.
  TEST_ASSERT_GOOD(expand(R"({"se":"network","cn":2})"));
  TEST_ASSERT_EQUAL_STRING(R"({"connType":2})", written().c_str());
  TEST_ASSERT_GOOD(expand(R"({"se":"security","sy":1,"pn":"1234"})"));
  TEST_ASSERT_EQUAL_STRING(R"({"type":1,"pin":"1234"})", written().c_str());
  TEST_ASSERT_GOOD(expand(R"({"se":"radio","bl":80})"));
  TEST_ASSERT_EQUAL_STRING(R"({"type":80})", written().c_str());
}
static void test_booleans_become_true_and_false() {
  TEST_ASSERT_GOOD(expand(R"({"se":"mqtt","en":1,"pd":0})"));
  TEST_ASSERT_EQUAL_STRING(R"({"enabled":true,"pubDisco":false})", written().c_str());
  TEST_ASSERT_EQUAL_STRING("enabled is not a boolean (1 or 0)", expand(R"({"se":"radio","en":true})"));
}
static void test_values_are_checked_against_their_kind() {
  TEST_ASSERT_EQUAL_STRING("port is not a whole number", expand(R"({"se":"mqtt","po":"1883"})"));
  TEST_ASSERT_EQUAL_STRING("hostname is not a string", expand(R"({"se":"general","hn":5})"));
  TEST_ASSERT_EQUAL_STRING("frequency is not a number", expand(R"({"se":"radio","fq":"433"})"));
}
static void test_a_number_may_be_whole() {
  TEST_ASSERT_GOOD(expand(R"({"se":"radio","fq":433,"bw":99.97})"));
  TEST_ASSERT_EQUAL_STRING(R"({"frequency":433,"rxBandwidth":99.97})", written().c_str());
}
static void test_whole_numbers_are_checked_against_what_the_setting_holds() {
  TEST_ASSERT_EQUAL_STRING("connMode must be 1-3", expand(R"({"se":"network","cn":4})"));
  TEST_ASSERT_EQUAL_STRING("connMode must be 1-3", expand(R"({"se":"network","cn":0})"));
  TEST_ASSERT_EQUAL_STRING("port must be 1-65535", expand(R"({"se":"mqtt","po":65536})"));
  TEST_ASSERT_EQUAL_STRING("txPower must be -30-12", expand(R"({"se":"radio","tp":13})"));
  TEST_ASSERT_EQUAL_STRING("TXPin must be 0-54", expand(R"({"se":"radio","tx":-1})"));
  TEST_ASSERT_EQUAL_STRING("securityType must be 0-2", expand(R"({"se":"security","sy":3})"));
  TEST_ASSERT_EQUAL_STRING("port must be 1-65535", expand(R"({"se":"mqtt","po":18446744073709551615})"));
  // A pin of -1 is none.
  TEST_ASSERT_GOOD(expand(R"({"se":"network","pw":-1})"));
  // Pins run to GPIO 54, the P4's last.
  TEST_ASSERT_GOOD(expand(R"({"se":"radio","tx":54})"));
  TEST_ASSERT_EQUAL_STRING("TXPin must be 0-54", expand(R"({"se":"radio","tx":55})"));
  TEST_ASSERT_GOOD(expand(R"({"se":"network","mi":54})"));
  // The ESP32's four clock modes; a P4 build takes 0 only (ETH_CLOCK_ID_MAX).
  TEST_ASSERT_GOOD(expand(R"({"se":"network","ck":3})"));
  TEST_ASSERT_EQUAL_STRING("CLKMode must be 0-3", expand(R"({"se":"network","ck":4})"));
  TEST_ASSERT_GOOD(expand(R"({"se":"radio","tp":-30})"));
}
static void test_the_bit_length_is_56_or_80() {
  TEST_ASSERT_GOOD(expand(R"({"se":"radio","bl":56})"));
  TEST_ASSERT_EQUAL_STRING("bitLength must be 56 or 80", expand(R"({"se":"radio","bl":60})"));
  TEST_ASSERT_EQUAL_STRING("bitLength must be 56 or 80", expand(R"({"se":"radio","bl":0})"));
}
static void test_the_radio_proto_is_one_there_is() {
  TEST_ASSERT_GOOD(expand(R"({"se":"radio","pr":2})"));
  TEST_ASSERT_GOOD(expand(R"({"se":"radio","pr":8})"));
  TEST_ASSERT_EQUAL_STRING("proto must be 0, 1, 2, 8 or 9", expand(R"({"se":"radio","pr":5})"));
  TEST_ASSERT_EQUAL_STRING("proto must be 0, 1, 2, 8 or 9", expand(R"({"se":"radio","pr":10})"));
}
static void test_numbers_are_checked_against_what_the_radio_can_be_set_to() {
  // The ends themselves, though 58.03 is a little under it as a float.
  TEST_ASSERT_GOOD(expand(R"({"se":"radio","fq":300,"bw":58.03,"dv":380.85})"));
  TEST_ASSERT_GOOD(expand(R"({"se":"radio","fq":928,"bw":812.5,"dv":1.58})"));
  TEST_ASSERT_EQUAL_STRING("frequency must be 300.00-928.00", expand(R"({"se":"radio","fq":299.9})"));
  TEST_ASSERT_EQUAL_STRING("rxBandwidth must be 58.03-812.50", expand(R"({"se":"radio","bw":900})"));
  TEST_ASSERT_EQUAL_STRING("deviation must be 1.58-380.85", expand(R"({"se":"radio","dv":0})"));
}
static void test_strings_must_fit_their_setting() {
  // hostname holds 31 characters, a PIN 4.
  TEST_ASSERT_GOOD(expand(R"({"se":"general","hn":"0123456789012345678901234567890"})"));
  TEST_ASSERT_EQUAL_STRING("hostname is longer than 31 characters", expand(R"({"se":"general","hn":"01234567890123456789012345678901"})"));
  TEST_ASSERT_EQUAL_STRING("pin is longer than 4 characters", expand(R"({"se":"security","pn":"12345"})"));
  // The broker's hostname is another setting, with room for 64.
  TEST_ASSERT_GOOD(expand(R"({"se":"mqtt","hn":"01234567890123456789012345678901"})"));
}
static void test_addresses_are_checked_and_empty_clears_them() {
  TEST_ASSERT_GOOD(expand(R"({"se":"network","dh":0,"ip":"192.168.1.20","gw":"192.168.1.1","su":"255.255.255.0","d1":"","d2":""})"));
  TEST_ASSERT_EQUAL_STRING(R"({"dhcp":false,"ip":"192.168.1.20","gateway":"192.168.1.1","subnet":"255.255.255.0","dns1":"0.0.0.0","dns2":"0.0.0.0"})", written().c_str());
  TEST_ASSERT_EQUAL_STRING("ip is not an IP address", expand(R"({"se":"network","ip":"192.168.1"})"));
  TEST_ASSERT_EQUAL_STRING("gateway is not an IP address", expand(R"({"se":"network","gw":"192.168.1.256"})"));
  TEST_ASSERT_EQUAL_STRING("dns1 is not an IP address", expand(R"({"se":"network","d1":"1.2.3.4.5"})"));
  TEST_ASSERT_EQUAL_STRING("dns2 is not an IP address", expand(R"({"se":"network","d2":"a.b.c.d"})"));
  TEST_ASSERT_EQUAL_STRING("ip is not an IP address", expand(R"({"se":"network","ip":"1..2.3"})"));
  TEST_ASSERT_EQUAL_STRING("ip is longer than 15 characters", expand(R"({"se":"network","ip":"0001.0002.0003.0004"})"));
}
static void test_only_what_was_sent_is_in_the_request() {
  TEST_ASSERT_GOOD(expand(R"({"se":"network","ro":0})"));
  TEST_ASSERT_EQUAL_STRING(R"({"roaming":false})", written().c_str());
}
static void test_a_save_needs_something_to_save() {
  TEST_ASSERT_EQUAL_STRING("Nothing to save", expand(R"({"se":"mqtt"})"));
}
static void test_an_unknown_key_is_refused() {
  TEST_ASSERT_EQUAL_STRING("Unknown field zz", expand(R"({"se":"general","zz":1})"));
}
static void test_what_is_only_sent_is_refused() {
  // Secrets are only said to be set; a save sends the secret itself.
  TEST_ASSERT_EQUAL_STRING("hasPassword can't be saved in mqtt", expand(R"({"se":"mqtt","hw":1})"));
  TEST_ASSERT_EQUAL_STRING("hasPin can't be saved in security", expand(R"({"se":"security","hi":1})"));
  TEST_ASSERT_EQUAL_STRING("hasPassphrase can't be saved in network", expand(R"({"se":"network","hq":1})"));
  TEST_ASSERT_EQUAL_STRING("hasEthernet can't be saved in network", expand(R"({"se":"network","he":1})"));
  TEST_ASSERT_EQUAL_STRING("radioInit can't be saved in radio", expand(R"({"se":"radio","ri":1})"));
}
static void test_a_field_of_another_section_is_refused() {
  TEST_ASSERT_EQUAL_STRING("ssid can't be saved in general", expand(R"({"se":"general","sd":"home"})"));
  TEST_ASSERT_EQUAL_STRING("ntpServer can't be saved in network", expand(R"({"se":"network","ns":"pool.ntp.org"})"));
  TEST_ASSERT_EQUAL_STRING("TXPin can't be saved in mqtt", expand(R"({"se":"mqtt","tx":12})"));
  TEST_ASSERT_EQUAL_STRING("shadeId can't be saved in radio", expand(R"({"se":"radio","s":1})"));
}
static void test_secure_config_only_is_not_a_field() {
  // Two modes, None and Secured (ADR 0006): "Secure Config Only" has no key.
  TEST_ASSERT_EQUAL_STRING("Unknown field pm", expand(R"({"se":"security","pm":1})"));
}
static void test_ethernet_needs_a_chip_with_it() {
  TEST_ASSERT_GOOD(expand(R"({"se":"network","bo":1})"));
  TEST_ASSERT_EQUAL_STRING("This device has no Ethernet", expand(R"({"se":"network","bo":1})", false));
  // The rest of the section saves without it.
  TEST_ASSERT_GOOD(expand(R"({"se":"network","sd":"home","pp":"secret"})", false));
}

/*********************************************************************
 * The parts a save sets
 ********************************************************************/
static void test_a_save_sets_the_parts_it_has_fields_of() {
  TEST_ASSERT_GOOD(expand(R"({"se":"network","sd":"home","dh":1})"));
  TEST_ASSERT_EQUAL_UINT16(bit(settings_part_t::wifi) | bit(settings_part_t::ip), parts());
  TEST_ASSERT_TRUE(settingsSets(parts(), settings_part_t::wifi));
  TEST_ASSERT_FALSE(settingsSets(parts(), settings_part_t::conn));
  TEST_ASSERT_GOOD(expand(R"({"se":"network","cn":3,"ck":1})"));
  TEST_ASSERT_EQUAL_UINT16(bit(settings_part_t::conn) | bit(settings_part_t::ethernet), parts());
  TEST_ASSERT_GOOD(expand(R"({"se":"general","sb":1,"pz":"CET-1CEST"})"));
  TEST_ASSERT_EQUAL_UINT16(bit(settings_part_t::general) | bit(settings_part_t::ntp), parts());
}
static void test_a_route_name_belongs_to_its_section() {
  // hostname is the broker's in mqtt, and type the radio's bit length in radio.
  TEST_ASSERT_GOOD(expand(R"({"se":"mqtt","hn":"broker"})"));
  TEST_ASSERT_EQUAL_UINT16(bit(settings_part_t::mqtt), parts());
  TEST_ASSERT_GOOD(expand(R"({"se":"radio","bl":56})"));
  TEST_ASSERT_EQUAL_UINT16(bit(settings_part_t::radio), parts());
  TEST_ASSERT_GOOD(expand(R"({"se":"security","sy":0})"));
  TEST_ASSERT_EQUAL_UINT16(bit(settings_part_t::security), parts());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_every_section_by_its_name);
  RUN_TEST(test_a_section_must_be_named);
  RUN_TEST(test_short_keys_become_the_route_names);
  RUN_TEST(test_fields_named_apart_from_the_dictionary);
  RUN_TEST(test_booleans_become_true_and_false);
  RUN_TEST(test_values_are_checked_against_their_kind);
  RUN_TEST(test_a_number_may_be_whole);
  RUN_TEST(test_whole_numbers_are_checked_against_what_the_setting_holds);
  RUN_TEST(test_the_bit_length_is_56_or_80);
  RUN_TEST(test_the_radio_proto_is_one_there_is);
  RUN_TEST(test_numbers_are_checked_against_what_the_radio_can_be_set_to);
  RUN_TEST(test_strings_must_fit_their_setting);
  RUN_TEST(test_addresses_are_checked_and_empty_clears_them);
  RUN_TEST(test_only_what_was_sent_is_in_the_request);
  RUN_TEST(test_a_save_needs_something_to_save);
  RUN_TEST(test_an_unknown_key_is_refused);
  RUN_TEST(test_what_is_only_sent_is_refused);
  RUN_TEST(test_a_field_of_another_section_is_refused);
  RUN_TEST(test_secure_config_only_is_not_a_field);
  RUN_TEST(test_ethernet_needs_a_chip_with_it);
  RUN_TEST(test_a_save_sets_the_parts_it_has_fields_of);
  RUN_TEST(test_a_route_name_belongs_to_its_section);
  return UNITY_END();
}
