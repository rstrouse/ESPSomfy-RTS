#include <WiFi.h>
#include <esp_timer.h>
#include "esp_log.h"
#include "mbedtls/md.h"
#include <Preferences.h>
#include <esp_random.h>
#include <bootloader_random.h>
#include "ConfigSettings.h"
#include "Utils.h"
#include "Somfy.h"
#include "WResp.h"
#include "LoopQueue.h"
#include "SomfySnapshot.h"
#include "Web.h"
#include "WebRoutes.h"
#include "outputs/ProtoSocket.h"
#include "Timing.h"
#include "LoginThrottle.h"
#include "HttpAccess.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>

extern ConfigSettings settings;
extern SomfyShadeController somfy;
extern Web webServer;
extern LoopQueue loopQueue;

char g_async_content[WEB_MAX_RESPONSE];


// Encodings
const char _encoding_text[] = "text/plain";
const char _encoding_json[] = "application/json";

static const char *TAG = "Web";

// Failed logins per client IP.
static LoginThrottle loginThrottle;
// The throttle's key for a client: its IPv4 address.  The firmware doesn't
// enable IPv6; an IPv6 client would share key 0 with every other one.
static uint32_t loginKey(const IPAddress &ip) { return (uint32_t)ip; }
// The throttle's clock, in ms, which doesn't wrap as millis() does.
static uint64_t loginNow() { return (uint64_t)esp_timer_get_time() / 1000; }

// The tokens' HMAC key.  It has its own NVS namespace, which no settings
// save clears and no backup holds, so only erasing NVS replaces it.
static uint8_t tokenKey[API_TOKEN_KEY_SIZE];
// Reads the token key, or makes one on first boot.  Runs before the web
// server starts, so no token is made with an empty key, and before WiFi, so
// the hardware RNG takes its entropy from the bootloader's source: calling it
// once the radio is on would disturb WiFi.
static void loadTokenKey() {
  Preferences pref;
  if(!pref.begin("TOKEN")) ESP_LOGE(TAG, "Couldn't open the token key's NVS namespace: every token changes at the next boot");
  if(pref.getBytes("key", tokenKey, sizeof(tokenKey)) != sizeof(tokenKey)) {
    bootloader_random_enable();
    esp_fill_random(tokenKey, sizeof(tokenKey));
    bootloader_random_disable();
    if(pref.putBytes("key", tokenKey, sizeof(tokenKey)) == sizeof(tokenKey)) ESP_LOGI(TAG, "Made a new token key");
    else ESP_LOGE(TAG, "Couldn't store the new token key: every token changes at the next boot");
  }
  pref.end();
}

