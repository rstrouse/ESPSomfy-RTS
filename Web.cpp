#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include "mbedtls/md.h"
#include "ConfigSettings.h"
#include "ConfigFile.h"
#include "Utils.h"
#include "SSDP.h"
#include "Somfy.h"
#include "WResp.h"
#include "Web.h"
#include "MQTT.h"
#include "GitOTA.h"
#include "Network.h"

extern ConfigSettings settings;
extern SSDPClass SSDP;
extern rebootDelay_t rebootDelay;
extern SomfyShadeController somfy;
extern Web webServer;
extern MQTTClass mqtt;
extern GitUpdater git;
extern Network net;

//#define WEB_MAX_RESPONSE 34768
#define WEB_MAX_RESPONSE 4096
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
  server.handleClient();
  delay(1);
  apiServer.handleClient();
  delay(1);
}
void Web::sendCORSHeaders(WebServer &server) { 
    //server.sendHeader(F("Connection"), F("Keep-Alive")); 
    //server.sendHeader(F("Keep-Alive"), F("timeout=5, max=1000"));
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
void Web::handleDeserializationError(WebServer &server, DeserializationError &err) {
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
    webServer.sendCORSHeaders(server);
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
    memset(username, 0x00, sizeof(username));
    memset(password, 0x00, sizeof(password));
    memset(pin, 0x00, sizeof(pin));
    if(server.hasArg("plain")) {
      DynamicJsonDocument docin(512);
      DeserializationError err = deserializeJson(docin, server.arg("plain"));
      if (err) {
        this->handleDeserializationError(server, err);
        return;
      }
      else {
          JsonObject objin = docin.as<JsonObject>();
          if(objin.containsKey("username") && objin["username"]) strlcpy(username, objin["username"], sizeof(username));
          if(objin.containsKey("password") && objin["password"]) strlcpy(password, objin["password"], sizeof(password));
          if(objin.containsKey("pin") && objin["pin"]) strlcpy(pin, objin["pin"], sizeof(pin));
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
  if(git.lockFS) {
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Filesystem update in progress\"}"));
    return;
  }
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  esp_task_wdt_reset();
  // Load the index html page from the data directory.
  Serial.print("Loading file ");
  Serial.println(filename);
  File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.print("Error opening");
    Serial.println(filename);
    server.send(500, _encoding_text, "Error opening file");
  }
  esp_task_wdt_delete(NULL);
  server.streamFile(file, encoding);
  file.close();
  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();
}
void Web::handleController(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  settings.printAvailHeap();
  if (method == HTTP_POST || method == HTTP_GET) {
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    resp.addElem("maxRooms", (uint8_t)SOMFY_MAX_ROOMS);
    resp.addElem("maxShades", (uint8_t)SOMFY_MAX_SHADES);
    resp.addElem("maxGroups", (uint8_t)SOMFY_MAX_GROUPS);
    resp.addElem("maxGroupedShades", (uint8_t)SOMFY_MAX_GROUPED_SHADES);
    resp.addElem("maxLinkedRemotes", (uint8_t)SOMFY_MAX_LINKED_REMOTES);
    resp.addElem("startingAddress", (uint32_t)somfy.startingAddress);
    resp.beginObject("transceiver");
    somfy.transceiver.toJSON(resp);
    resp.endObject();
    resp.beginObject("version");
    git.toJSON(resp);
    resp.endObject();
    resp.beginArray("rooms");
    somfy.toJSONRooms(resp);
    resp.endArray();
    resp.beginArray("shades");
    somfy.toJSONShades(resp);
    resp.endArray();
    resp.beginArray("groups");
    somfy.toJSONGroups(resp);
    resp.endArray();
    resp.beginArray("repeaters");
    somfy.toJSONRepeaters(resp);
    resp.endArray();
    resp.endObject();
    resp.endResponse();
  }
  else server.send(404, _encoding_text, _response_404);
}
void Web::handleLoginContext(WebServer &server) {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    resp.addElem("type", static_cast<uint8_t>(settings.Security.type));
    resp.addElem("permissions", settings.Security.permissions);
    resp.addElem("serverId", settings.serverId);
    resp.addElem("version", settings.fwVersion.name);
    resp.addElem("model", "ESPSomfyRTS");
    resp.addElem("hostname", settings.hostname);
    resp.endObject();
    resp.endResponse();
}
void Web::handleGetRepeaters(WebServer &server) {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginArray();
      somfy.toJSONRepeaters(resp);
      resp.endArray();
      resp.endResponse();
    }
    else server.send(404, _encoding_text, _response_404);
}
void Web::handleGetRooms(WebServer &server) {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginArray();
      somfy.toJSONRooms(resp);
      resp.endArray();
      resp.endResponse();
    }
    else server.send(404, _encoding_text, _response_404);
}
void Web::handleGetShades(WebServer &server) {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginArray();
      somfy.toJSONShades(resp);
      resp.endArray();
      resp.endResponse();
    }
    else server.send(404, _encoding_text, _response_404);
}
void Web::handleGetGroups(WebServer &server) {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginArray();
      somfy.toJSONGroups(resp);
      resp.endArray();
      resp.endResponse();
    }
    else server.send(404, _encoding_text, _response_404);
}
void Web::handleShadeCommand(WebServer& server) {
  webServer.sendCORSHeaders(server);
  if (server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  uint8_t shadeId = 255;
  uint8_t target = 255;
  uint8_t stepSize = 0;
  int8_t repeat = -1;
  somfy_commands command = somfy_commands::My;
  if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("shadeId")) {
      shadeId = atoi(server.arg("shadeId").c_str());
      if (server.hasArg("command")) command = translateSomfyCommand(server.arg("command"));
      else if (server.hasArg("target")) target = atoi(server.arg("target").c_str());
      if (server.hasArg("repeat")) repeat = atoi(server.arg("repeat").c_str());
      if(server.hasArg("stepSize")) stepSize = atoi(server.arg("stepSize").c_str());
    }
    else if (server.hasArg("plain")) {
      Serial.println("Sending Shade Command");
      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if (err) {
        this->handleDeserializationError(server, err);
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
        else if (obj.containsKey("target")) {
            target = obj["target"].as<uint8_t>();
        }
        if (obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
        if(obj.containsKey("stepSize")) stepSize = obj["stepSize"].as<uint8_t>();
      }
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
    SomfyShade* shade = somfy.getShadeById(shadeId);
    if (shade) {
      Serial.print("Received:");
      Serial.println(server.arg("plain"));
      // Send the command to the shade.
      if (target <= 100)
          shade->moveToTarget(shade->transformPosition(target));
      else
          shade->sendCommand(command, repeat > 0 ? repeat : shade->repeats, stepSize);
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      shade->toJSONRef(resp);
      resp.endObject();
      resp.endResponse();
    }
    else {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
    }
  }
  else
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleRepeatCommand(WebServer& server) {
  webServer.sendCORSHeaders(server);
  HTTPMethod method = server.method();
  if (method == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  uint8_t shadeId = 255;
  uint8_t groupId = 255;
  uint8_t stepSize = 0;
  int8_t repeat = -1;
  somfy_commands command = somfy_commands::My;
  if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
    if(server.hasArg("shadeId")) shadeId = atoi(server.arg("shadeId").c_str());
    else if(server.hasArg("groupId")) groupId = atoi(server.arg("groupId").c_str());
    if(server.hasArg("command")) command = translateSomfyCommand(server.arg("command"));
    if(server.hasArg("repeat")) repeat = atoi(server.arg("repeat").c_str());
    if(server.hasArg("stepSize")) stepSize = atoi(server.arg("stepSize").c_str());
    if(shadeId == 255 && groupId == 255 && server.hasArg("plain")) {
      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if (err) {
        this->handleDeserializationError(server, err);
        return;
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
        if(obj.containsKey("groupId")) groupId = obj["groupId"];
        if(obj.containsKey("stepSize")) stepSize = obj["stepSize"];
        if (obj.containsKey("command")) {
            String scmd = obj["command"];
            command = translateSomfyCommand(scmd);
        }
        if (obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
      }
    }
    //DynamicJsonDocument sdoc(512);
    //JsonObject sobj = sdoc.to<JsonObject>();
    if(shadeId != 255) {
      SomfyShade *shade = somfy.getShadeById(shadeId);
      if(!shade) {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade reference could not be found.\"}"));
        return;        
      }
      if(shade->shadeType == shade_types::garage1 && command == somfy_commands::Prog) command = somfy_commands::Toggle;
      if(!shade->isLastCommand(command)) {
        // We are going to send this as a new command.
        shade->sendCommand(command, repeat >= 0 ? repeat : shade->repeats, stepSize);
      }
      else {
        shade->repeatFrame(repeat >= 0 ? repeat : shade->repeats);
      }
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginArray();
      shade->toJSONRef(resp);
      resp.endArray();
      resp.endResponse();
    }
    else if(groupId != 255) {
      SomfyGroup * group = somfy.getGroupById(groupId);
      if(!group) {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group reference could not be found.\"}"));
        return;        
      }
      if(!group->isLastCommand(command)) {
        // We are going to send this as a new command.
        group->sendCommand(command, repeat >= 0 ? repeat : group->repeats, stepSize);
      }
      else
        group->repeatFrame(repeat >= 0 ? repeat : group->repeats);
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      group->toJSONRef(resp);
      resp.endObject();
      resp.endResponse();
        
      //group->toJSON(sobj);
      //serializeJson(sdoc, g_content);
      //server.send(200, _encoding_json, g_content);
    }
  }
  else {
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
  }
}
void Web::handleGroupCommand(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  uint8_t groupId = 255;
  uint8_t stepSize = 0;
  int8_t repeat = -1;
  somfy_commands command = somfy_commands::My;
  if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("groupId")) {
      groupId = atoi(server.arg("groupId").c_str());
      if (server.hasArg("command")) command = translateSomfyCommand(server.arg("command"));
      if(server.hasArg("repeat")) repeat = atoi(server.arg("repeat").c_str());
      if(server.hasArg("stepSize")) stepSize = atoi(server.arg("stepSize").c_str());
    }
    else if (server.hasArg("plain")) {
      Serial.println("Sending Group Command");
      DynamicJsonDocument doc(256);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if (err) {
        this->handleDeserializationError(server, err);
        return;
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        if (obj.containsKey("groupId")) groupId = obj["groupId"];
        else {
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
          return;
        }
        if (obj.containsKey("command")) {
          String scmd = obj["command"];
          command = translateSomfyCommand(scmd);
        }
        if(obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
        if(obj.containsKey("stepSize")) stepSize = obj["stepSize"].as<uint8_t>();
      }
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
    SomfyGroup * group = somfy.getGroupById(groupId);
    if (group) {
      Serial.print("Received:");
      Serial.println(server.arg("plain"));
      // Send the command to the group.
      group->sendCommand(command, repeat >= 0 ? repeat : group->repeats, stepSize);
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      group->toJSONRef(resp);
      resp.endObject();
      resp.endResponse();
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group with the specified id not found.\"}"));
    }
  }
  else
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleTiltCommand(WebServer &server) {
  webServer.sendCORSHeaders(server);
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
        this->handleDeserializationError(server, err);
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
    SomfyShade* shade = somfy.getShadeById(shadeId);
    if (shade) {
      Serial.print("Received:");
      Serial.println(server.arg("plain"));
      // Send the command to the shade.
      if(target <= 100)
        shade->moveToTiltTarget(shade->transformPosition(target));
      else
        shade->sendTiltCommand(command);
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      shade->toJSONRef(resp);
      resp.endObject();
      resp.endResponse();
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
    }  
  }
  else
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleRoom(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  if (method == HTTP_GET) {
    if (server.hasArg("roomId")) {
      int roomId = atoi(server.arg("roomId").c_str());
      SomfyRoom* room = somfy.getRoomById(roomId);
      if (room) {
        JsonResponse resp;
        resp.beginResponse(&server, g_content, sizeof(g_content));
        resp.beginObject();
        room->toJSON(resp);
        resp.endObject();
        resp.endResponse();
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Room Id not found.\"}"));
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid room id.\"}"));
    }
  }
  else if (method == HTTP_PUT || method == HTTP_POST) {
    // We are updating an existing room.
    if (server.hasArg("plain")) {
      Serial.println("Updating a room");
      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if (err) {
        this->handleDeserializationError(server, err);
        return;
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        if (obj.containsKey("roomId")) {
          SomfyRoom* room = somfy.getRoomById(obj["roomId"]);
          if (room) {
            uint8_t err = room->fromJSON(obj);
            if(err == 0) {
              room->save();
              JsonResponse resp;
              resp.beginResponse(&server, g_content, sizeof(g_content));
              resp.beginObject();
              room->toJSON(resp);
              resp.endObject();
              resp.endResponse();
            }
            else {
              snprintf(g_content, sizeof(g_content), "{\"status\":\"DATA\",\"desc\":\"Data Error.\", \"code\":%d}", err);
              server.send(500, _encoding_json, g_content);
            }
          }
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Room Id not found.\"}"));
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room id was supplied.\"}"));
      }
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room object supplied.\"}"));
  }
  else
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleShade(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  if (method == HTTP_GET) {
    if (server.hasArg("shadeId")) {
      int shadeId = atoi(server.arg("shadeId").c_str());
      SomfyShade* shade = somfy.getShadeById(shadeId);
      if (shade) {
        JsonResponse resp;
        resp.beginResponse(&server, g_content, sizeof(g_content));
        resp.beginObject();
        shade->toJSON(resp);
        resp.endObject();
        resp.endResponse();
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
        this->handleDeserializationError(server, err);
        return;
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        if (obj.containsKey("shadeId")) {
          SomfyShade* shade = somfy.getShadeById(obj["shadeId"]);
          if (shade) {
            uint8_t err = shade->fromJSON(obj);
            if(err == 0) {
              shade->save();
              JsonResponse resp;
              resp.beginResponse(&server, g_content, sizeof(g_content));
              resp.beginObject();
              shade->toJSON(resp);
              resp.endObject();
              resp.endResponse();
            }
            else {
              snprintf(g_content, sizeof(g_content), "{\"status\":\"DATA\",\"desc\":\"Data Error.\", \"code\":%d}", err);
              server.send(500, _encoding_json, g_content);
            }
          }
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
      }
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
  }
  else
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleGroup(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  if (method == HTTP_GET) {
    if (server.hasArg("groupId")) {
      int groupId = atoi(server.arg("groupId").c_str());
      SomfyGroup* group = somfy.getGroupById(groupId);
      if (group) {
        JsonResponse resp;
        resp.beginResponse(&server, g_content, sizeof(g_content));
        resp.beginObject();
        group->toJSON(resp);
        resp.endObject();
        resp.endResponse();
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
        this->handleDeserializationError(server, err);
        return;
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        if (obj.containsKey("groupId")) {
          SomfyGroup* group = somfy.getGroupById(obj["groupId"]);
          if (group) {
            group->fromJSON(obj);
            group->save();
            JsonResponse resp;
            resp.beginResponse(&server, g_content, sizeof(g_content));
            resp.beginObject();
            group->toJSON(resp);
            resp.endObject();
            resp.endResponse();
          }
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
      }
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
  }
  else
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleDiscovery(WebServer &server) {
  HTTPMethod method = apiServer.method();
  if (method == HTTP_POST || method == HTTP_GET) {
    Serial.println("Discovery Requested");
    char connType[10] = "Unknown";
    if(net.connType == conn_types_t::ethernet) strcpy(connType, "Ethernet");
    else if(net.connType == conn_types_t::wifi) strcpy(connType, "Wifi");

    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    resp.addElem("serverId", settings.serverId);
    resp.addElem("version", settings.fwVersion.name);
    resp.addElem("latest", git.latest.name);
    resp.addElem("model", "ESPSomfyRTS");
    resp.addElem("hostname", settings.hostname);
    resp.addElem("authType", static_cast<uint8_t>(settings.Security.type));
    resp.addElem("permissions", settings.Security.permissions);
    resp.addElem("chipModel", settings.chipModel);
    resp.addElem("connType", connType);
    resp.addElem("checkForUpdate", settings.checkForUpdate);
    resp.beginObject("memory");
    resp.addElem("max", ESP.getMaxAllocHeap());
    resp.addElem("free", ESP.getFreeHeap());
    resp.addElem("min", ESP.getMinFreeHeap());
    resp.addElem("total", ESP.getHeapSize());
    resp.endObject();
    resp.beginArray("rooms");
    somfy.toJSONRooms(resp);
    resp.endArray();
    resp.beginArray("shades");
    somfy.toJSONShades(resp);
    resp.endArray();
    resp.beginArray("groups");
    somfy.toJSONGroups(resp);
    resp.endArray();
    resp.endObject();
    resp.endResponse();
    net.needsBroadcast = true;
  }
  else
    server.send(500, _encoding_text, "Invalid http method");
}
void Web::handleBackup(WebServer &server, bool attach) {
  webServer.sendCORSHeaders(server);
  if(server.hasArg("attach")) attach = toBoolean(server.arg("attach").c_str(), attach);
  if(attach) {
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
    server.sendHeader(F("Access-Control-Expose-Headers"), F("Content-Disposition"));
  }
  Serial.println("Saving current shade information");
  somfy.writeBackup();
  File file = LittleFS.open("/controller.backup", "r");
  if (!file) {
    Serial.println("Error opening shades.cfg");
    server.send(500, _encoding_text, "shades.cfg");
    return;
  }
  server.streamFile(file, _encoding_text);
  file.close();
}
void Web::handleSetPositions(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  uint8_t shadeId = (server.hasArg("shadeId")) ? atoi(server.arg("shadeId").c_str()) : 255;
  int8_t pos = (server.hasArg("position")) ? atoi(server.arg("position").c_str()) : -1;
  int8_t tiltPos = (server.hasArg("tiltPosition")) ? atoi(server.arg("tiltPosition").c_str()) : -1;
  if(server.hasArg("plain")) {
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      this->handleDeserializationError(server, err);
      return;
    }
    else {
      JsonObject obj = doc.as<JsonObject>();
      if(obj.containsKey("shadeId")) shadeId = obj["shadeId"];
      if(obj.containsKey("position")) pos = obj["position"];
      if(obj.containsKey("tiltPosition")) tiltPos = obj["tiltPosition"];
    }
  }
  if(shadeId != 255) {
    SomfyShade *shade = somfy.getShadeById(shadeId);
    if(shade) {
      if(pos >= 0) shade->target = shade->currentPos = pos;
      if(tiltPos >= 0 && shade->tiltType != tilt_types::none) shade->tiltTarget = shade->currentTiltPos = tiltPos;
      shade->emitState();
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      shade->toJSON(resp);
      resp.endObject();
      resp.endResponse();
    }
    else
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"An invalid shadeId was provided\"}"));
  }
  else {
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"shadeId was not provided\"}"));
  }
}
void Web::handleSetSensor(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  uint8_t shadeId = (server.hasArg("shadeId")) ? atoi(server.arg("shadeId").c_str()) : 255;
  uint8_t groupId = (server.hasArg("groupId")) ? atoi(server.arg("groupId").c_str()) : 255;
  int8_t sunny = (server.hasArg("sunny")) ? toBoolean(server.arg("sunny").c_str(), false) ? 1 : 0 : -1;
  int8_t windy = (server.hasArg("windy")) ? atoi(server.arg("windy").c_str()) : -1;
  int8_t repeat = (server.hasArg("repeat")) ? atoi(server.arg("repeat").c_str()) : -1;
  if(server.hasArg("plain")) {
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      this->handleDeserializationError(server, err);
      return;
    }
    else {
      JsonObject obj = doc.as<JsonObject>();
      if(obj.containsKey("shadeId")) shadeId = obj["shadeId"].as<uint8_t>();
      if(obj.containsKey("groupId")) groupId = obj["groupId"].as<uint8_t>();
      if(obj.containsKey("sunny")) {
        if(obj["sunny"].is<bool>())
          sunny = obj["sunny"].as<bool>() ? 1 : 0;
        else
          sunny = obj["sunny"].as<int8_t>();
      }
      if(obj.containsKey("windy")) {
        if(obj["windy"].is<bool>())
          windy = obj["windy"].as<bool>() ? 1 : 0;
        else
          windy = obj["windy"].as<int8_t>();
      }
      if(obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
    }
  }
  if(shadeId != 255) {
    SomfyShade *shade = somfy.getShadeById(shadeId);
    if(shade) {
      shade->sendSensorCommand(windy, sunny, repeat >= 0 ? (uint8_t)repeat : shade->repeats);
      shade->emitState();
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      shade->toJSON(resp);
      resp.endObject();
      resp.endResponse();
    }
    else
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"An invalid shadeId was provided\"}"));
      
  }
  else if(groupId != 255) {
    SomfyGroup *group = somfy.getGroupById(groupId);
    if(group) {
      group->sendSensorCommand(windy, sunny, repeat >= 0 ? (uint8_t)repeat : group->repeats);
      group->emitState();
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      group->toJSON(resp);
      resp.endObject();
      resp.endResponse();
    }
    else
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"An invalid groupId was provided\"}"));
  }
  else {
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"shadeId was not provided\"}"));
  }
}
void Web::handleDownloadFirmware(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  GitRepo repo;
  GitRelease *rel = nullptr;
  int8_t err = repo.getReleases();
  Serial.println("downloadFirmware called...");
  if(err == 0) {
    if(server.hasArg("ver")) {
      if(strcmp(server.arg("ver").c_str(), "latest") == 0) rel = &repo.releases[0];
      else if(strcmp(server.arg("ver").c_str(), "main") == 0) {
        rel = &repo.releases[GIT_MAX_RELEASES];
      }
      else {
        for(uint8_t i = 0; i < GIT_MAX_RELEASES; i++) {
          if(repo.releases[i].id == 0) continue;
          if(strcmp(repo.releases[i].name, server.arg("ver").c_str()) == 0) {
            rel = &repo.releases[i];  
          }
        }
      }
      if(rel) {
        JsonResponse resp;
        resp.beginResponse(&server, g_content, sizeof(g_content));
        resp.beginObject();
        rel->toJSON(resp);
        resp.endObject();
        resp.endResponse();
        strcpy(git.targetRelease, rel->name);
        git.status = GIT_AWAITING_UPDATE;
      }
      else
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Release not found in repo.\"}"));
    }
    else
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Release version not supplied.\"}"));
  }
  else {
      server.send(err, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error communicating with Github.\"}"));
  }
}
void Web::handleNotFound(WebServer &server) {
    HTTPMethod method = server.method();
    Serial.printf("Request %s 404-%d ", server.uri().c_str(), method);
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
      Serial.println("OPTIONS ");
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
}
void Web::handleReboot(WebServer &server) {
  webServer.sendCORSHeaders(server);
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
}
void Web::begin() {
  Serial.println("Creating Web MicroServices...");
  server.enableCORS(true);
  const char *keys[1] = {"apikey"};
  server.collectHeaders(keys, 1);
  // API Server Handlers
  apiServer.collectHeaders(keys, 1);  
  apiServer.enableCORS(true);
  apiServer.on("/discovery", []() { webServer.handleDiscovery(apiServer); });
  apiServer.on("/rooms", []() {webServer.handleGetRooms(apiServer); });
  apiServer.on("/shades", []() { webServer.handleGetShades(apiServer); });
  apiServer.on("/groups", []() { webServer.handleGetGroups(apiServer); });
  apiServer.on("/login", []() { webServer.handleLogin(apiServer); });
  apiServer.onNotFound([]() { webServer.handleNotFound(apiServer); });
  apiServer.on("/controller", []() { webServer.handleController(apiServer); });
  apiServer.on("/shadeCommand", []() { webServer.handleShadeCommand(apiServer); });
  apiServer.on("/groupCommand", []() { webServer.handleGroupCommand(apiServer); });
  apiServer.on("/tiltCommand", []() { webServer.handleTiltCommand(apiServer); });
  apiServer.on("/repeatCommand", []() { webServer.handleRepeatCommand(apiServer); });
  apiServer.on("/room", HTTP_GET, [] () { webServer.handleRoom(apiServer); });
  apiServer.on("/shade", HTTP_GET, [] () { webServer.handleShade(apiServer); });
  apiServer.on("/group", HTTP_GET, [] () { webServer.handleGroup(apiServer); });
  apiServer.on("/setPositions", []() { webServer.handleSetPositions(apiServer); });
  apiServer.on("/setSensor", []() { webServer.handleSetSensor(apiServer); });
  apiServer.on("/downloadFirmware", []() { webServer.handleDownloadFirmware(apiServer); });
  apiServer.on("/backup", []() { webServer.handleBackup(apiServer); });
  apiServer.on("/reboot", []() { webServer.handleReboot(apiServer); });
  
  // Web Interface
  server.on("/tiltCommand", []() { webServer.handleTiltCommand(server); });
  server.on("/repeatCommand", []() { webServer.handleRepeatCommand(server); });
  server.on("/shadeCommand", []() { webServer.handleShadeCommand(server); });
  server.on("/groupCommand", []() { webServer.handleGroupCommand(server); });
  server.on("/setPositions", []() { webServer.handleSetPositions(server); });
  server.on("/setSensor", []() { webServer.handleSetSensor(server); });
  server.on("/upnp.xml", []() { SSDP.schema(server.client()); });
  server.on("/", []() { webServer.handleStreamFile(server, "/index.html", _encoding_html); });
  server.on("/login", []() { webServer.handleLogin(server); });
  server.on("/loginContext", []() { webServer.handleLoginContext(server); });
  server.on("/shades.cfg", []() { webServer.handleStreamFile(server, "/shades.cfg", _encoding_text); });
  server.on("/shades.tmp", []() { webServer.handleStreamFile(server, "/shades.tmp", _encoding_text); });
  server.on("/getReleases", []() {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    GitRepo repo;
    repo.getReleases();
    git.setCurrentRelease(repo);
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    repo.toJSON(resp);
    resp.endObject();
    resp.endResponse();
  });
  server.on("/downloadFirmware", []() { webServer.handleDownloadFirmware(server); });
  server.on("/cancelFirmware", []() {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    // If we are currently downloading the filesystem we cannot cancel.
    if(!git.lockFS) {
      git.status = GIT_UPDATE_CANCELLING;
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      git.toJSON(resp);
      resp.endObject();
      resp.endResponse();
      git.cancelled = true;
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Cannot cancel during filesystem update.\"}"));
    }
  });
  server.on("/backup", []() { webServer.handleBackup(server, true); });
  server.on("/restore", HTTP_POST, []() {
    webServer.sendCORSHeaders(server);
    server.sendHeader("Connection", "close");
    if(webServer.uploadSuccess) {
      server.send(200, _encoding_json, "{\"status\":\"Success\",\"desc\":\"Restoring Shade settings\"}");
      restore_options_t opts;
      if(server.hasArg("data")) {
        Serial.println(server.arg("data"));
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, server.arg("data"));
        if (err) {
          webServer.handleDeserializationError(server, err);
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          opts.fromJSON(obj);
        }
      }
      else {
        Serial.println("No restore options sent.  Using defaults...");
        opts.shades = true;
      }
      ShadeConfigFile::restore(&somfy, "/shades.tmp", opts);
      Serial.println("Rebooting ESP for restored settings...");
      rebootDelay.reboot = true;
      rebootDelay.rebootTime = millis() + 1000;
    }
    }, []() {
      esp_task_wdt_reset();
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        webServer.uploadSuccess = false;
        Serial.printf("Restore: %s\n", upload.filename.c_str());
        // Begin by opening a new temporary file.
        File fup = LittleFS.open("/shades.tmp", "w");
        fup.close();
      }
      else if (upload.status == UPLOAD_FILE_WRITE) {
        File fup = LittleFS.open("/shades.tmp", "a");
        //upload.buf[upload.currentSize] = 0x00;
        //Serial.print((char *)upload.buf);
        fup.write(upload.buf, upload.currentSize);
        fup.close();
      }
      else if (upload.status == UPLOAD_FILE_END) {
        webServer.uploadSuccess = true;
      }

    });
  server.on("/index.js", []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/index.js", "text/javascript"); });
  server.on("/main.css", []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/main.css", "text/css"); });
  server.on("/widgets.css", []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/widgets.css", "text/css"); });
  server.on("/icons.css", []() {  webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/icons.css", "text/css"); });
  server.on("/favicon.png", []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/favicon.png", "image/png"); });
  server.on("/icon.png", []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/icon.png", "image/png"); });
  server.on("/icon.svg", []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/icon.svg", "image/svg+xml"); });
  server.on("/apple-icon.png", []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/apple-icon.png", "image/png"); });
  server.onNotFound([]() { webServer.handleNotFound(server); });
  server.on("/controller", []() { webServer.handleController(server); });
  server.on("/rooms", []() { webServer.handleGetRooms(server); });
  server.on("/shades", []() { webServer.handleGetShades(server); });
  server.on("/groups", []() { webServer.handleGetGroups(server); });
  server.on("/room", []() { webServer.handleRoom(server); });
  server.on("/shade", []() { webServer.handleShade(server); });
  server.on("/group", []() { webServer.handleGroup(server); });
  server.on("/getNextRoom", []() {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    resp.addElem("roomId", somfy.getNextRoomId());
    resp.endObject();
    resp.endResponse();
  });
  server.on("/getNextShade", []() {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    uint8_t shadeId = somfy.getNextShadeId();
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    resp.addElem("shadeId", shadeId);
    resp.addElem("remoteAddress", (uint32_t)somfy.getNextRemoteAddress(shadeId));
    resp.addElem("bitLength", somfy.transceiver.config.type);
    resp.addElem("stepSize", (uint8_t)100);
    resp.addElem("proto", static_cast<uint8_t>(somfy.transceiver.config.proto));
    resp.endObject();
    resp.endResponse();
    });
  server.on("/getNextGroup", []() {
    webServer.sendCORSHeaders(server);
    uint8_t groupId = somfy.getNextGroupId();
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    resp.addElem("groupId", groupId);
    resp.addElem("remoteAddress", (uint32_t)somfy.getNextRemoteAddress(groupId));
    resp.addElem("bitLength", somfy.transceiver.config.type);
    resp.addElem("proto", static_cast<uint8_t>(somfy.transceiver.config.proto));
    resp.endObject();
    resp.endResponse();
    });
  server.on("/addRoom", []() {
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    SomfyRoom * room = nullptr;
    if (method == HTTP_POST || method == HTTP_PUT) {
      Serial.println("Adding a room");
      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if (err) {
        webServer.handleDeserializationError(server, err);
        return;
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        Serial.println("Counting rooms");
        if (somfy.roomCount() > SOMFY_MAX_ROOMS) {
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Maximum number of rooms exceeded.\"}"));
          return;
        }
        else {
          Serial.println("Adding room");
          room = somfy.addRoom(obj);
          if (!room) {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error adding room.\"}"));
            return;
          }
        }
      }
    }
    if (room) {
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      room->toJSON(resp);
      resp.endObject();
      resp.endResponse();
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error saving Somfy Room.\"}"));
    }
    });
  server.on("/addShade", []() {
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    SomfyShade* shade = nullptr;
    if (method == HTTP_POST || method == HTTP_PUT) {
      Serial.println("Adding a shade");
      DynamicJsonDocument doc(1024);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if (err) {
        webServer.handleDeserializationError(server, err);
        return;
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        Serial.println("Counting shades");
        if (somfy.shadeCount() > SOMFY_MAX_SHADES) {
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Maximum number of shades exceeded.\"}"));
          return;
        }
        else {
          Serial.println("Adding shade");
          shade = somfy.addShade(obj);
          if (!shade) {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error adding shade.\"}"));
            return;
          }
        }
      }
    }
    if (shade) {
      //Serial.println("Serializing shade");
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      shade->toJSON(resp);
      resp.endObject();
      resp.endResponse();
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
        webServer.handleDeserializationError(server, err);
        return;
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        Serial.println("Counting shades");
        if (somfy.groupCount() > SOMFY_MAX_GROUPS) {
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Maximum number of groups exceeded.\"}"));
          return;
        }
        else {
          Serial.println("Adding group");
          group = somfy.addGroup(obj);
          if (!group) {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error adding group.\"}"));
            return;
          }
        }
      }
    }
    if (group) {
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      group->toJSON(resp);
      resp.endObject();
      resp.endResponse();
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error saving Somfy Group.\"}"));
    }
    });
  server.on("/groupOptions", []() {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_GET || method == HTTP_POST) {
      if (server.hasArg("groupId")) {
        int groupId = atoi(server.arg("groupId").c_str());
        SomfyGroup* group = somfy.getGroupById(groupId);
        if (group) {
          JsonResponse resp;
          resp.beginResponse(&server, g_content, sizeof(g_content));
          resp.beginObject();
          group->toJSON(resp);
          resp.beginArray("availShades");
          for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
            SomfyShade *shade = &somfy.shades[i];
            if(shade->getShadeId() != 255) {
              bool isLinked = false;
              for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) {
                if(group->linkedShades[j] == shade->getShadeId()) {
                  isLinked = true;
                  break;
                }
              }
              if(!isLinked) {
                resp.beginObject();
                shade->toJSONRef(resp);
                resp.endObject();
              }
            }
          }
          resp.endArray();
          resp.endObject();
          resp.endResponse();
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
      }
      else {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid group id.\"}"));
      }
    }
    
    });
  server.on("/saveRoom", []() {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      // We are updating an existing room.
      if (server.hasArg("plain")) {
        Serial.println("Updating a room");
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          webServer.handleDeserializationError(server, err);
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("roomId")) {
            SomfyRoom* room = somfy.getRoomById(obj["roomId"]);
            if (room) {
              room->fromJSON(obj);
              room->save();
              JsonResponse resp;
              resp.beginResponse(&server, g_content, sizeof(g_content));
              resp.beginObject();
              room->toJSON(resp);
              resp.endObject();
              resp.endResponse();
            }
            else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Room Id not found.\"}"));
          }
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room id was supplied.\"}"));
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room object supplied.\"}"));
    }
  });

  server.on("/saveShade", []() {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      // We are updating an existing shade.
      if (server.hasArg("plain")) {
        Serial.println("Updating a shade");
        DynamicJsonDocument doc(1024);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          webServer.handleDeserializationError(server, err);
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("shadeId")) {
            SomfyShade* shade = somfy.getShadeById(obj["shadeId"]);
            if (shade) {
              int8_t err = shade->fromJSON(obj);
              if(err == 0) {
                shade->save();
                JsonResponse resp;
                resp.beginResponse(&server, g_content, sizeof(g_content));
                resp.beginObject();
                shade->toJSON(resp);
                resp.endObject();
                resp.endResponse();
              }
              else {
                snprintf(g_content, sizeof(g_content), "{\"status\":\"DATA\",\"desc\":\"Data Error.\", \"code\":%d}", err);
                server.send(500, _encoding_json, g_content);
              }
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
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      // We are updating an existing shade.
      if (server.hasArg("plain")) {
        Serial.println("Updating a group");
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          webServer.handleDeserializationError(server, err);
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("groupId")) {
            SomfyGroup* group = somfy.getGroupById(obj["groupId"]);
            if (group) {
              group->fromJSON(obj);
              group->save();
              JsonResponse resp;
              resp.beginResponse(&server, g_content, sizeof(g_content));
              resp.beginObject();
              group->toJSON(resp);
              resp.endObject();
              resp.endResponse();
            }
            else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
          }
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
    }
    });
  server.on("/setMyPosition", []() {
    webServer.sendCORSHeaders(server);
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
          webServer.handleDeserializationError(server, err);
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
      SomfyShade* shade = somfy.getShadeById(shadeId);
      if (shade) {
        // Send the command to the shade.
        if(tilt < 0) tilt = shade->myPos;
        if(shade->tiltType == tilt_types::none) tilt = -1;
        if(pos >= 0 && pos <= 100)
          shade->setMyPosition(shade->transformPosition(pos), shade->transformPosition(tilt));
          JsonResponse resp;
          resp.beginResponse(&server, g_content, sizeof(g_content));
          resp.beginObject();
          shade->toJSONRef(resp);
          resp.endObject();
          resp.endResponse();
      }
      else {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
      }
    }
    else 
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
    });
  server.on("/setRollingCode", []() {
    webServer.sendCORSHeaders(server);
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
          webServer.handleDeserializationError(server, err);
          return;
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
        JsonResponse resp;
        resp.beginResponse(&server, g_content, sizeof(g_content));
        resp.beginObject();
        shade->toJSON(resp);
        resp.endObject();
        resp.endResponse();
      }
    }
  });
  server.on("/setPaired", []() {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    uint8_t shadeId = 255;
    bool paired = false;
    if(server.hasArg("plain")) {
      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if(err) {
          webServer.handleDeserializationError(server, err);
          return;
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
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      shade->toJSON(resp);
      resp.endObject();
      resp.endResponse();
    }
  });
  server.on("/unpairShade", []() {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      uint8_t shadeId = 255;
      if (server.hasArg("plain")) {
        // Its coming in the body.
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          webServer.handleDeserializationError(server, err);
          return;
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
        JsonResponse resp;
        resp.beginResponse(&server, g_content, sizeof(g_content));
        resp.beginObject();
        shade->toJSON(resp);
        resp.endObject();
        resp.endResponse();
      }
    }
    });
  server.on("/linkRepeater", []() {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      // We are adding a linked repeater.
      uint32_t address = 0;
      if (server.hasArg("plain")) {
        Serial.println("Linking a repeater");
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          webServer.handleDeserializationError(server, err);
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("address")) address = obj["address"];
          else if(obj.containsKey("remoteAddress")) address = obj["remoteAddress"];
        }
      }
      else if(server.hasArg("address"))
        address = atoi(server.arg("address").c_str());
      if(address == 0)
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No repeater address was supplied.\"}"));
      else {
        somfy.linkRepeater(address);
        JsonResponse resp;
        resp.beginResponse(&server, g_content, sizeof(g_content));
        resp.beginArray();
        somfy.toJSONRepeaters(resp);
        resp.endArray();
        resp.endResponse();
      }
    }
  });
  server.on("/unlinkRepeater", []() {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      // We are adding a linked repeater.
      uint32_t address = 0;
      if (server.hasArg("plain")) {
        Serial.println("Unlinking a repeater");
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          webServer.handleDeserializationError(server, err);
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("address")) address = obj["address"];
          else if(obj.containsKey("remoteAddress")) address = obj["remoteAddress"];
        }
      }
      else if(server.hasArg("address"))
        address = atoi(server.arg("address").c_str());
      if(address == 0)
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No repeater address was supplied.\"}"));
      else {
        somfy.unlinkRepeater(address);
        JsonResponse resp;
        resp.beginResponse(&server, g_content, sizeof(g_content));
        resp.beginArray();
        somfy.toJSONRepeaters(resp);
        resp.endArray();
        resp.endResponse();
      }
    }
  });
  
  server.on("/unlinkRemote", []() {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      // We are updating an existing shade by adding a linked remote.
      if (server.hasArg("plain")) {
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          webServer.handleDeserializationError(server, err);
          return;
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
              JsonResponse resp;
              resp.beginResponse(&server, g_content, sizeof(g_content));
              resp.beginObject();
              shade->toJSON(resp);
              resp.endObject();
              resp.endResponse();
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
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      // We are updating an existing shade by adding a linked remote.
      if (server.hasArg("plain")) {
        Serial.println("Linking a remote");
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          webServer.handleDeserializationError(server, err);
          return;
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
              JsonResponse resp;
              resp.beginResponse(&server, g_content, sizeof(g_content));
              resp.beginObject();
              shade->toJSON(resp);
              resp.endObject();
              resp.endResponse();
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
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
      if (server.hasArg("plain")) {
        Serial.println("Linking a shade to a group");
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          webServer.handleDeserializationError(server, err);
          return;
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
          JsonResponse resp;
          resp.beginResponse(&server, g_content, sizeof(g_content));
          resp.beginObject();
          group->toJSON(resp);
          resp.endObject();
          resp.endResponse();
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No linking object supplied.\"}"));
    }
  });
  server.on("/unlinkFromGroup", []() {
    webServer.sendCORSHeaders(server);
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
          JsonResponse resp;
          resp.beginResponse(&server, g_content, sizeof(g_content));
          resp.beginObject();
          group->toJSON(resp);
          resp.endObject();
          resp.endResponse();
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No unlinking object supplied.\"}"));
    }
  });
  server.on("/deleteRoom", []() {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    uint8_t roomId = 0;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
      if (server.hasArg("roomId")) {
        roomId = atoi(server.arg("roomId").c_str());
      }
      else if (server.hasArg("plain")) {
        Serial.println("Deleting a Room");
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
          webServer.handleDeserializationError(server, err);
          return;
        }
        else {
          JsonObject obj = doc.as<JsonObject>();
          if (obj.containsKey("roomId")) roomId = obj["roomId"];
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room id was supplied.\"}"));
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room object supplied.\"}"));
    }
    SomfyRoom* room = somfy.getRoomById(roomId);
    if (!room) server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Room with the specified id not found.\"}"));
    else {
      somfy.deleteRoom(roomId);
      server.send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Room deleted.\"}"));
    }
    });
  server.on("/deleteShade", []() {
    webServer.sendCORSHeaders(server);
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
          webServer.handleDeserializationError(server, err);
          return;
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
    webServer.sendCORSHeaders(server);
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
          webServer.handleDeserializationError(server, err);
          return;
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
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    if (Update.hasError())
      server.send(500, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Error updating firmware: \"}");
    else
      server.send(200, _encoding_json, "{\"status\":\"SUCCESS\",\"desc\":\"Successfully updated firmware\"}");
    rebootDelay.reboot = true;
    rebootDelay.rebootTime = millis() + 500;
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        webServer.uploadSuccess = false;
        Serial.printf("Update: %s - %d\n", upload.filename.c_str(), upload.totalSize);
        //if(!Update.begin(upload.totalSize, U_SPIFFS)) {
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
          Update.printError(Serial);
        }
        else {
          somfy.transceiver.end(); // Shut down the radio so we do not get any interrupts during this process.
          mqtt.end();
        }
      }
      else if(upload.status == UPLOAD_FILE_ABORTED) {
        Serial.printf("Upload of %s aborted\n", upload.filename.c_str());
        Update.abort();
      }
      else if (upload.status == UPLOAD_FILE_WRITE) {
        /* flashing firmware to ESP*/
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
          Serial.printf("Upload of %s aborted invalid size %d\n", upload.filename.c_str(), upload.currentSize);
          Update.abort();
        }
      }
      else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
          webServer.uploadSuccess = true;
        }
        else {
          Update.printError(Serial);
        }
      }
      esp_task_wdt_reset();
    });
  server.on("/updateShadeConfig", HTTP_POST, []() {
    if(git.lockFS) {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Filesystem update in progress\"}"));
      return;
    }
    webServer.sendCORSHeaders(server);
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
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    server.sendHeader("Connection", "close");
    if (Update.hasError())
      server.send(500, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Error updating application: \"}");
    else
      server.send(200, _encoding_json, "{\"status\":\"SUCCESS\",\"desc\":\"Successfully updated application\"}");
    rebootDelay.reboot = true;
    rebootDelay.rebootTime = millis() + 500;
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        webServer.uploadSuccess = false;
        Serial.printf("Update: %s %d\n", upload.filename.c_str(), upload.totalSize);
        //if(!Update.begin(upload.totalSize, U_SPIFFS)) {
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) { //start with max available size and tell it we are updating the file system.
          Update.printError(Serial);
        }
        else {
          somfy.transceiver.end(); // Shut down the radio so we do not get any interrupts during this process.
          mqtt.end();
        }
      }
      else if(upload.status == UPLOAD_FILE_ABORTED) {
        Serial.printf("Upload of %s aborted\n", upload.filename.c_str());
        Update.abort();
        somfy.commit();
      }
      else if (upload.status == UPLOAD_FILE_WRITE) {
        /* flashing littlefs to ESP*/
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
          Serial.printf("Upload of %s aborted invalid size %d\n", upload.filename.c_str(), upload.currentSize);
          Update.abort();
        }
      }
      else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          webServer.uploadSuccess = true;
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
          somfy.commit();
        }
        else {
          somfy.commit();
          Update.printError(Serial);
        }
      }
      esp_task_wdt_reset();
    });
  server.on("/scanaps", []() {
    webServer.sendCORSHeaders(server);
    esp_task_wdt_reset();
    
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    esp_task_wdt_delete(NULL);
    if(net.softAPOpened) WiFi.disconnect(false);
    int n = WiFi.scanNetworks(false, true);
    esp_task_wdt_add(NULL);
    
    Serial.print("Scanned ");
    Serial.print(n);
    Serial.println(" networks");
    // Ok we need to chunk this response as well.
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    resp.beginObject("connected");
    resp.addElem("name", settings.WIFI.ssid);
    resp.addElem("passphrase", settings.WIFI.passphrase);
    resp.addElem("strength", (int32_t)WiFi.RSSI());
    resp.addElem("channel", (int32_t)WiFi.channel());
    resp.endObject();
    resp.beginArray("accessPoints");
    for(int i = 0; i < n; ++i) {
      if(WiFi.SSID(i).length() == 0 || WiFi.RSSI(i) < -95) continue; // Ignore hidden and weak networks that we cannot connect to anyway.
      resp.beginObject();
      resp.addElem("name", WiFi.SSID(i).c_str());
      resp.addElem("channel", (int32_t)WiFi.channel(i));
      resp.addElem("strength", (int32_t)WiFi.RSSI(i));
      resp.addElem("macAddress", WiFi.BSSIDstr(i).c_str());
      resp.endObject();
    }
    resp.endArray();
    resp.endObject();
    resp.endResponse();
    });
  server.on("/reboot", []() { webServer.handleReboot(server);});
  server.on("/saveSecurity", []() {
    webServer.sendCORSHeaders(server);
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
        char token[65];
        webServer.createAPIToken(server.client().remoteIP(), token);
        obj["apiKey"] = token;
        DynamicJsonDocument sdoc(1024);
        JsonObject sobj = sdoc.to<JsonObject>();
        settings.Security.toJSON(sobj);
        serializeJson(sdoc, g_content);
        server.send(200, _encoding_json, g_content);
      }
      else {
        server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
      }
    }
    });
  server.on("/getSecurity", []() {
    webServer.sendCORSHeaders(server);
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();
    settings.Security.toJSON(obj);
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    });
  server.on("/saveRadio", []() {
    webServer.sendCORSHeaders(server);
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
        JsonResponse resp;
        resp.beginResponse(&server, g_content, sizeof(g_content));
        resp.beginObject();
        somfy.transceiver.toJSON(resp);
        resp.endObject();
        resp.endResponse();
      }
      else {
        server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
      }
    }
    });
  server.on("/getRadio", []() {
    webServer.sendCORSHeaders(server);
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    somfy.transceiver.toJSON(resp);
    resp.endObject();
    resp.endResponse();
    });
  server.on("/sendRemoteCommand", []() {
    webServer.sendCORSHeaders(server);
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
          webServer.handleDeserializationError(server, err);
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
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    DynamicJsonDocument doc(512);
    
    Serial.print("Plain: ");
    Serial.print(server.method());
    Serial.println(server.arg("plain"));
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      webServer.handleDeserializationError(server, err);
      return;
    }
    else {
      JsonObject obj = doc.as<JsonObject>();
      HTTPMethod method = server.method();
      if (method == HTTP_POST || method == HTTP_PUT) {
        // Parse out all the inputs.
        if (obj.containsKey("hostname") || obj.containsKey("ssdpBroadcast") || obj.containsKey("checkForUpdate")) {
          bool checkForUpdate = settings.checkForUpdate;
          settings.fromJSON(obj);
          settings.save();
          if(settings.checkForUpdate != checkForUpdate) git.emitUpdateCheck();
          if(obj.containsKey("hostname")) net.updateHostname();
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
    webServer.sendCORSHeaders(server);
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
          settings.connType = static_cast<conn_types_t>(obj["connType"].as<uint8_t>());
          settings.save();
          reboot = true;
        }
        if(obj.containsKey("wifi")) {
          JsonObject objWifi = obj["wifi"];
          if(settings.connType == conn_types_t::wifi) {
            if(objWifi.containsKey("ssid") && objWifi["ssid"].as<String>().compareTo(settings.WIFI.ssid) != 0) {
              if(WiFi.softAPgetStationNum() == 0) reboot = true;
            }
            if(objWifi.containsKey("passphrase") && objWifi["passphrase"].as<String>().compareTo(settings.WIFI.passphrase) != 0) {
              if(WiFi.softAPgetStationNum() == 0) reboot = true;
            }
          }
          settings.WIFI.fromJSON(objWifi);
          settings.WIFI.save();
        }
        if(obj.containsKey("ethernet"))
        {
          JsonObject objEth = obj["ethernet"];
          // This is an ethernet connection so if anything changes we need to reboot.
          if(settings.connType == conn_types_t::ethernet || settings.connType == conn_types_t::ethernetpref)
            reboot = true;
          settings.Ethernet.fromJSON(objEth);
          settings.Ethernet.save();
        }
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
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    Serial.println("Setting IP...");
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      webServer.handleDeserializationError(server, err);
      return;
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
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    Serial.println("Settings WIFI connection...");
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      webServer.handleDeserializationError(server, err);
      return;
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
    webServer.sendCORSHeaders(server);
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    resp.addElem("fwVersion", settings.fwVersion.name);
    settings.toJSON(resp);
    settings.NTP.toJSON(resp);
    resp.endObject();
    resp.endResponse();
    /*
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();
    doc["fwVersion"] = settings.fwVersion.name;
    settings.toJSON(obj);
    //settings.Ethernet.toJSON(obj);
    //settings.WIFI.toJSON(obj);
    settings.NTP.toJSON(obj);
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    */
    });
  server.on("/networksettings", []() {
    webServer.sendCORSHeaders(server);
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    settings.toJSON(resp);
    resp.addElem("fwVersion", settings.fwVersion.name);
    resp.beginObject("ethernet");
    settings.Ethernet.toJSON(resp);
    resp.endObject();
    resp.beginObject("wifi");
    settings.WIFI.toJSON(resp);
    resp.endObject();
    resp.beginObject("ip");
    settings.IP.toJSON(resp);
    resp.endObject();
    resp.endObject();
    resp.endResponse();
    
    /*
    DynamicJsonDocument doc(2048);
    JsonObject obj = doc.to<JsonObject>();
    doc["fwVersion"] = settings.fwVersion.name;
    settings.toJSON(obj);
    JsonObject eth = obj.createNestedObject("ethernet");
    settings.Ethernet.toJSON(eth);
    JsonObject wifi = obj.createNestedObject("wifi");
    settings.WIFI.toJSON(wifi);
    JsonObject ip = obj.createNestedObject("ip");
    settings.IP.toJSON(ip);
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    */
    });
  server.on("/connectmqtt", []() {
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      webServer.handleDeserializationError(server, err);
      return;
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
        JsonResponse resp;
        resp.beginResponse(&server, g_content, sizeof(g_content));
        resp.beginObject();
        settings.MQTT.toJSON(resp);
        resp.endObject();
        resp.endResponse();
        /*
        DynamicJsonDocument sdoc(1024);
        JsonObject sobj = sdoc.to<JsonObject>();
        settings.MQTT.toJSON(sobj);
        serializeJson(sdoc, g_content);
        server.send(200, _encoding_json, g_content);
        */
      }
      else {
        server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
      }
    }
    });
  server.on("/mqttsettings", []() {
    webServer.sendCORSHeaders(server);
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    settings.MQTT.toJSON(resp);
    resp.endObject();
    resp.endResponse();
    
    /*
    DynamicJsonDocument doc(1024);
    JsonObject obj = doc.to<JsonObject>();
    settings.MQTT.toJSON(obj);
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    */
    });
  server.on("/roomSortOrder", []() {
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    DynamicJsonDocument doc(512);
    Serial.print("Plain: ");
    Serial.print(server.method());
    Serial.println(server.arg("plain"));
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      webServer.handleDeserializationError(server, err);
      return;
    }
    else {
      JsonArray arr = doc.as<JsonArray>();
      HTTPMethod method = server.method();
      if (method == HTTP_POST || method == HTTP_PUT) {
        // Parse out all the inputs.
        uint8_t order = 0;
        for(JsonVariant v : arr) {
          uint8_t roomId = v.as<uint8_t>();
          if (roomId != 0) {
            SomfyRoom *room = somfy.getRoomById(roomId);
            if(room) room->sortOrder = order++;
          }
        }
        server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set room order\"}");
      }
      else {
        server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
      }
    }
  });
  server.on("/shadeSortOrder", []() {
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    DynamicJsonDocument doc(512);
    Serial.print("Plain: ");
    Serial.print(server.method());
    Serial.println(server.arg("plain"));
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      webServer.handleDeserializationError(server, err);
      return;
    }
    else {
      JsonArray arr = doc.as<JsonArray>();
      HTTPMethod method = server.method();
      if (method == HTTP_POST || method == HTTP_PUT) {
        // Parse out all the inputs.
        uint8_t order = 0;
        for(JsonVariant v : arr) {
          uint8_t shadeId = v.as<uint8_t>();
          if (shadeId != 255) {
            SomfyShade *shade = somfy.getShadeById(shadeId);
            if(shade) shade->sortOrder = order++;
          }
        }
        server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set shade order\"}");
      }
      else {
        server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
      }
    }
  });
  server.on("/groupSortOrder", []() {
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    DynamicJsonDocument doc(512);
    Serial.print("Plain: ");
    Serial.print(server.method());
    Serial.println(server.arg("plain"));
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      webServer.handleDeserializationError(server, err);
      return;
    }
    else {
      JsonArray arr = doc.as<JsonArray>();
      HTTPMethod method = server.method();
      if (method == HTTP_POST || method == HTTP_PUT) {
        // Parse out all the inputs.
        uint8_t order = 0;
        for(JsonVariant v : arr) {
          uint8_t groupId = v.as<uint8_t>();
          if (groupId != 255) {
            SomfyGroup *group = somfy.getGroupById(groupId);
            if(group) group->sortOrder = order++;
          }
        }
        server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set group order\"}");
      }
      else {
        server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
      }
    }
  });  
  server.on("/beginFrequencyScan", []() {
    webServer.sendCORSHeaders(server);
    somfy.transceiver.beginFrequencyScan();
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    somfy.transceiver.toJSON(resp);
    resp.endObject();
    resp.endResponse();
    /*
    DynamicJsonDocument doc(1024);
    JsonObject obj = doc.to<JsonObject>();
    somfy.transceiver.toJSON(obj);
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    */
  });
  server.on("/endFrequencyScan", []() {
    webServer.sendCORSHeaders(server);
    somfy.transceiver.endFrequencyScan();
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    somfy.transceiver.toJSON(resp);
    resp.endObject();
    resp.endResponse();
    /*
    DynamicJsonDocument doc(1024);
    JsonObject obj = doc.to<JsonObject>();
    somfy.transceiver.toJSON(obj);
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    */
  });
  server.on("/recoverFilesystem", [] () {
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    webServer.sendCORSHeaders(server);
    if(git.status == GIT_UPDATING)
      server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Filesystem is updating.  Please wait!!!\"}");
    else if(git.status != GIT_STATUS_READY)
      server.send(200, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Cannot recover file system at this time.\"}");
    else {
      git.recoverFilesystem();
      server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Recovering filesystem from github please wait!!!\"}");
    }
  });
  server.begin();
  apiServer.begin();
}
