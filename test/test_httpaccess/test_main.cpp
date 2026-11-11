#include <unity.h>
#include "HttpAccess.h"

void setUp() {}
void tearDown() {}

/*********************************************************************
 * Web assets
 ********************************************************************/
static void test_the_web_ui_files_are_web_assets() {
  TEST_ASSERT_TRUE(isWebAsset("/"));
  TEST_ASSERT_TRUE(isWebAsset("/index.html"));
  TEST_ASSERT_TRUE(isWebAsset("/index.js"));
  TEST_ASSERT_TRUE(isWebAsset("/app.css"));
  TEST_ASSERT_TRUE(isWebAsset("/favicon.png"));
  TEST_ASSERT_TRUE(isWebAsset("/apple-icon.png"));
  TEST_ASSERT_TRUE(isWebAsset("/icon.svg"));
  TEST_ASSERT_TRUE(isWebAsset("/appversion"));
}
static void test_the_shade_files_are_not_web_assets() {
  TEST_ASSERT_FALSE(isWebAsset("/shades.cfg"));
  TEST_ASSERT_FALSE(isWebAsset("/shades.tmp"));
}
static void test_a_compressed_file_is_not_asked_for_by_name() {
  // serveStatic finds index.js.gz for /index.js.
  TEST_ASSERT_FALSE(isWebAsset("/index.js.gz"));
}
static void test_a_type_alone_is_not_a_web_asset() {
  TEST_ASSERT_FALSE(isWebAsset("/.js"));
  TEST_ASSERT_FALSE(isWebAsset(".js"));
  TEST_ASSERT_FALSE(isWebAsset("/js"));
}
static void test_a_path_leaving_the_root_is_not_a_web_asset() {
  TEST_ASSERT_FALSE(isWebAsset("/../shades.cfg.js"));
  TEST_ASSERT_FALSE(isWebAsset("/x/../index.js"));
}
static void test_a_file_below_the_root_is_not_a_web_asset() {
  TEST_ASSERT_FALSE(isWebAsset("/backup/a.js"));
  TEST_ASSERT_FALSE(isWebAsset("/js/index.js"));
  TEST_ASSERT_FALSE(isWebAsset("//index.js"));
}
static void test_a_missing_or_relative_path_is_not_a_web_asset() {
  TEST_ASSERT_FALSE(isWebAsset(nullptr));
  TEST_ASSERT_FALSE(isWebAsset(""));
  TEST_ASSERT_FALSE(isWebAsset("index.html"));
}

/*********************************************************************
 * Public routes
 ********************************************************************/
static void test_logging_in_and_ssdp_are_public() {
  TEST_ASSERT_TRUE(isPublicRoute("/loginContext", false));
  TEST_ASSERT_TRUE(isPublicRoute("/login", false));
  TEST_ASSERT_TRUE(isPublicRoute("/upnp.xml", false));
}
static void test_the_socket_checks_its_own_token() {
  TEST_ASSERT_TRUE(isPublicRoute("/ws", false));
}
static void test_the_web_ui_is_public() {
  TEST_ASSERT_TRUE(isPublicRoute("/", false));
  TEST_ASSERT_TRUE(isPublicRoute("/index.js", false));
}
static void test_the_other_routes_need_the_token() {
  static const char *const routes[] = {"/backup", "/restore", "/updateFirmware", "/updateApplication",
    "/shadeCommand", "/groupCommand", "/tiltCommand"};
  for(const char *route : routes) TEST_ASSERT_FALSE_MESSAGE(isPublicRoute(route, false), route);
}
static void test_an_unknown_path_needs_the_token() {
  TEST_ASSERT_FALSE(isPublicRoute("/shades.cfg", false));
  TEST_ASSERT_FALSE(isPublicRoute("/controller", false));
  TEST_ASSERT_FALSE(isPublicRoute(nullptr, false));
}
static void test_a_route_differing_in_case_or_length_needs_the_token() {
  TEST_ASSERT_FALSE(isPublicRoute("/Login", false));
  TEST_ASSERT_FALSE(isPublicRoute("/login/", false));
  TEST_ASSERT_FALSE(isPublicRoute("/logins", false));
  TEST_ASSERT_FALSE(isPublicRoute("/ws/", false));
}
static void test_a_web_asset_name_below_a_route_needs_the_token() {
  TEST_ASSERT_FALSE(isPublicRoute("/backup/a.js", false));
  TEST_ASSERT_FALSE(isPublicRoute("/shadeCommand/a.png", false));
  TEST_ASSERT_FALSE(isPublicRoute("/updateFirmware/x.js", false));
  TEST_ASSERT_FALSE(isPublicRoute("/login/a.js", false));
}
static void test_a_preflight_is_public() {
  TEST_ASSERT_TRUE(isPublicRoute("/backup", true));
}