// Sent when a command can't be handed to the main loop.
static void sendQueueFull(AsyncWebServerRequest *request) {
  request->send(503, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Command queue full\"}"));
}
// Sent when the radio can't take a frame (suspended or queue full).
static void sendRadioUnavailable(AsyncWebServerRequest *request) {
  request->send(503, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Radio TX unavailable\"}"));
}
// Hands an item to loop(), which owns the shades.  When it can't be the
// request is answered with the reason and false is returned.
bool queueLoopItem(AsyncWebServerRequest *request, const loop_item_t &item) {
  // Nothing can go on air during a frequency scan so tell the client now rather
  // than dropping the request once it reaches the radio.
  if(loopItemUsesRadio(item) && somfy.transceiver.txSuspended()) { sendRadioUnavailable(request); return false; }
  if(!loopQueue.submit(item)) { sendQueueFull(request); return false; }
  return true;
}
static bool queueShadeRequest(AsyncWebServerRequest *request, const shade_request_t &req) {
  loop_item_t item;
  item.request = req;
  return queueLoopItem(request, item);
}
AsyncWebServer asyncServer(80);
void Web::startup() {
  ESP_LOGI(TAG, "Launching web server...");
  loadTokenKey();
}
void Web::loop() {
  delay(1);
}
bool Web::tokenAccepted(AsyncWebServerRequest *request, const char *given) {
  if(settings.Security.type == security_types::None) return true;
  if(!given || !request->client()) return false;
  char token[API_TOKEN_SIZE];
  this->createAPIToken(request->client()->remoteIP(), token);
  return tokenMatches(token, given);
}
bool Web::isAuthenticated(AsyncWebServerRequest *request) {
  const AsyncWebHeader *key = request->getHeader("apikey");
  return this->tokenAccepted(request, key ? key->value().c_str() : nullptr);
}
bool Web::createAPIPinToken(const IPAddress ipAddress, const char *pin, char *token) {
  return this->createAPIToken((String(pin) + ":" + ipAddress.toString()).c_str(), token);
}
bool Web::createAPIPasswordToken(const IPAddress ipAddress, const char *username, const char *password, char *token) {
  return this->createAPIToken((String(username) + ":" + String(password) + ":" + ipAddress.toString()).c_str(), token);
}
// Writes HMAC-SHA256(token key, payload) in hex to token, which holds
// API_TOKEN_SIZE.  The payload is "username:password:ip" under a password,
// "pin:ip" under a PIN and "ip" under None.  When mbedtls fails (it allocates
// the HMAC context) token is left empty, which tokenMatches never accepts.
bool Web::createAPIToken(const char *payload, char *token) {
    byte hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    bool ok = mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1) == 0
      && mbedtls_md_hmac_starts(&ctx, tokenKey, sizeof(tokenKey)) == 0
      && mbedtls_md_hmac_update(&ctx, (const unsigned char *)payload, strlen(payload)) == 0
      && mbedtls_md_hmac_finish(&ctx, hmacResult) == 0;
    mbedtls_md_free(&ctx);
    token[0] = '\0';
    if(!ok) {
      ESP_LOGE(TAG, "Couldn't make a token");
      return false;
    }
    for(int i = 0; i < sizeof(hmacResult); i++){
        char str[3];
        sprintf(str, "%02x", (int)hmacResult[i]);
        strcat(token, str);
    }
    return true;
}
bool Web::createAPIToken(const IPAddress ipAddress, char *token) {
    if(settings.Security.type == security_types::Password) return createAPIPasswordToken(ipAddress, settings.Security.username, settings.Security.password, token);
    if(settings.Security.type == security_types::PinEntry) return createAPIPinToken(ipAddress, settings.Security.pin, token);
    return createAPIToken(ipAddress.toString().c_str(), token);
}
// =====================================================
// Async API Handlers
// =====================================================
// The library keeps query parameters and form fields apart, and looks up only
// the first unless asked.  /restore's options come as a form field.
static const AsyncWebParameter *queryOrFormParam(AsyncWebServerRequest *request, const char *name) {
  const AsyncWebParameter *p = request->getParam(name);
  return p ? p : request->getParam(name, true);
}
String asyncParam(AsyncWebServerRequest *request, const char *name) {
  const AsyncWebParameter *p = queryOrFormParam(request, name);
  return p ? p->value() : String();
}
bool asyncHasParam(AsyncWebServerRequest *request, const char *name) {
  return queryOrFormParam(request, name) != nullptr;
}

// -- Serialization helpers --
// The command replies read the live shade model on this task: they copy the
// one item into the snapshot types and write its reference.
static void serializeShadeRef(SomfyShade *shade, JsonFormatter &json) {
  shade_snapshot_t s;
  snapshotShade(*shade, s);
  writeShadeRef(json, s);
}
static void serializeGroupRef(SomfyGroup *group, JsonFormatter &json) {
  group_snapshot_t s;
  snapshotGroup(*group, s);
  writeGroupRef(json, s);
}

