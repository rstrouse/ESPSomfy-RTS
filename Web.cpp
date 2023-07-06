#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include "mbedtls/md.h"
#include "ConfigSettings.h"
#include "Web.h"
#include "Utils.h"
#include "SSDP.h"
#include "Somfy.h"
#include "MQTT.h"

extern ConfigSettings settings;
extern SSDPClass SSDP;
extern rebootDelay_t rebootDelay;
extern SomfyShadeController somfy;
extern Web webServer;
extern MQTTClass mqtt;
#define WEB_MAX_RESPONSE 16384
static char g_content[WEB_MAX_RESPONSE];


// General responses
static const char _response_404[] = "404: Service Not Found";


// Encodings
static const char _encoding_text[] = "text/plain";
static const char _encoding_html[] = "text/html";
static const char _encoding_json[] = "application/json";

WebServer apiServer(8081);
WebServer server(80);
void Web::startup() {
  Serial.println("Launching web server...");
}
void Web::loop() {
  apiServer.handleClient();
  server.handleClient();
}
void Web::sendCORSHeaders() { 
    //server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
    //server.sendHeader(F("Access-Control-Max-Age"), F("600"));
    //server.sendHeader(F("Access-Control-Allow-Methods"), F("PUT,POST,GET,OPTIONS"));
    //server.sendHeader(F("Access-Control-Allow-Headers"), F("*"));
}
void Web::sendCacheHeaders(uint32_t seconds) {
  server.sendHeader(F("Cache-Control"), F("public, max-age=604800, immutable"));
}
void Web::end() {
  //server.end();
}
bool Web::isAuthenticated(WebServer &server, bool cfg) {
  Serial.println("Checking authentication");
  if(settings.Security.type == security_types::None) return true;
  else if(!cfg && (settings.Security.permissions & static_cast<uint8_t>(security_permissions::ConfigOnly)) == 0x01) return true;
  else if(server.hasHeader("apikey")) {
    // Api key was supplied.
    Serial.println("Checking API Key...");
    char token[65];
    memset(token, 0x00, sizeof(token));
    this->createAPIToken(server.client().remoteIP(), token);
    // Compare the tokens.
    if(String(token) != server.header("apikey")) return false;
    server.sendHeader("apikey", token);
  }
  else {
    // Send a 401
    Serial.println("Not authenticated...");
    server.send(401, "Unauthorized API Key");
    return false;
  }
  return true;
}
bool Web::createAPIPinToken(const IPAddress ipAddress, const char *pin, char *token) {
  return this->createAPIToken((String(pin) + ":" + ipAddress.toString()).c_str(), token);
}
bool Web::createAPIPasswordToken(const IPAddress ipAddress, const char *username, const char *password, char *token) {
  return this->createAPIToken((String(username) + ":" + String(password) + ":" + ipAddress.toString()).c_str(), token);
}
bool Web::createAPIToken(const char *payload, char *token) {
    byte hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)settings.serverId, strlen(settings.serverId));
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)payload, strlen(payload)); 
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    Serial.print("Hash: ");
    token[0] = '\0';
    for(int i = 0; i < sizeof(hmacResult); i++){
        char str[3];
        sprintf(str, "%02x", (int)hmacResult[i]);
        strcat(token, str);
    }
    Serial.println(token);
    return true;
}
bool Web::createAPIToken(const IPAddress ipAddress, char *token) {
    String payload;
    if(settings.Security.type == security_types::Password) createAPIPasswordToken(ipAddress, settings.Security.username, settings.Security.password, token);
    else if(settings.Security.type == security_types::PinEntry) createAPIPinToken(ipAddress, settings.Security.pin, token);
    else createAPIToken(ipAddress.toString().c_str(), token);
    return true;
}
void Web::handleLogout(WebServer &server) {
  Serial.println("Logging out of webserver");
  server.sendHeader("Location", "/");
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("Set-Cookie", "ESPSOMFYID=0");
  server.send(301);
}
void Web::handleLogin(WebServer &server) {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    char token[65];
    memset(&token, 0x00, sizeof(token));
    this->createAPIToken(server.client().remoteIP(), token);
    obj["type"] = static_cast<uint8_t>(settings.Security.type);
    if(settings.Security.type == security_types::None) {
      obj["apiKey"] = token;
      obj["msg"] = "Success";
      obj["success"] = true;
      serializeJson(doc, g_content);
      server.send(200, _encoding_json, g_content);
      return;
    }
    Serial.println("Web logging in...");
    char username[33] = "";
    char password[33] = "";
    char pin[5] = "";
    if(server.hasArg("plain")) {
      DynamicJsonDocument docin(256);
      DeserializationError err = deserializeJson(docin, server.arg("plain"));
      if (err) {
        switch (err.code()) {
        case DeserializationError::InvalidInput:
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
          break;
        case DeserializationError::NoMemory:
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
          break;
        default:
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
          break;
        }
        return;
      }
      else {
          JsonObject objin = docin.as<JsonObject>();
          if(objin.containsKey("username")) strlcpy(username, objin["username"], sizeof(username));
          if(objin.containsKey("password")) strlcpy(username, objin["password"], sizeof(password));
          if(objin.containsKey("pin")) strlcpy(pin, objin["pin"], sizeof(pin));
      }
    }
    else {
      if(server.hasArg("username")) strlcpy(username, server.arg("username").c_str(), sizeof(username));
      if(server.hasArg("password")) strlcpy(password, server.arg("password").c_str(), sizeof(password));
      if(server.hasArg("pin")) strlcpy(pin, server.arg("pin").c_str(), sizeof(pin));
    }
    // At this point we should have all the data we need to login.
    if(settings.Security.type == security_types::PinEntry) {
      Serial.print("Validating pin ");
      Serial.println(pin);
      if(strlen(pin) == 0 || strcmp(pin, settings.Security.pin) != 0) {
        obj["success"] = false;
        obj["msg"] = "Invalid Pin Entry";
      }
      else {
        obj["success"] = true;
        obj["msg"] = "Login successful";
        obj["apiKey"] = token;
      }
    }
    else if(settings.Security.type == security_types::Password) {
      if(strlen(username) == 0 || strlen(password) == 0 || strcmp(username, settings.Security.username) != 0 || strcmp(password, settings.Security.password) != 0) {
        obj["success"] = false;
        obj["msg"] = "Invalid username or password";
      }
      else {
        obj["success"] = true;
        obj["msg"] = "Login successful";
        obj["apiKey"] = token;
      }
    }
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    return;
}
void Web::handleStreamFile(WebServer &server, const char *filename, const char *encoding) {
  webServer.sendCORSHeaders();
  // Load the index html page from the data directory.
  Serial.print("Loading file ");
  Serial.println(filename);
  File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.print("Error opening");
    Serial.println(filename);
    server.send(500, _encoding_text, "shades.cfg");
  }
  server.streamFile(file, encoding);
  file.close();
}
void Web::handleController(WebServer &server) {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
      DynamicJsonDocument doc(16384);
      somfy.toJSON(doc);
      serializeJson(doc, g_content);
      server.send(200, _encoding_json, g_content);
    }
    else server.send(404, _encoding_text, _response_404);
}
void Web::handleLoginContext(WebServer &server) {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();
    obj["type"] = static_cast<uint8_t>(settings.Security.type);
    obj["permissions"] = settings.Security.permissions;
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
}
void Web::handleGetShades(WebServer &server) {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
      DynamicJsonDocument doc(16384);
      JsonArray arr = doc.to<JsonArray>();
      somfy.toJSONShades(arr);
      serializeJson(doc, g_content);
      server.send(200, _encoding_json, g_content);
    }
    else server.send(404, _encoding_text, _response_404);
}
void Web::handleGetGroups(WebServer &server) {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
      DynamicJsonDocument doc(16384);
      JsonArray arr = doc.to<JsonArray>();
      somfy.toJSONGroups(arr);
      serializeJson(doc, g_content);
      server.send(200, _encoding_json, g_content);
    }
    else server.send(404, _encoding_text, _response_404);
}
void Web::begin() {
  Serial.println("Creating Web MicroServices...");
  server.enableCORS(true);
  const char *keys[1] = {"apikey"};
  server.collectHeaders(keys, 1);
  apiServer.collectHeaders(keys, 1);  
  apiServer.enableCORS(true);
  apiServer.on("/discovery", []() {
    HTTPMethod method = apiServer.method();
    if (method == HTTP_POST || method == HTTP_GET) {
      Serial.println("Discovery Requested");
      DynamicJsonDocument doc(16384);
      JsonObject obj = doc.to<JsonObject>();
      obj["serverId"] = settings.serverId;
      obj["version"] = settings.fwVersion;
      obj["model"] = "ESPSomfyRTS";
      obj["authType"] = static_cast<uint8_t>(settings.Security.type);
      obj["permissions"] = settings.Security.permissions;
      JsonArray arrShades = obj.createNestedArray("shades");
      somfy.toJSONShades(arrShades);
      JsonArray arrGroups = obj.createNestedArray("groups");
      somfy.toJSONGroups(arrGroups);
      serializeJson(doc, g_content);
      apiServer.send(200, _encoding_json, g_content);
    }
    apiServer.send(500, _encoding_text, "Invalid http method");
    });
  apiServer.on("/shades", []() { webServer.handleGetShades(apiServer); });
  apiServer.on("/groups", []() { webServer.handleGetGroups(apiServer); });
  apiServer.on("/login", []() { webServer.handleLogin(apiServer); });
  apiServer.onNotFound([]() {
    Serial.print("Request 404:");
    HTTPMethod method = apiServer.method();
    switch (method) {
    case HTTP_POST:
      Serial.print("POST ");
      break;
    case HTTP_GET:
      Serial.print("GET ");
      break;
    case HTTP_PUT:
      Serial.print("PUT ");
      break;
    case HTTP_OPTIONS:
      Serial.print("OPTIONS ");
      apiServer.send(200, "OK");
      return;
    default:
      Serial.print("[");
      Serial.print(method);
      Serial.print("]");
      break;

    }
    snprintf(g_content, sizeof(g_content), "404 Service Not Found: %s", apiServer.uri().c_str());
    apiServer.send(404, _encoding_text, g_content);
    });
  apiServer.on("/controller", []() { webServer.handleController(apiServer); });
  apiServer.on("/shadeCommand", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = apiServer.method();
    uint8_t shadeId = 255;
    uint8_t target = 255;
    uint8_t repeat = 1;
    somfy_commands command = somfy_commands::My;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
      if (apiServer.hasArg("shadeId")) {
        shadeId = atoi(apiServer.arg("shadeId").c_str());
        if (apiServer.hasArg("command")) command = translateSomfyCommand(apiServer.arg("command"));
        else if(apiServer.hasArg("target")) target = atoi(apiServer.arg("target").c_str());
        if(apiServer.hasArg("repeat")) repeat = atoi(apiServer.arg("repeat").c_str());
      }
      else if (apiServer.hasArg("plain")) {
        Serial.println("Sending Shade Command");
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, apiServer.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
          else apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
          if (obj.containsKey("command")) {
            String scmd = obj["command"];
            command = translateSomfyCommand(scmd);
          }
          else if(obj.containsKey("target")) {
            target = obj["target"].as<uint8_t>();
          }
          if(obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
        }
      }
      else apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
    }
    SomfyShade* shade = somfy.getShadeById(shadeId);
    if (shade) {
      Serial.print("Received:");
      Serial.println(apiServer.arg("plain"));
      // Send the command to the shade.
      if(target <= 100)
        shade->moveToTarget(shade->transformPosition(target));
      else
        shade->sendCommand(command, repeat);
      DynamicJsonDocument sdoc(512);
      JsonObject sobj = sdoc.to<JsonObject>();
      shade->toJSON(sobj);
      serializeJson(sdoc, g_content);
      apiServer.send(200, _encoding_json, g_content);
    }
    else {
      apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
    }
    });
  apiServer.on("/groupCommand", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = apiServer.method();
    uint8_t groupId = 255;
    uint8_t repeat = 1;
    somfy_commands command = somfy_commands::My;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
      if (apiServer.hasArg("shadeId")) {
        groupId = atoi(apiServer.arg("groupId").c_str());
        if (apiServer.hasArg("command")) command = translateSomfyCommand(apiServer.arg("command"));
        if(apiServer.hasArg("repeat")) repeat = atoi(apiServer.arg("repeat").c_str());
      }
      else if (apiServer.hasArg("plain")) {
        Serial.println("Sending Group Command");
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, apiServer.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("groupId")) groupId = obj["groupId"];
          else apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
          if (obj.containsKey("command")) {
            String scmd = obj["command"];
            command = translateSomfyCommand(scmd);
          }
          if(obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
        }
      }
      else apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
    }
    SomfyGroup * group = somfy.getGroupById(groupId);
    if (group) {
      Serial.print("Received:");
      Serial.println(apiServer.arg("plain"));
      // Send the command to the group.
      group->sendCommand(command, repeat);
      DynamicJsonDocument sdoc(512);
      JsonObject sobj = sdoc.to<JsonObject>();
      group->toJSON(sobj);
      serializeJson(sdoc, g_content);
      apiServer.send(200, _encoding_json, g_content);
    }
    else {
      apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group with the specified id not found.\"}"));
    }
    });
  apiServer.on("/tiltCommand", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = apiServer.method();
    uint8_t shadeId = 255;
    uint8_t target = 255;
    somfy_commands command = somfy_commands::My;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
      if (apiServer.hasArg("shadeId")) {
        shadeId = atoi(apiServer.arg("shadeId").c_str());
        if (apiServer.hasArg("command")) command = translateSomfyCommand(apiServer.arg("command"));
        else if(apiServer.hasArg("target")) target = atoi(apiServer.arg("target").c_str());
      }
      else if (apiServer.hasArg("plain")) {
        Serial.println("Sending Shade Tilt Command");
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, apiServer.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
          else apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
          if (obj.containsKey("command")) {
            String scmd = obj["command"];
            command = translateSomfyCommand(scmd);
          }
          else if(obj.containsKey("target")) {
            target = obj["target"].as<uint8_t>();
          }
        }
      }
      else apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
    }
    SomfyShade* shade = somfy.getShadeById(shadeId);
    if (shade) {
      Serial.print("Received:");
      Serial.println(apiServer.arg("plain"));
      // Send the command to the shade.
      if(target <= 100)
        shade->moveToTiltTarget(shade->transformPosition(target));
      else
        shade->sendTiltCommand(command);
      DynamicJsonDocument sdoc(512);
      JsonObject sobj = sdoc.to<JsonObject>();
      shade->toJSON(sobj);
      serializeJson(sdoc, g_content);
      apiServer.send(200, _encoding_json, g_content);
    }
    else {
      apiServer.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
    }
    });

  server.on("/upnp.xml", []() {
    SSDP.schema(server.client());
    });
  server.on("/", []() {  webServer.handleStreamFile(server, "/index.html", _encoding_html); });
  server.on("/login", []() { webServer.handleLogin(server); });
  server.on("/loginContext", []() { webServer.handleLoginContext(server); });
  server.on("/shades.cfg", []() { webServer.handleStreamFile(server, "/shades.cfg", _encoding_text); });
  server.on("/shades.tmp", []() { webServer.handleStreamFile(server, "/shades.tmp", _encoding_text); });
  server.on("/backup", []() {
    webServer.sendCORSHeaders();
    char filename[120];
    Timestamp ts;
    char * iso = ts.getISOTime();
    // Replace the invalid characters as quickly as we can.
    for(uint8_t i = 0; i < strlen(iso); i++) {
      switch(iso[i]) {
        case '.':
          // Just trim off the ms.
          iso[i] = '\0';
          break;
        case ':':
          iso[i] = '_';
          break;
      }
    }
    snprintf(filename, sizeof(filename), "attachment; filename=\"ESPSomfyRTS %s.backup\"", iso);
    Serial.println(filename);
    server.sendHeader(F("Content-Disposition"), filename);
    Serial.println("Saving current shade information");
    somfy.commit();
    File file = LittleFS.open("/shades.cfg", "r");
    if (!file) {
      Serial.println("Error opening shades.cfg");
      server.send(500, _encoding_text, "shades.cfg");
    }
    server.streamFile(file, _encoding_text);
    file.close();
  });
  server.on("/restore", HTTP_POST, []() {
    webServer.sendCORSHeaders();
    server.sendHeader("Connection", "close");
    server.send(200, _encoding_json, "{\"status\":\"Success\",\"desc\":\"Restoring Shade settings\"}");
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Restore: %s\n", upload.filename.c_str());
        // Begin by opening a new temporary file.
        File fup = LittleFS.open("/shades.tmp", "w");
        fup.close();
      }
      else if (upload.status == UPLOAD_FILE_WRITE) {
        File fup = LittleFS.open("/shades.tmp", "a");
        fup.write(upload.buf, upload.currentSize);
        fup.close();
      }
      else if (upload.status == UPLOAD_FILE_END) {
        // TODO: Do some validation of the file.
        Serial.println("Validating restore");
        // Go through the uploaded file to determine if it is valid.
        if(somfy.loadShadesFile("/shades.tmp")) somfy.commit();
      }
    });
  server.on("/index.js", []() { webServer.handleStreamFile(server, "/index.js", "text/javascript"); });
  server.on("/main.css", []() { webServer.handleStreamFile(server, "/main.css", "text/css"); });
  server.on("/widgets.css", []() { webServer.handleStreamFile(server, "/widgets.css", "text/css"); });
  server.on("/icons.css", []() {  webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/icons.css", "text/css"); });
  server.on("/favicon.png", []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/favicon.png", "image/png"); });
  server.on("/icon.png", []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/icon.png", "image/png"); });
  server.onNotFound([]() {
    Serial.print("Request 404:");
    HTTPMethod method = server.method();
    switch (method) {
    case HTTP_POST:
      Serial.print("POST ");
      break;
    case HTTP_GET:
      Serial.print("GET ");
      break;
    case HTTP_PUT:
      Serial.print("PUT ");
      break;
    case HTTP_OPTIONS:
      Serial.print("OPTIONS ");
      server.send(200, "OK");
      return;
    default:
      Serial.print("[");
      Serial.print(method);
      Serial.print("]");
      break;

    }
    snprintf(g_content, sizeof(g_content), "404 Service Not Found: %s", server.uri().c_str());
    server.send(404, _encoding_text, g_content);
    });
  server.on("/controller", []() { webServer.handleController(server); });
  server.on("/shades", []() { webServer.handleGetShades(server); });
  server.on("/groups", []() { webServer.handleGetGroups(server); });
  server.on("/getNextShade", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    StaticJsonDocument<256> doc;
    uint8_t shadeId = somfy.getNextShadeId();
    JsonObject obj = doc.to<JsonObject>();
    obj["shadeId"] = shadeId;
    obj["remoteAddress"] = somfy.getNextRemoteAddress(shadeId);
    obj["bitLength"] = somfy.transceiver.config.type;
    obj["stepSize"] = 100;
    obj["proto"] = static_cast<uint8_t>(somfy.transceiver.config.proto);
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    });
  server.on("/getNextGroup", []() {
    webServer.sendCORSHeaders();
    StaticJsonDocument<256> doc;
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    uint8_t groupId = somfy.getNextGroupId();
    JsonObject obj = doc.to<JsonObject>();
    obj["groupId"] = groupId;
    obj["remoteAddress"] = somfy.getNextRemoteAddress(groupId);
    obj["bitLength"] = somfy.transceiver.config.type;
    obj["proto"] = static_cast<uint8_t>(somfy.transceiver.config.proto);
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    });
  server.on("/addShade", []() {
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    SomfyShade* shade = nullptr;
    if (method == HTTP_POST || method == HTTP_PUT) {
      Serial.println("Adding a shade");
      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if (err) {
        switch (err.code()) {
        case DeserializationError::InvalidInput:
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
          break;
        case DeserializationError::NoMemory:
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
          break;
        default:
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
          break;
        }
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        Serial.println("Counting shades");
        if (somfy.shadeCount() > SOMFY_MAX_SHADES) {
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Maximum number of shades exceeded.\"}"));
        }
        else {
          Serial.println("Adding shade");
          shade = somfy.addShade(obj);
          if (shade) {
            DynamicJsonDocument sdoc(512);
            JsonObject sobj = sdoc.to<JsonObject>();
            shade->toJSON(sobj);
            serializeJson(sdoc, g_content);
            server.send(200, _encoding_json, g_content);
          }
          else {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error adding shade.\"}"));
          }
        }
      }
    }
    if (shade) {
      //Serial.println("Serializing shade");
      DynamicJsonDocument doc(256);
      JsonObject obj = doc.to<JsonObject>();
      shade->toJSON(obj);
      serializeJson(doc, g_content);
      Serial.println(g_content);
      server.send(200, _encoding_json, g_content);
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error saving Somfy Shade.\"}"));
    }
    });
  server.on("/addGroup", []() {
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    SomfyGroup * group = nullptr;
    if (method == HTTP_POST || method == HTTP_PUT) {
      Serial.println("Adding a group");
      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if (err) {
        switch (err.code()) {
        case DeserializationError::InvalidInput:
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
          break;
        case DeserializationError::NoMemory:
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
          break;
        default:
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
          break;
        }
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        Serial.println("Counting shades");
        if (somfy.groupCount() > SOMFY_MAX_GROUPS) {
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Maximum number of groups exceeded.\"}"));
        }
        else {
          Serial.println("Adding group");
          group = somfy.addGroup(obj);
          if (group) {
            DynamicJsonDocument sdoc(512);
            JsonObject sobj = sdoc.to<JsonObject>();
            group->toJSON(sobj);
            serializeJson(sdoc, g_content);
            server.send(200, _encoding_json, g_content);
          }
          else {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error adding group.\"}"));
          }
        }
      }
    }
    if (group) {
      DynamicJsonDocument doc(256);
      JsonObject obj = doc.to<JsonObject>();
      group->toJSON(obj);
      serializeJson(doc, g_content);
      Serial.println(g_content);
      server.send(200, _encoding_json, g_content);
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error saving Somfy Group.\"}"));
    }
    });
  server.on("/shade", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_GET) {
      if (server.hasArg("shadeId")) {
        int shadeId = atoi(server.arg("shadeId").c_str());
        SomfyShade* shade = somfy.getShadeById(shadeId);
        if (shade) {
          DynamicJsonDocument doc(2048);
          JsonObject obj = doc.to<JsonObject>();
          shade->toJSON(obj);
          serializeJson(doc, g_content);
          server.send(200, _encoding_json, g_content);
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
      }
      else {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid shade id.\"}"));
      }
    }
    else if (method == HTTP_PUT || method == HTTP_POST) {
      // We are updating an existing shade.
      if (server.hasArg("plain")) {
        Serial.println("Updating a shade");
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("shadeId")) {
            SomfyShade* shade = somfy.getShadeById(obj["shadeId"]);
            if (shade) {
              shade->fromJSON(obj);
              shade->save();
              DynamicJsonDocument sdoc(2048);
              JsonObject sobj = sdoc.to<JsonObject>();
              shade->toJSON(sobj);
              serializeJson(sdoc, g_content);
              server.send(200, _encoding_json, g_content);
            }
            else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
          }
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
    }
    });
  server.on("/group", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_GET) {
      if (server.hasArg("groupId")) {
        int groupId = atoi(server.arg("groupId").c_str());
        SomfyGroup* group = somfy.getGroupById(groupId);
        if (group) {
          DynamicJsonDocument doc(2048);
          JsonObject obj = doc.to<JsonObject>();
          group->toJSON(obj);
          serializeJson(doc, g_content);
          server.send(200, _encoding_json, g_content);
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
      }
      else {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid shade id.\"}"));
      }
    }
    else if (method == HTTP_PUT || method == HTTP_POST) {
      // We are updating an existing group.
      if (server.hasArg("plain")) {
        Serial.println("Updating a group");
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("groupId")) {
            SomfyGroup* group = somfy.getGroupById(obj["groupId"]);
            if (group) {
              group->fromJSON(obj);
              group->save();
              DynamicJsonDocument sdoc(2048);
              JsonObject sobj = sdoc.to<JsonObject>();
              group->toJSON(sobj);
              serializeJson(sdoc, g_content);
              server.send(200, _encoding_json, g_content);
            }
            else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
          }
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
    }
    });
  server.on("/groupOptions", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_GET || method == HTTP_POST) {
      if (server.hasArg("groupId")) {
        int groupId = atoi(server.arg("groupId").c_str());
        SomfyGroup* group = somfy.getGroupById(groupId);
        if (group) {
          DynamicJsonDocument doc(4096);
          JsonObject obj = doc.to<JsonObject>();
          group->toJSON(obj);
          JsonArray arr = obj.createNestedArray("availShades");
          for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
            SomfyShade *shade = &somfy.shades[i];
            if(shade->getShadeId() != 255) {
              bool isLinked = false;
              for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) {
                if(group->linkedShades[i] == shade->getShadeId()) {
                  isLinked = true;
                  break;
                }
              }
              if(!isLinked) {
                JsonObject s = arr.createNestedObject();
                shade->toJSONRef(s);
              }
            }
          }
          serializeJson(doc, g_content);
          server.send(200, _encoding_json, g_content);
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
      }
      else {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid group id.\"}"));
      }
    }
    
    });
  server.on("/saveShade", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      // We are updating an existing shade.
      if (server.hasArg("plain")) {
        Serial.println("Updating a shade");
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("shadeId")) {
            SomfyShade* shade = somfy.getShadeById(obj["shadeId"]);
            if (shade) {
              shade->fromJSON(obj);
              shade->save();
              DynamicJsonDocument sdoc(512);
              JsonObject sobj = sdoc.to<JsonObject>();
              shade->toJSON(sobj);
              serializeJson(sdoc, g_content);
              server.send(200, _encoding_json, g_content);
            }
            else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
          }
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
    }
    });
  server.on("/saveGroup", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      // We are updating an existing shade.
      if (server.hasArg("plain")) {
        Serial.println("Updating a group");
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("groupId")) {
            SomfyGroup* group = somfy.getGroupById(obj["groupId"]);
            if (group) {
              group->fromJSON(obj);
              group->save();
              DynamicJsonDocument sdoc(512);
              JsonObject sobj = sdoc.to<JsonObject>();
              group->toJSON(sobj);
              serializeJson(sdoc, g_content);
              server.send(200, _encoding_json, g_content);
            }
            else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
          }
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
    }
    });
  server.on("/tiltCommand", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    uint8_t shadeId = 255;
    uint8_t target = 255;
    somfy_commands command = somfy_commands::My;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
      if (server.hasArg("shadeId")) {
        shadeId = atoi(server.arg("shadeId").c_str());
        if (server.hasArg("command")) command = translateSomfyCommand(server.arg("command"));
        else if(server.hasArg("target")) target = atoi(server.arg("target").c_str());
      }
      else if (server.hasArg("plain")) {
        Serial.println("Sending Shade Tilt Command");
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
          if (obj.containsKey("command")) {
            String scmd = obj["command"];
            command = translateSomfyCommand(scmd);
          }
          else if(obj.containsKey("target")) {
            target = obj["target"].as<uint8_t>();
          }
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
    }
    SomfyShade* shade = somfy.getShadeById(shadeId);
    if (shade) {
      Serial.print("Received:");
      Serial.println(server.arg("plain"));
      // Send the command to the shade.
      if(target <= 100)
        shade->moveToTiltTarget(shade->transformPosition(target));
      else
        shade->sendTiltCommand(command);
      DynamicJsonDocument sdoc(512);
      JsonObject sobj = sdoc.to<JsonObject>();
      shade->toJSON(sobj);
      serializeJson(sdoc, g_content);
      server.send(200, _encoding_json, g_content);
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
    }
    });
  server.on("/shadeCommand", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    uint8_t shadeId = 255;
    uint8_t target = 255;
    uint8_t repeat = 1;
    somfy_commands command = somfy_commands::My;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
      if (server.hasArg("shadeId")) {
        shadeId = atoi(server.arg("shadeId").c_str());
        if (server.hasArg("command")) command = translateSomfyCommand(server.arg("command"));
        else if(server.hasArg("target")) target = atoi(server.arg("target").c_str());
        if(server.hasArg("repeat")) repeat = atoi(server.arg("repeat").c_str());
      }
      else if (server.hasArg("plain")) {
        Serial.println("Sending Shade Command");
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
          if (obj.containsKey("command")) {
            String scmd = obj["command"];
            command = translateSomfyCommand(scmd);
          }
          else if(obj.containsKey("target")) {
            target = obj["target"].as<uint8_t>();
          }
          if(obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
    }
    SomfyShade* shade = somfy.getShadeById(shadeId);
    if (shade) {
      Serial.print("Received:");
      Serial.println(server.arg("plain"));
      // Send the command to the shade.
      if(target <= 100)
        shade->moveToTarget(shade->transformPosition(target));
      else
        shade->sendCommand(command, repeat);
      DynamicJsonDocument sdoc(512);
      JsonObject sobj = sdoc.to<JsonObject>();
      shade->toJSON(sobj);
      serializeJson(sdoc, g_content);
      server.send(200, _encoding_json, g_content);
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
    }
    });
  server.on("/groupCommand", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    uint8_t groupId = 255;
    uint8_t repeat = 1;
    somfy_commands command = somfy_commands::My;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
      if (server.hasArg("groupId")) {
        groupId = atoi(server.arg("shadeId").c_str());
        if (server.hasArg("command")) command = translateSomfyCommand(server.arg("command"));
        if(server.hasArg("repeat")) repeat = atoi(server.arg("repeat").c_str());
      }
      else if (server.hasArg("plain")) {
        Serial.println("Sending Group Command");
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("groupId")) groupId = obj["groupId"];
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
          if (obj.containsKey("command")) {
            String scmd = obj["command"];
            command = translateSomfyCommand(scmd);
          }
          if(obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
    }
    SomfyGroup * group = somfy.getGroupById(groupId);
    if (group) {
      Serial.print("Received:");
      Serial.println(server.arg("plain"));
      // Send the command to the group.
      group->sendCommand(command, repeat);
      DynamicJsonDocument sdoc(512);
      JsonObject sobj = sdoc.to<JsonObject>();
      group->toJSON(sobj);
      serializeJson(sdoc, g_content);
      server.send(200, _encoding_json, g_content);
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group with the specified id not found.\"}"));
    }
    });
  server.on("/setMyPosition", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    uint8_t shadeId = 255;
    int8_t pos = -1;
    int8_t tilt = -1;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
      if (server.hasArg("shadeId")) {
        shadeId = atoi(server.arg("shadeId").c_str());
        if(server.hasArg("pos")) pos = atoi(server.arg("pos").c_str());
        if(server.hasArg("tilt")) tilt = atoi(server.arg("tilt").c_str());
      }
      else if (server.hasArg("plain")) {
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
          if(obj.containsKey("pos")) pos = obj["pos"].as<int8_t>();
          if(obj.containsKey("tilt")) tilt = obj["tilt"].as<int8_t>();
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
    }
    SomfyShade* shade = somfy.getShadeById(shadeId);
    if (shade) {
      // Send the command to the shade.
      if(tilt < 0) tilt = shade->myPos;
      if(shade->tiltType == tilt_types::none) tilt = -1;
      if(pos >= 0 && pos <= 100)
        shade->setMyPosition(shade->transformPosition(pos), shade->transformPosition(tilt));
      DynamicJsonDocument sdoc(512);
      JsonObject sobj = sdoc.to<JsonObject>();
      shade->toJSON(sobj);
      serializeJson(sdoc, g_content);
      server.send(200, _encoding_json, g_content);
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
    }
    });
  server.on("/setRollingCode", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      uint8_t shadeId = 255;
      uint16_t rollingCode = 0;
      if (server.hasArg("plain")) {
        // Its coming in the body.
        StaticJsonDocument<129> doc;
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
          if(obj.containsKey("rollingCode")) rollingCode = obj["rollingCode"];
        }
      }
      else if (server.hasArg("shadeId")) {
        shadeId = atoi(server.arg("shadeId").c_str());
        rollingCode = atoi(server.arg("rollingCode").c_str());
      }
      SomfyShade* shade = nullptr;
      if (shadeId != 255) shade = somfy.getShadeById(shadeId);
      if (!shade) {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade not found to set rolling code\"}"));
      }
      else {
        shade->setRollingCode(rollingCode);
        DynamicJsonDocument doc(512);
        JsonObject obj = doc.to<JsonObject>();
        shade->toJSON(obj);
        serializeJson(doc, g_content);
        server.send(200, _encoding_json, g_content);
      }
    }
  });
  server.on("/setPaired", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    uint8_t shadeId = 255;
    bool paired = false;
    if(server.hasArg("plain")) {
      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if(err) {
        switch(err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
        }
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
        if(obj.containsKey("paired")) paired = obj["paired"];
      }
    }
    else if (server.hasArg("shadeId"))
      shadeId = atoi(server.arg("shadeId").c_str());
    if(server.hasArg("paired"))
      paired = toBoolean(server.arg("paired").c_str(), false);
    SomfyShade* shade = nullptr;
    if (shadeId != 255) shade = somfy.getShadeById(shadeId);
    if (!shade) {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade not found to pair\"}"));
    }
    else {
      shade->paired = paired;
      shade->save();
      DynamicJsonDocument doc(512);
      JsonObject obj = doc.to<JsonObject>();
      shade->toJSON(obj);
      serializeJson(doc, g_content);
      server.send(200, _encoding_json, g_content);
    }
  });
  server.on("/unpairShade", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      uint8_t shadeId = 255;
      if (server.hasArg("plain")) {
        // Its coming in the body.
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
        }
      }
      else if (server.hasArg("shadeId"))
        shadeId = atoi(server.arg("shadeId").c_str());
      SomfyShade* shade = nullptr;
      if (shadeId != 255) shade = somfy.getShadeById(shadeId);
      if (!shade) {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade not found to unpair\"}"));
      }
      else {
        if(shade->bitLength == 56)
          shade->sendCommand(somfy_commands::Prog, 7);
        else
          shade->sendCommand(somfy_commands::Prog, 1);
        shade->paired = false;
        shade->save();
        DynamicJsonDocument doc(512);
        JsonObject obj = doc.to<JsonObject>();
        shade->toJSON(obj);
        serializeJson(doc, g_content);
        server.send(200, _encoding_json, g_content);
      }
    }
    });
  server.on("/unlinkRemote", []() {
    webServer.sendCORSHeaders();
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      // We are updating an existing shade by adding a linked remote.
      if (server.hasArg("plain")) {
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("shadeId")) {
            SomfyShade* shade = somfy.getShadeById(obj["shadeId"]);
            if (shade) {
              if (obj.containsKey("remoteAddress")) {
                shade->unlinkRemote(obj["remoteAddress"]);
              }
              else {
                server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Remote address not provided.\"}"));
              }
              DynamicJsonDocument sdoc(2048);
              JsonObject sobj = sdoc.to<JsonObject>();
              shade->toJSON(sobj);
              serializeJson(sdoc, g_content);
              server.send(200, _encoding_json, g_content);
            }
            else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
          }
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No remote object supplied.\"}"));
    }
    });
  server.on("/linkRemote", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      // We are updating an existing shade by adding a linked remote.
      if (server.hasArg("plain")) {
        Serial.println("Linking a remote");
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("shadeId")) {
            SomfyShade* shade = somfy.getShadeById(obj["shadeId"]);
            if (shade) {
              if (obj.containsKey("remoteAddress")) {
                if (obj.containsKey("rollingCode")) shade->linkRemote(obj["remoteAddress"], obj["rollingCode"]);
                else shade->linkRemote(obj["remoteAddress"]);
              }
              else {
                server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Remote address not provided.\"}"));
              }
              DynamicJsonDocument sdoc(2048);
              JsonObject sobj = sdoc.to<JsonObject>();
              shade->toJSON(sobj);
              serializeJson(sdoc, g_content);
              server.send(200, _encoding_json, g_content);
            }
            else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
          }
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No remote object supplied.\"}"));
    }
    });
  server.on("/linkToGroup", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      if (server.hasArg("plain")) {
        Serial.println("Linking a shade to a group");
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          uint8_t shadeId = obj.containsKey("shadeId") ? obj["shadeId"] : 0;
          uint8_t groupId = obj.containsKey("groupId") ? obj["groupId"] : 0;
          if(groupId == 0) {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group id not provided.\"}"));
            return;
          }
          if(shadeId == 0) {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not provided.\"}"));
            return;
          }
          SomfyGroup * group = somfy.getGroupById(groupId);
          if(!group) {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group id not found.\"}"));
            return;
          }
          SomfyShade * shade = somfy.getShadeById(shadeId);
          if(!shade) {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not found.\"}"));
            return;
          }
          group->linkShade(shadeId);
          DynamicJsonDocument sdoc(2048);
          JsonObject sobj = sdoc.to<JsonObject>();
          group->toJSON(sobj);
          serializeJson(sdoc, g_content);
          server.send(200, _encoding_json, g_content);
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No linking object supplied.\"}"));
    }
  });
  server.on("/unlinkFromGroup", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      if (server.hasArg("plain")) {
        Serial.println("Unlinking a shade from a group");
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          uint8_t shadeId = obj.containsKey("shadeId") ? obj["shadeId"] : 0;
          uint8_t groupId = obj.containsKey("groupId") ? obj["groupId"] : 0;
          if(groupId == 0) {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group id not provided.\"}"));
            return;
          }
          if(shadeId == 0) {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not provided.\"}"));
            return;
          }
          SomfyGroup * group = somfy.getGroupById(groupId);
          if(!group) {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group id not found.\"}"));
            return;
          }
          SomfyShade * shade = somfy.getShadeById(shadeId);
          if(!shade) {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not found.\"}"));
            return;
          }
          group->unlinkShade(shadeId);
          DynamicJsonDocument sdoc(2048);
          JsonObject sobj = sdoc.to<JsonObject>();
          group->toJSON(sobj);
          serializeJson(sdoc, g_content);
          server.send(200, _encoding_json, g_content);
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No unlinking object supplied.\"}"));
    }
  });
  server.on("/deleteShade", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    uint8_t shadeId = 255;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
      if (server.hasArg("shadeId")) {
        shadeId = atoi(server.arg("shadeId").c_str());
      }
      else if (server.hasArg("plain")) {
        Serial.println("Deleting a shade");
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
    }
    SomfyShade* shade = somfy.getShadeById(shadeId);
    if (!shade) server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
    else if(shade->isInGroup()) {
      server.send(400, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"This shade is a member of a group and cannot be deleted.\"}"));
    }
    else {
      somfy.deleteShade(shadeId);
      server.send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Shade deleted.\"}"));
    }
    });
  server.on("/deleteGroup", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    uint8_t groupId = 255;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
      if (server.hasArg("groupId")) {
        groupId = atoi(server.arg("groupId").c_str());
      }
      else if (server.hasArg("plain")) {
        Serial.println("Deleting a group");
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("groupId")) groupId = obj["groupId"];
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
    }
    SomfyGroup * group = somfy.getGroupById(groupId);
    if (!group) server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group with the specified id not found.\"}"));
    else {
      somfy.deleteGroup(groupId);
      server.send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Group deleted.\"}"));
    }
    });
  server.on("/updateFirmware", HTTP_POST, []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    if (Update.hasError())
      server.send(500, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Error updating firmware: \"}");
    else
      server.send(200, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Updating firmware: \"}");
    rebootDelay.reboot = true;
    rebootDelay.rebootTime = millis() + 500;
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Update: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
          Update.printError(Serial);
        }
      }
      else if (upload.status == UPLOAD_FILE_WRITE) {
        /* flashing firmware to ESP*/
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      }
      else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        }
        else {
          Update.printError(Serial);
        }
      }
    });
  server.on("/updateShadeConfig", HTTP_POST, []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    server.sendHeader("Connection", "close");
    server.send(200, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Updating Shade Config: \"}");
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Update: shades.cfg\n");
        File fup = LittleFS.open("/shades.tmp", "w");
        fup.close();
      }
      else if (upload.status == UPLOAD_FILE_WRITE) {
        /* flashing littlefs to ESP*/
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          File fup = LittleFS.open("/shades.tmp", "a");
          fup.write(upload.buf, upload.currentSize);
          fup.close();
        }
      }
      else if (upload.status == UPLOAD_FILE_END) {
        somfy.loadShadesFile("/shades.tmp");
      }
    });
  server.on("/updateApplication", HTTP_POST, []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    server.sendHeader("Connection", "close");
    server.send(200, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Updating Application: \"}");
    rebootDelay.reboot = true;
    rebootDelay.rebootTime = millis() + 500;
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Update: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) { //start with max available size and tell it we are updating the file system.
          Update.printError(Serial);
        }
      }
      else if (upload.status == UPLOAD_FILE_WRITE) {
        /* flashing littlefs to ESP*/
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      }
      else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
          somfy.commit();
        }
        else {
          Update.printError(Serial);
        }
      }
    });
  server.on("/scanaps", []() {
    webServer.sendCORSHeaders();
    int statusCode = 200;
    int n = WiFi.scanNetworks();
    Serial.print("Scanned ");
    Serial.print(n);
    Serial.println(" networks");
    DynamicJsonDocument doc(16384);
    JsonObject obj = doc.to<JsonObject>();
    JsonObject connected = obj.createNestedObject("connected");
    connected["name"] = settings.WIFI.ssid;
    connected["passphrase"] = settings.WIFI.passphrase;
    connected["strength"] = WiFi.RSSI();
    connected["channel"] = WiFi.channel();
    JsonArray arr = obj.createNestedArray("accessPoints");
    for(int i = 0; i < n; ++i) {
      if(WiFi.SSID(i).length() == 0 || WiFi.RSSI(i) < -95) continue; // Ignore hidden and weak networks that we cannot connect to anyway.
      JsonObject a = arr.createNestedObject();
      a["name"] = WiFi.SSID(i);
      a["channel"] = WiFi.channel(i);
      a["encryption"] = settings.WIFI.mapEncryptionType(WiFi.encryptionType(i));
      a["strength"] = WiFi.RSSI(i);
      a["macAddress"] = WiFi.BSSIDstr(i);
    }
    serializeJson(doc, g_content);
    /*
    String content = "{\"connected\": {\"name\":\"" + String(settings.WIFI.ssid) + "\",\"passphrase\":\"" + String(settings.WIFI.passphrase) + "\",\"strength\":" + WiFi.RSSI() + ",\"channel\":" + WiFi.channel() + "}, \"accessPoints\":[";
    for (int i = 0; i < n; ++i) {
      if (i != 0) content += ",";
      content += "{\"name\":\"" + WiFi.SSID(i) + "\",\"channel\":" + WiFi.channel(i) + ",\"encryption\":\"" + settings.WIFI.mapEncryptionType(WiFi.encryptionType(i)) + "\",\"strength\":" + WiFi.RSSI(i) + ",\"macAddress\":\"" + WiFi.BSSIDstr(i) + "\"}";
      delay(10);
    }
    content += "]}";
    */
    server.send(statusCode, "application/json", g_content);
    });
  server.on("/reboot", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
      Serial.println("Rebooting ESP...");
      rebootDelay.reboot = true;
      rebootDelay.rebootTime = millis() + 500;
      server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully started reboot\"}");
    }
    else {
      server.send(201, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
    });
  server.on("/saveSecurity", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      Serial.print("Error parsing JSON ");
      Serial.println(err.c_str());
      String msg = err.c_str();
      server.send(400, _encoding_html, "Error parsing JSON body<br>" + msg);
    }
    else {
      JsonObject obj = doc.as<JsonObject>();
      HTTPMethod method = server.method();
      if (method == HTTP_POST || method == HTTP_PUT) {
        settings.Security.fromJSON(obj);
        settings.Security.save();
        DynamicJsonDocument sdoc(512);
        JsonObject sobj = sdoc.to<JsonObject>();
        char token[65];
        webServer.createAPIToken(server.client().remoteIP(), token);
        obj["apiKey"] = token;
        serializeJson(sdoc, g_content);
        server.send(200, _encoding_json, g_content);
        //server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully saved radio\"}");
      }
      else {
        server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
      }
    }
    });
  server.on("/getSecurity", []() {
    webServer.sendCORSHeaders();
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();
    settings.Security.toJSON(obj);
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    });
  server.on("/saveRadio", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      Serial.print("Error parsing JSON ");
      Serial.println(err.c_str());
      String msg = err.c_str();
      server.send(400, _encoding_html, "Error parsing JSON body<br>" + msg);
    }
    else {
      JsonObject obj = doc.as<JsonObject>();
      HTTPMethod method = server.method();
      if (method == HTTP_POST || method == HTTP_PUT) {
        somfy.transceiver.fromJSON(obj);
        somfy.transceiver.save();
        DynamicJsonDocument sdoc(1024);
        JsonObject sobj = sdoc.to<JsonObject>();
        somfy.transceiver.toJSON(sobj);
        serializeJson(sdoc, g_content);
        server.send(200, _encoding_json, g_content);
        //server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully saved radio\"}");
      }
      else {
        server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
      }
    }
    });
  server.on("/getRadio", []() {
    webServer.sendCORSHeaders();
    DynamicJsonDocument doc(1024);
    JsonObject obj = doc.to<JsonObject>();
    somfy.transceiver.toJSON(obj);
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    });
  server.on("/sendRemoteCommand", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
      somfy_frame_t frame;
      uint8_t repeats = 0;
      if (server.hasArg("address")) {
        frame.remoteAddress = atoi(server.arg("address").c_str());
        if (server.hasArg("encKey")) frame.encKey = atoi(server.arg("encKey").c_str());
        if (server.hasArg("command")) frame.cmd = translateSomfyCommand(server.arg("command"));
        if (server.hasArg("rcode")) frame.rollingCode = atoi(server.arg("rcode").c_str());
        if (server.hasArg("repeats")) repeats = atoi(server.arg("repeats").c_str());
      }
      else if (server.hasArg("plain")) {
        StaticJsonDocument<128> doc;
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          switch (err.code()) {
          case DeserializationError::InvalidInput:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
            break;
          case DeserializationError::NoMemory:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
            break;
          default:
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
            break;
          }
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          String scmd;
          if (obj.containsKey("address")) frame.remoteAddress = obj["address"];
          if (obj.containsKey("command")) scmd = obj["command"].as<String>();
          if (obj.containsKey("repeats")) repeats = obj["repeats"];
          if (obj.containsKey("rcode")) frame.rollingCode = obj["rcode"];
          if (obj.containsKey("encKey")) frame.encKey = obj["encKey"];
          frame.cmd = translateSomfyCommand(scmd.c_str());
        }
      }
      if (frame.remoteAddress > 0 && frame.rollingCode > 0) {
        somfy.sendFrame(frame, repeats);
        server.send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Command Sent\"}"));
      }
      else
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No address or rolling code provided\"}"));
    }
    });
  server.on("/setgeneral", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    DynamicJsonDocument doc(512);
    
    Serial.print("Plain: ");
    Serial.print(server.method());
    Serial.println(server.arg("plain"));
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      Serial.print("Error parsing JSON ");
      Serial.println(err.c_str());
      String msg = err.c_str();
      server.send(400, _encoding_html, "Error parsing JSON body<br>" + msg);
    }
    else {
      JsonObject obj = doc.as<JsonObject>();
      HTTPMethod method = server.method();
      if (method == HTTP_POST || method == HTTP_PUT) {
        // Parse out all the inputs.
        if (obj.containsKey("hostname") || obj.containsKey("ssdpBroadcast")) {
          settings.fromJSON(obj);
          settings.save();
        }
        if (obj.containsKey("ntpServer") || obj.containsKey("ntpServer")) {
          settings.NTP.fromJSON(obj);
          settings.NTP.save();
        }
        server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set General Settings\"}");
      }
      else {
        server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
      }
    }
    });
  server.on("/setNetwork", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      Serial.print("Error parsing JSON ");
      Serial.println(err.c_str());
      String msg = err.c_str();
      server.send(400, _encoding_html, "Error parsing JSON body<br>" + msg);
    }
    else {
      JsonObject obj = doc.as<JsonObject>();
      HTTPMethod method = server.method();
      if (method == HTTP_POST || method == HTTP_PUT) {
        // Parse out all the inputs.
        bool reboot = false;
        if(obj.containsKey("connType") && obj["connType"].as<uint8_t>() != static_cast<uint8_t>(settings.connType)) {
          settings.connType = static_cast<conn_types>(obj["connType"].as<uint8_t>());
          settings.save();
          reboot = true;
        }
        if(settings.connType == conn_types::wifi) {
          if(obj.containsKey("ssid") && obj["ssid"].as<String>().compareTo(settings.WIFI.ssid) != 0) reboot = true;
          if(obj.containsKey("passphrase") && obj["passphrase"].as<String>().compareTo(settings.WIFI.passphrase) != 0) reboot = true;
        }
        else {
          // This is an ethernet connection so if anything changes we need to reboot.
          reboot = true;
        }
        JsonObject objWifi = obj["wifi"];
        JsonObject objEth = obj["ethernet"];
        settings.WIFI.fromJSON(objWifi);
        settings.Ethernet.fromJSON(objEth);
      
        settings.WIFI.save();
        settings.Ethernet.save();
        if (reboot) {
          Serial.println("Rebooting ESP for new Network settings...");
          rebootDelay.reboot = true;
          rebootDelay.rebootTime = millis() + 1000;
        }
        server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set Network Settings\"}");
      }
      else {
        server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
      }
    }
  });
  server.on("/setIP", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    Serial.println("Setting IP...");
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      Serial.print("Error parsing JSON ");
      Serial.println(err.c_str());
      String msg = err.c_str();
      server.send(400, "text/html", "Error parsing JSON body<br>" + msg);
    }
    else {
      JsonObject obj = doc.as<JsonObject>();
      HTTPMethod method = server.method();
      if (method == HTTP_POST || method == HTTP_PUT) {
        settings.IP.fromJSON(obj);
        settings.IP.save();
        server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set Network Settings\"}");
      }
      else {
        server.send(201, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
      }
    }
  });
  server.on("/connectwifi", []() {
    webServer.sendCORSHeaders();
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    Serial.println("Settings WIFI connection...");
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      Serial.print("Error parsing JSON ");
      Serial.println(err.c_str());
      String msg = err.c_str();
      server.send(400, "text/html", "Error parsing JSON body<br>" + msg);
    }
    else {
      JsonObject obj = doc.as<JsonObject>();
      HTTPMethod method = server.method();
      //Serial.print(F("HTTP Method: "));
      //Serial.println(server.method());
      if (method == HTTP_POST || method == HTTP_PUT) {
        String ssid = "";
        String passphrase = "";
        if (obj.containsKey("ssid")) ssid = obj["ssid"].as<String>();
        if (obj.containsKey("passphrase")) passphrase = obj["passphrase"].as<String>();
        bool reboot;
        if (ssid.compareTo(settings.WIFI.ssid) != 0) reboot = true;
        if (passphrase.compareTo(settings.WIFI.passphrase) != 0) reboot = true;
        if (!settings.WIFI.ssidExists(ssid.c_str()) && ssid.length() > 0) {
          server.send(400, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"WiFi Network Does not exist\"}");
        }
        else {
          SETCHARPROP(settings.WIFI.ssid, ssid.c_str(), sizeof(settings.WIFI.ssid));
          SETCHARPROP(settings.WIFI.passphrase, passphrase.c_str(), sizeof(settings.WIFI.passphrase));
          settings.WIFI.save();
          settings.WIFI.print();
          server.send(201, _encoding_json, "{\"status\":\"OK\",\"desc\":\"Successfully set server connection\"}");
          if (reboot) {
            Serial.println("Rebooting ESP for new WiFi settings...");
            rebootDelay.reboot = true;
            rebootDelay.rebootTime = millis() + 1000;
          }
        }
      }
      else {
        server.send(201, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
      }
    }
    });
  server.on("/modulesettings", []() {
    webServer.sendCORSHeaders();
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();
    doc["fwVersion"] = settings.fwVersion;
    settings.toJSON(obj);
    //settings.Ethernet.toJSON(obj);
    //settings.WIFI.toJSON(obj);
    settings.NTP.toJSON(obj);
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    });
  server.on("/networksettings", []() {
    webServer.sendCORSHeaders();
    DynamicJsonDocument doc(2048);
    JsonObject obj = doc.to<JsonObject>();
    doc["fwVersion"] = settings.fwVersion;
    settings.toJSON(obj);
    JsonObject eth = obj.createNestedObject("ethernet");
    settings.Ethernet.toJSON(eth);
    JsonObject wifi = obj.createNestedObject("wifi");
    settings.WIFI.toJSON(wifi);
    JsonObject ip = obj.createNestedObject("ip");
    settings.IP.toJSON(ip);
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    });
  server.on("/connectmqtt", []() {
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      Serial.print("Error parsing JSON ");
      Serial.println(err.c_str());
      String msg = err.c_str();
      server.send(400, F("text/html"), "Error parsing JSON body<br>" + msg);
    }
    else {
      JsonObject obj = doc.as<JsonObject>();
      HTTPMethod method = server.method();
      Serial.print("Saving MQTT ");
      Serial.print(F("HTTP Method: "));
      Serial.println(server.method());
      if (method == HTTP_POST || method == HTTP_PUT) {
        mqtt.disconnect();
        settings.MQTT.fromJSON(obj);
        settings.MQTT.save();
        
        StaticJsonDocument<512> sdoc;
        JsonObject sobj = sdoc.to<JsonObject>();
        settings.MQTT.toJSON(sobj);
        serializeJson(sdoc, g_content);
        server.send(200, _encoding_json, g_content);
      }
      else {
        server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
      }
    }
    });
  server.on("/mqttsettings", []() {
    webServer.sendCORSHeaders();
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();
    settings.MQTT.toJSON(obj);
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    });
  server.begin();
  apiServer.begin();
}