/*********************************************************************
 * Origin and Host
 ********************************************************************/
static void test_the_page_the_device_served_matches() {
  TEST_ASSERT_TRUE(originMatchesHost("http://192.168.1.20", "192.168.1.20"));
  TEST_ASSERT_TRUE(originMatchesHost("http://espsomfyrts.local", "espsomfyrts.local"));
  TEST_ASSERT_TRUE(originMatchesHost("http://192.168.1.20:8000", "192.168.1.20:8000"));
}
static void test_https_behind_a_proxy_matches() {
  TEST_ASSERT_TRUE(originMatchesHost("https://somfy.example.com", "somfy.example.com"));
}
static void test_host_names_match_whatever_their_case() {
  TEST_ASSERT_TRUE(originMatchesHost("http://ESPSomfyRTS.local", "espsomfyrts.local"));
  TEST_ASSERT_TRUE(originMatchesHost("HTTP://192.168.1.20", "192.168.1.20"));
}
static void test_another_site_does_not_match() {
  TEST_ASSERT_FALSE(originMatchesHost("http://evil.example", "192.168.1.20"));
  TEST_ASSERT_FALSE(originMatchesHost("http://192.168.1.20.evil.example", "192.168.1.20"));
  TEST_ASSERT_FALSE(originMatchesHost("http://192.168.1.2", "192.168.1.20"));
}
static void test_another_port_does_not_match() {
  TEST_ASSERT_FALSE(originMatchesHost("http://192.168.1.20:8000", "192.168.1.20"));
}
static void test_a_page_opened_from_a_file_does_not_match() {
  TEST_ASSERT_FALSE(originMatchesHost("null", "192.168.1.20"));
  TEST_ASSERT_FALSE(originMatchesHost("file://", "192.168.1.20"));
}
static void test_an_origin_without_a_host_does_not_match() {
  TEST_ASSERT_FALSE(originMatchesHost("http://192.168.1.20", ""));
  TEST_ASSERT_FALSE(originMatchesHost("http://", ""));
  TEST_ASSERT_FALSE(originMatchesHost("http://192.168.1.20", nullptr));
}
static void test_a_client_that_is_not_a_browser_sends_no_origin() {
  TEST_ASSERT_TRUE(originMatchesHost(nullptr, "192.168.1.20"));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_the_web_ui_files_are_web_assets);
  RUN_TEST(test_the_shade_files_are_not_web_assets);
  RUN_TEST(test_a_compressed_file_is_not_asked_for_by_name);
  RUN_TEST(test_a_type_alone_is_not_a_web_asset);
  RUN_TEST(test_a_path_leaving_the_root_is_not_a_web_asset);
  RUN_TEST(test_a_file_below_the_root_is_not_a_web_asset);
  RUN_TEST(test_a_missing_or_relative_path_is_not_a_web_asset);
  RUN_TEST(test_logging_in_and_ssdp_are_public);
  RUN_TEST(test_the_socket_checks_its_own_token);
  RUN_TEST(test_the_web_ui_is_public);
  RUN_TEST(test_the_other_routes_need_the_token);
  RUN_TEST(test_an_unknown_path_needs_the_token);
  RUN_TEST(test_a_route_differing_in_case_or_length_needs_the_token);
  RUN_TEST(test_a_web_asset_name_below_a_route_needs_the_token);
  RUN_TEST(test_a_preflight_is_public);
  RUN_TEST(test_the_page_the_device_served_matches);
  RUN_TEST(test_https_behind_a_proxy_matches);
  RUN_TEST(test_host_names_match_whatever_their_case);
  RUN_TEST(test_another_site_does_not_match);
  RUN_TEST(test_another_port_does_not_match);
  RUN_TEST(test_a_page_opened_from_a_file_does_not_match);
  RUN_TEST(test_an_origin_without_a_host_does_not_match);
  RUN_TEST(test_a_client_that_is_not_a_browser_sends_no_origin);
  return UNITY_END();
}