// -- Async handler implementations --
void Web::handleLogin(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  char token[API_TOKEN_SIZE];
  memset(&token, 0x00, sizeof(token));
  this->createAPIToken(request->client()->remoteIP(), token);
  if(settings.Security.type == security_types::None) {
    snprintf(g_async_content, sizeof(g_async_content),
      "{\"type\":%u,\"apiKey\":\"%s\",\"msg\":\"Success\",\"success\":true}",
      static_cast<uint8_t>(settings.Security.type), token);
    request->send(200, _encoding_json, g_async_content);
    return;
  }
  // A refused IP isn't told whether these credentials were right.
  IPAddress ip = request->client()->remoteIP();
  uint32_t key = loginKey(ip);
  uint32_t retryMs = loginThrottle.retryAfter(key, loginNow());
  if(retryMs) {
    uint32_t secs = (retryMs + 999) / 1000;
    snprintf(g_async_content, sizeof(g_async_content),
      "{\"type\":%u,\"msg\":\"Too many failed logins. Try again in %lu s.\",\"success\":false,\"retryAfter\":%lu}",
      static_cast<uint8_t>(settings.Security.type), (unsigned long)secs, (unsigned long)secs);
    AsyncWebServerResponse *response = request->beginResponse(429, _encoding_json, g_async_content);
    response->addHeader("Retry-After", String(secs));
    request->send(response);
    return;
  }
  char username[33] = "";
  char password[33] = "";
  char pin[5] = "";
  // Try query params first
  if(asyncHasParam(request, "username")) strlcpy(username, asyncParam(request, "username").c_str(), sizeof(username));
  if(asyncHasParam(request, "password")) strlcpy(password, asyncParam(request, "password").c_str(), sizeof(password));
  if(asyncHasParam(request, "pin")) strlcpy(pin, asyncParam(request, "pin").c_str(), sizeof(pin));
  // Override from JSON body if present
  if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["username"].isNull()) strlcpy(username, obj["username"], sizeof(username));
    if(!obj["password"].isNull()) strlcpy(password, obj["password"], sizeof(password));
    if(!obj["pin"].isNull()) strlcpy(pin, obj["pin"], sizeof(pin));
  }
  // The token these credentials would get, against the stored ones'.
  char ptok[API_TOKEN_SIZE] = "";
  if(settings.Security.type == security_types::PinEntry) this->createAPIPinToken(ip, pin, ptok);
  else if(settings.Security.type == security_types::Password) this->createAPIPasswordToken(ip, username, password, ptok);
  bool success = tokenMatches(token, ptok);
  if(success) {
    loginThrottle.succeeded(key);
    snprintf(g_async_content, sizeof(g_async_content),
      "{\"type\":%u,\"apiKey\":\"%s\",\"msg\":\"Success\",\"success\":true}",
      static_cast<uint8_t>(settings.Security.type), token);
    request->send(200, _encoding_json, g_async_content);
  }
  else {
    uint32_t lockout = loginThrottle.failed(key, loginNow());
    if(lockout) ESP_LOGW(TAG, "Failed login from %s: refused for %lu s", ip.toString().c_str(), (unsigned long)(lockout / 1000));
    snprintf(g_async_content, sizeof(g_async_content),
      "{\"type\":%u,\"msg\":\"Invalid credentials\",\"success\":false}",
      static_cast<uint8_t>(settings.Security.type));
    request->send(401, _encoding_json, g_async_content);
  }
}
void Web::handleShadeCommand(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  shade_request_t req;
  req.type = shade_request_type_t::shade_command;
  // Try query params
  if(asyncHasParam(request, "shadeId")) {
    req.id = asyncParam(request, "shadeId").toInt();
    if(asyncHasParam(request, "command")) req.command = translateSomfyCommand(asyncParam(request, "command"));
    else if(asyncHasParam(request, "target")) req.target = requestTarget(asyncParam(request, "target").toInt());
    if(asyncHasParam(request, "repeat")) req.repeat = requestRepeat(asyncParam(request, "repeat").toInt());
    if(asyncHasParam(request, "stepSize")) req.stepSize = asyncParam(request, "stepSize").toInt();
  }
  else if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["shadeId"].isNull()) req.id = obj["shadeId"];
    else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}")); return; }
    if(!obj["command"].isNull()) { String scmd = obj["command"]; req.command = translateSomfyCommand(scmd); }
    else if(!obj["target"].isNull()) req.target = requestTarget(obj["target"].as<int>());
    if(!obj["repeat"].isNull()) req.repeat = requestRepeat(obj["repeat"].as<int>());
    if(!obj["stepSize"].isNull()) req.stepSize = obj["stepSize"].as<uint8_t>();
  }
  else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}")); return; }
  SomfyShade *shade = somfy.getShadeById(req.id);
  if(shade) {
    ESP_LOGI(TAG, "handleShadeCommand shade=%u target=%d command=%s", req.id, req.target, translateSomfyCommand(req.command).c_str());
    if(!queueShadeRequest(request, req)) return;
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeShadeRef(shade, resp);
    resp.endObject();
    resp.endResponse();
  }
  else {
    ESP_LOGW(TAG, "handleShadeCommand shade=%u not found", req.id);
    request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
  }
}
void Web::handleGroupCommand(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  shade_request_t req;
  req.type = shade_request_type_t::group_command;
  if(asyncHasParam(request, "groupId")) {
    req.id = asyncParam(request, "groupId").toInt();
    if(asyncHasParam(request, "command")) req.command = translateSomfyCommand(asyncParam(request, "command"));
    if(asyncHasParam(request, "repeat")) req.repeat = requestRepeat(asyncParam(request, "repeat").toInt());
    if(asyncHasParam(request, "stepSize")) req.stepSize = asyncParam(request, "stepSize").toInt();
  }
  else if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["groupId"].isNull()) req.id = obj["groupId"];
    else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}")); return; }
    if(!obj["command"].isNull()) { String scmd = obj["command"]; req.command = translateSomfyCommand(scmd); }
    if(!obj["repeat"].isNull()) req.repeat = requestRepeat(obj["repeat"].as<int>());
    if(!obj["stepSize"].isNull()) req.stepSize = obj["stepSize"].as<uint8_t>();
  }
  else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}")); return; }
  SomfyGroup *group = somfy.getGroupById(req.id);
  if(group) {
    ESP_LOGI(TAG, "handleGroupCommand group=%u command=%s", req.id, translateSomfyCommand(req.command).c_str());
    if(!queueShadeRequest(request, req)) return;
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeGroupRef(group, resp);
    resp.endObject();
    resp.endResponse();
  }
  else {
    ESP_LOGW(TAG, "handleGroupCommand group=%u not found", req.id);
    request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group with the specified id not found.\"}"));
  }
}
void Web::handleTiltCommand(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  shade_request_t req;
  req.type = shade_request_type_t::tilt_command;
  if(asyncHasParam(request, "shadeId")) {
    req.id = asyncParam(request, "shadeId").toInt();
    if(asyncHasParam(request, "command")) req.command = translateSomfyCommand(asyncParam(request, "command"));
    else if(asyncHasParam(request, "target")) req.target = requestTarget(asyncParam(request, "target").toInt());
  }
  else if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["shadeId"].isNull()) req.id = obj["shadeId"];
    else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}")); return; }
    if(!obj["command"].isNull()) { String scmd = obj["command"]; req.command = translateSomfyCommand(scmd); }
    else if(!obj["target"].isNull()) req.target = requestTarget(obj["target"].as<int>());
  }
  else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}")); return; }
  SomfyShade *shade = somfy.getShadeById(req.id);
  if(shade) {
    ESP_LOGI(TAG, "handleTiltCommand shade=%u target=%d command=%s", req.id, req.target, translateSomfyCommand(req.command).c_str());
    if(!queueShadeRequest(request, req)) return;
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeShadeRef(shade, resp);
    resp.endObject();
    resp.endResponse();
  }
  else {
    ESP_LOGW(TAG, "handleTiltCommand shade=%u not found", req.id);
    request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
  }
}
void Web::handleBackup(AsyncWebServerRequest *request) {
  bool attach = false;
  if(asyncHasParam(request, "attach")) attach = toBoolean(asyncParam(request, "attach").c_str(), false);
  ESP_LOGI(TAG, "Async saving current shade information");
  somfy.writeBackup();
  if(somfy.backupData.length() == 0) {
    request->send(500, _encoding_text, "backup failed");
    return;
  }
  if(attach) {
    char filename[120];
    Timestamp ts;
    char *iso = ts.getISOTime();
    for(uint8_t i = 0; i < strlen(iso); i++) {
      if(iso[i] == '.') { iso[i] = '\0'; break; }
      if(iso[i] == ':') iso[i] = '_';
    }
    snprintf(filename, sizeof(filename), "attachment; filename=\"ESPSomfyRTS %s.backup\"", iso);
    AsyncWebServerResponse *response = request->beginResponse(200, _encoding_text, somfy.backupData);
    response->addHeader("Content-Disposition", filename);
#ifdef WEB_CORS
    response->addHeader("Access-Control-Expose-Headers", "Content-Disposition");
#endif
    request->send(response);
  }
  else {
    request->send(200, _encoding_text, somfy.backupData);
  }
}
void Web::handleNotFound(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  snprintf(g_async_content, sizeof(g_async_content), "404 Service Not Found: %s", request->url().c_str());
  request->send(404, _encoding_text, g_async_content);
}

// Logs how long part of a request took, when it is worth logging.
static void logRequestTime(AsyncWebServerRequest *request, const IPAddress &ip, const char *part, uint32_t us) {
  if(!worthLogging(us)) return;
  char what[112];
  snprintf(what, sizeof(what), "web %s %s from %u.%u.%u.%u %s", request->methodToString(), request->url().c_str(),
    ip[0], ip[1], ip[2], ip[3], part);
  logElapsed(TAG, what, us);
}
// Logs how long a request takes to handle and, once the connection closes, how
// long the whole exchange took.  The total includes bodies sent after the
// handler returns, such as a web UI file.
static void timeRequest(AsyncWebServerRequest *request, ArMiddlewareNext next) {
  ElapsedTimer timer;
  // Taken now as the client may already be gone when the disconnect runs.
  IPAddress ip = request->client() ? request->client()->remoteIP() : IPAddress();
  // Uploads set their own disconnect handler while the body arrives, before
  // this runs, and replacing it would stop the reboot after an update.
  if(!request->multipart()) {
    request->onDisconnect([request, timer, ip]() { logRequestTime(request, ip, "total", timer.us()); });
  }
  next();
  logRequestTime(request, ip, "handler", timer.us());
}
// Under Secured every route but the public ones needs the token.  This runs
// once the body is in, after an upload's chunks, so the uploads check the
// token themselves as well (uploadAllowed in HttpRoutes.cpp).
static void requireToken(AsyncWebServerRequest *request, ArMiddlewareNext next) {
  if(isPublicRoute(request->url().c_str(), request->method() == HTTP_OPTIONS) || webServer.isAuthenticated(request)) {
    next();
    return;
  }
  request->send(401, _encoding_text, "Unauthorized API Key");
}

void Web::begin() {
  ESP_LOGI(TAG, "Creating Web MicroServices...");
  asyncServer.addMiddleware([](AsyncWebServerRequest *r, ArMiddlewareNext next) { timeRequest(r, next); });
  asyncServer.addMiddleware([](AsyncWebServerRequest *r, ArMiddlewareNext next) { requireToken(r, next); });
#ifdef WEB_CORS
  // For work on the UI from a file:// page (baseUrl in index.js).  A release
  // leaves them out: the UI comes from the device, and HA and scripts aren't
  // browsers.
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "PUT,POST,GET,OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");
#endif
  // Before the routes, so serveStatic never answers /ws.
  protoSocket.begin(asyncServer);
  registerHttpRoutes(asyncServer);
  asyncServer.begin();
}
