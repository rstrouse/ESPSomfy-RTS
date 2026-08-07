#include <WiFi.h>
#include <LittleFS.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include "esp_log.h"
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
#include "ESPNetwork.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <memory>

extern ConfigSettings settings;
extern SSDPClass SSDP;
extern rebootDelay_t rebootDelay;
extern SomfyShadeController somfy;
extern Web webServer;
extern MQTTClass mqtt;
extern GitUpdater git;
extern ESPNetwork net;

//#define WEB_MAX_RESPONSE 34768
#define WEB_MAX_RESPONSE 4096
static char g_async_content[WEB_MAX_RESPONSE];


// General responses
static const char _response_404[] = "404: Service Not Found";


// Encodings
static const char _encoding_text[] = "text/plain";
static const char _encoding_html[] = "text/html";
static const char _encoding_json[] = "application/json";

static const char *TAG = "Web";

static QueueHandle_t webCmdQueue = nullptr;
static SemaphoreHandle_t webCmdDone = nullptr;

AsyncWebServer asyncServer(80);
AsyncWebServer asyncApiServer(8081);
void Web::startup() {
  ESP_LOGI(TAG, "Launching web server...");
  if(!webCmdQueue) webCmdQueue = xQueueCreate(WEB_CMD_QUEUE_SIZE, sizeof(web_command_t));
  if(!webCmdDone) webCmdDone = xSemaphoreCreateBinary();

  asyncServer.on("/loginContext", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot().to<JsonObject>();
    root["type"] = static_cast<uint8_t>(settings.Security.type);
    root["permissions"] = settings.Security.permissions;
    root["serverId"] = settings.serverId;
    root["version"] = settings.fwVersion.name;
    root["model"] = "ESPSomfyRTS";
    root["hostname"] = settings.hostname;    
    response->setLength();
    request->send(response);
  });
  asyncApiServer.begin();
  ESP_LOGI(TAG, "Async API server started on port 8081");
}
void Web::loop() {
  this->processQueue();
  delay(1);
}
bool Web::queueCommand(const web_command_t &cmd) {
  if(!webCmdQueue || !webCmdDone) return false;
  // Clear any stale signal
  xSemaphoreTake(webCmdDone, 0);
  if(xQueueSend(webCmdQueue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGE(TAG, "Command queue full, dropping command");
    return false;
  }
  // Wait for main loop to process it
  if(xSemaphoreTake(webCmdDone, pdMS_TO_TICKS(WEB_CMD_TIMEOUT_MS)) != pdTRUE) {
    ESP_LOGW(TAG, "Command queue timeout waiting for processing");
    return false;
  }
  return true;
}
void Web::processQueue() {
  if(!webCmdQueue || !webCmdDone) return;
  web_command_t cmd;
  while(xQueueReceive(webCmdQueue, &cmd, 0) == pdTRUE) {
    switch(cmd.type) {
      case web_cmd_t::shade_command: {
        SomfyShade *shade = somfy.getShadeById(cmd.shadeId);
        if(shade) {
          if(cmd.target <= 100) shade->moveToTarget(shade->transformPosition(cmd.target));
          else shade->sendCommand(cmd.command, cmd.repeat > 0 ? cmd.repeat : shade->repeats, cmd.stepSize);
        }
        break;
      }
      case web_cmd_t::group_command: {
        SomfyGroup *group = somfy.getGroupById(cmd.groupId);
        if(group) group->sendCommand(cmd.command, cmd.repeat >= 0 ? cmd.repeat : group->repeats, cmd.stepSize);
        break;
      }
      case web_cmd_t::tilt_command: {
        SomfyShade *shade = somfy.getShadeById(cmd.shadeId);
        if(shade) {
          if(cmd.target <= 100) shade->moveToTiltTarget(shade->transformPosition(cmd.target));
          else shade->sendTiltCommand(cmd.command);
        }
        break;
      }
      case web_cmd_t::shade_repeat: {
        SomfyShade *shade = somfy.getShadeById(cmd.shadeId);
        if(shade) {
          if(shade->shadeType == shade_types::garage1 && cmd.command == somfy_commands::Prog) cmd.command = somfy_commands::Toggle;
          if(!shade->isLastCommand(cmd.command)) shade->sendCommand(cmd.command, cmd.repeat >= 0 ? cmd.repeat : shade->repeats, cmd.stepSize);
          else shade->repeatFrame(cmd.repeat >= 0 ? cmd.repeat : shade->repeats);
        }
        break;
      }
      case web_cmd_t::group_repeat: {
        SomfyGroup *group = somfy.getGroupById(cmd.groupId);
        if(group) {
          if(!group->isLastCommand(cmd.command)) group->sendCommand(cmd.command, cmd.repeat >= 0 ? cmd.repeat : group->repeats, cmd.stepSize);
          else group->repeatFrame(cmd.repeat >= 0 ? cmd.repeat : group->repeats);
        }
        break;
      }
      case web_cmd_t::set_positions: {
        SomfyShade *shade = somfy.getShadeById(cmd.shadeId);
        if(shade) {
          if(cmd.position >= 0) shade->target = shade->currentPos = cmd.position;
          if(cmd.tiltPosition >= 0 && shade->tiltType != tilt_types::none) shade->tiltTarget = shade->currentTiltPos = cmd.tiltPosition;
          shade->emitState();
        }
        break;
      }
      case web_cmd_t::shade_sensor: {
        SomfyShade *shade = somfy.getShadeById(cmd.shadeId);
        if(shade) {
          shade->sendSensorCommand(cmd.windy, cmd.sunny, cmd.repeat >= 0 ? (uint8_t)cmd.repeat : shade->repeats);
          shade->emitState();
        }
        break;
      }
      case web_cmd_t::group_sensor: {
        SomfyGroup *group = somfy.getGroupById(cmd.groupId);
        if(group) {
          group->sendSensorCommand(cmd.windy, cmd.sunny, cmd.repeat >= 0 ? (uint8_t)cmd.repeat : group->repeats);
          group->emitState();
        }
        break;
      }
    }
    xSemaphoreGive(webCmdDone);
  }
}
bool Web::isAuthenticated(AsyncWebServerRequest *request, bool cfg) {
  ESP_LOGD(TAG, "Checking async authentication");
  if(settings.Security.type == security_types::None) return true;
  else if(!cfg && (settings.Security.permissions & static_cast<uint8_t>(security_permissions::ConfigOnly)) == 0x01) return true;
  else if(request->hasHeader("apikey")) {
    ESP_LOGD(TAG, "Checking API Key...");
    char token[65];
    memset(token, 0x00, sizeof(token));
    this->createAPIToken(request->client()->remoteIP(), token);
    if(String(token) != request->getHeader("apikey")->value()) {
      request->send(401, _encoding_text, "Unauthorized API Key");
      return false;
    }
    // Key is valid
  }
  else {
    ESP_LOGE(TAG, "Not authenticated...");
    request->send(401, _encoding_text, "Unauthorized API Key");
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
    token[0] = '\0';
    for(int i = 0; i < sizeof(hmacResult); i++){
        char str[3];
        sprintf(str, "%02x", (int)hmacResult[i]);
        strcat(token, str);
    }
    ESP_LOGD(TAG, "Hash: %s", token);
    return true;
}
bool Web::createAPIToken(const IPAddress ipAddress, char *token) {
    String payload;
    if(settings.Security.type == security_types::Password) createAPIPasswordToken(ipAddress, settings.Security.username, settings.Security.password, token);
    else if(settings.Security.type == security_types::PinEntry) createAPIPinToken(ipAddress, settings.Security.pin, token);
    else createAPIToken(ipAddress.toString().c_str(), token);
    return true;
}
// =====================================================
// Async API Handlers
// =====================================================
// Helper: get a query param as String, or empty if missing
static String asyncParam(AsyncWebServerRequest *request, const char *name) {
  if(request->hasParam(name)) return request->getParam(name)->value();
  return String();
}
static bool asyncHasParam(AsyncWebServerRequest *request, const char *name) {
  return request->hasParam(name);
}

// -- Serialization helpers (accept JsonFormatter& so both sync and async can use them) --
static void serializeRoom(SomfyRoom *room, JsonFormatter &json) {
  json.addElem("roomId", room->roomId);
  json.addElem("name", room->name);
  json.addElem("sortOrder", room->sortOrder);
}
static void serializeShadeRef(SomfyShade *shade, JsonFormatter &json) {
  json.addElem("shadeId", shade->getShadeId());
  json.addElem("roomId", shade->roomId);
  json.addElem("name", shade->name);
  json.addElem("remoteAddress", (uint32_t)shade->getRemoteAddress());
  json.addElem("paired", shade->paired);
  json.addElem("shadeType", static_cast<uint8_t>(shade->shadeType));
  json.addElem("flipCommands", shade->flipCommands);
  json.addElem("flipPosition", shade->flipCommands);
  json.addElem("bitLength", shade->bitLength);
  json.addElem("proto", static_cast<uint8_t>(shade->proto));
  json.addElem("flags", shade->flags);
  json.addElem("sunSensor", shade->hasSunSensor());
  json.addElem("hasLight", shade->hasLight());
  json.addElem("repeats", shade->repeats);
}
static void serializeShade(SomfyShade *shade, JsonFormatter &json) {
  json.addElem("shadeId", shade->getShadeId());
  json.addElem("roomId", shade->roomId);
  json.addElem("name", shade->name);
  json.addElem("remoteAddress", (uint32_t)shade->getRemoteAddress());
  json.addElem("upTime", (uint32_t)shade->upTime);
  json.addElem("downTime", (uint32_t)shade->downTime);
  json.addElem("paired", shade->paired);
  json.addElem("lastRollingCode", (uint32_t)shade->lastRollingCode);
  json.addElem("position", shade->transformPosition(shade->currentPos));
  json.addElem("tiltType", static_cast<uint8_t>(shade->tiltType));
  json.addElem("tiltPosition", shade->transformPosition(shade->currentTiltPos));
  json.addElem("tiltDirection", shade->tiltDirection);
  json.addElem("tiltTime", (uint32_t)shade->tiltTime);
  json.addElem("stepSize", (uint32_t)shade->stepSize);
  json.addElem("tiltTarget", shade->transformPosition(shade->tiltTarget));
  json.addElem("target", shade->transformPosition(shade->target));
  json.addElem("myPos", shade->transformPosition(shade->myPos));
  json.addElem("myTiltPos", shade->transformPosition(shade->myTiltPos));
  json.addElem("direction", shade->direction);
  json.addElem("shadeType", static_cast<uint8_t>(shade->shadeType));
  json.addElem("bitLength", shade->bitLength);
  json.addElem("proto", static_cast<uint8_t>(shade->proto));
  json.addElem("flags", shade->flags);
  json.addElem("flipCommands", shade->flipCommands);
  json.addElem("flipPosition", shade->flipPosition);
  json.addElem("inGroup", shade->isInGroup());
  json.addElem("sunSensor", shade->hasSunSensor());
  json.addElem("light", shade->hasLight());
  json.addElem("repeats", shade->repeats);
  json.addElem("sortOrder", shade->sortOrder);
  json.addElem("gpioUp", shade->gpioUp);
  json.addElem("gpioDown", shade->gpioDown);
  json.addElem("gpioMy", shade->gpioMy);
  json.addElem("gpioLLTrigger", ((shade->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0) ? false : true);
  json.addElem("simMy", shade->simMy());
  json.beginArray("linkedRemotes");
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    SomfyLinkedRemote &lremote = shade->linkedRemotes[i];
    if(lremote.getRemoteAddress() != 0) {
      json.beginObject();
      json.addElem("remoteAddress", (uint32_t)lremote.getRemoteAddress());
      json.addElem("lastRollingCode", (uint32_t)lremote.lastRollingCode);
      json.endObject();
    }
  }
  json.endArray();
}
static void serializeGroupRef(SomfyGroup *group, JsonFormatter &json) {
  group->updateFlags();
  json.addElem("groupId", group->getGroupId());
  json.addElem("roomId", group->roomId);
  json.addElem("name", group->name);
  json.addElem("remoteAddress", (uint32_t)group->getRemoteAddress());
  json.addElem("lastRollingCode", (uint32_t)group->lastRollingCode);
  json.addElem("bitLength", group->bitLength);
  json.addElem("proto", static_cast<uint8_t>(group->proto));
  json.addElem("sunSensor", group->hasSunSensor());
  json.addElem("flipCommands", group->flipCommands);
  json.addElem("flags", group->flags);
  json.addElem("repeats", group->repeats);
  json.addElem("sortOrder", group->sortOrder);
}
static void serializeGroup(SomfyGroup *group, JsonFormatter &json) {
  serializeGroupRef(group, json);
  json.beginArray("linkedShades");
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    uint8_t shadeId = group->linkedShades[i];
    if(shadeId > 0 && shadeId < 255) {
      SomfyShade *shade = somfy.getShadeById(shadeId);
      if(shade) {
        json.beginObject();
        serializeShadeRef(shade, json);
        json.endObject();
      }
    }
  }
  json.endArray();
}
static void serializeRooms(JsonFormatter &json) {
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    SomfyRoom *room = &somfy.rooms[i];
    if(room->roomId != 0) {
      json.beginObject();
      serializeRoom(room, json);
      json.endObject();
    }
  }
}
static void serializeShades(JsonFormatter &json) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade &shade = somfy.shades[i];
    if(shade.getShadeId() != 255) {
      json.beginObject();
      serializeShade(&shade, json);
      json.endObject();
    }
  }
}
static void serializeGroups(JsonFormatter &json) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup &group = somfy.groups[i];
    if(group.getGroupId() != 255) {
      json.beginObject();
      serializeGroup(&group, json);
      json.endObject();
    }
  }
}
static void serializeRepeaters(JsonFormatter &json) {
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(somfy.repeaters[i] != 0) json.addElem((uint32_t)somfy.repeaters[i]);
  }
}
static void serializeTransceiverConfig(JsonFormatter &json) {
  auto &cfg = somfy.transceiver.config;
  json.addElem("type", cfg.type);
  json.addElem("TXPin", cfg.TXPin);
  json.addElem("RXPin", cfg.RXPin);
  json.addElem("SCKPin", cfg.SCKPin);
  json.addElem("MOSIPin", cfg.MOSIPin);
  json.addElem("MISOPin", cfg.MISOPin);
  json.addElem("CSNPin", cfg.CSNPin);
  json.addElem("rxBandwidth", cfg.rxBandwidth);
  json.addElem("frequency", cfg.frequency);
  json.addElem("deviation", cfg.deviation);
  json.addElem("txPower", cfg.txPower);
  json.addElem("proto", static_cast<uint8_t>(cfg.proto));
  json.addElem("enabled", cfg.enabled);
  json.addElem("noiseDetection", cfg.noiseDetection);
  json.addElem("radioInit", cfg.radioInit);
}
static void serializeAppVersion(JsonFormatter &json, appver_t &ver) {
  json.addElem("name", ver.name);
  json.addElem("major", ver.major);
  json.addElem("minor", ver.minor);
  json.addElem("build", ver.build);
  json.addElem("suffix", ver.suffix);
}
static void serializeGitVersion(JsonFormatter &json) {
  json.addElem("available", git.updateAvailable);
  json.addElem("status", git.status);
  json.addElem("error", (int32_t)git.error);
  json.addElem("cancelled", git.cancelled);
  json.addElem("checkForUpdate", settings.checkForUpdate);
  json.addElem("inetAvailable", git.inetAvailable);
  json.beginObject("fwVersion");
  serializeAppVersion(json, settings.fwVersion);
  json.endObject();
  json.beginObject("appVersion");
  serializeAppVersion(json, settings.appVersion);
  json.endObject();
  json.beginObject("latest");
  serializeAppVersion(json, git.latest);
  json.endObject();
}
static void serializeGitRelease(GitRelease *rel, JsonFormatter &json) {
  Timestamp ts;
  char buff[20];
  sprintf(buff, "%llu", rel->id);
  json.addElem("id", buff);
  json.addElem("name", rel->name);
  json.addElem("date", ts.getISOTime(rel->releaseDate));
  json.addElem("draft", rel->draft);
  json.addElem("preRelease", rel->preRelease);
  json.addElem("main", rel->main);
  json.addElem("hasFS", rel->hasFS);
  json.addElem("hwVersions", rel->hwVersions);
  json.beginObject("version");
  serializeAppVersion(json, rel->version);
  json.endObject();
}

// Memory-bounded streaming builder for /controller. Produces the JSON payload
// one small unit at a time into a fixed 3KB buffer, so a chunked HTTP response
// can drain it as the TCP send window allows — no growing cbuf in the async
// response stream.
class ControllerChunker {
public:
  enum : uint8_t { S_HEADER, S_ROOMS, S_SHADES, S_GROUPS, S_REPEATERS, S_DONE };
  static constexpr uint16_t LS_OPEN = 0xFFFF;

  BufferedJsonFormatter fmt;
  char unit[3072];
  size_t unitLen = 0;
  size_t consumed = 0;
  uint8_t section = S_HEADER;
  uint16_t idx = 0;
  uint16_t lsIdx = LS_OPEN;
  bool firstInArray = true;
  bool firstInLSArray = true;

  size_t generate(uint8_t *out, size_t maxLen) {
    size_t written = 0;
    while(written < maxLen) {
      if(consumed < unitLen) {
        size_t n = unitLen - consumed;
        if(n > maxLen - written) n = maxLen - written;
        memcpy(out + written, unit + consumed, n);
        consumed += n;
        written += n;
      } else {
        produceNext();
        if(unitLen == 0) return written;
      }
    }
    return written;
  }

private:
  void resetFmt(size_t offset = 0) {
    unit[offset] = 0;
    fmt.setBuffer(unit + offset, sizeof(unit) - offset);
  }

  void produceNext() {
    consumed = 0;
    unitLen = 0;
    switch(section) {
      case S_HEADER: {
        resetFmt();
        fmt.beginObject();
        fmt.addElem("maxRooms", (uint8_t)SOMFY_MAX_ROOMS);
        fmt.addElem("maxShades", (uint8_t)SOMFY_MAX_SHADES);
        fmt.addElem("maxGroups", (uint8_t)SOMFY_MAX_GROUPS);
        fmt.addElem("maxGroupedShades", (uint8_t)SOMFY_MAX_GROUPED_SHADES);
        fmt.addElem("maxLinkedRemotes", (uint8_t)SOMFY_MAX_LINKED_REMOTES);
        fmt.addElem("startingAddress", (uint32_t)somfy.startingAddress);
        fmt.beginObject("transceiver");
        fmt.beginObject("config");
        serializeTransceiverConfig(fmt);
        fmt.endObject();
        fmt.endObject();
        fmt.beginObject("version");
        serializeGitVersion(fmt);
        fmt.endObject();
        fmt.beginArray("rooms");
        unitLen = strlen(unit);
        section = S_ROOMS;
        idx = 0;
        firstInArray = true;
        return;
      }
      case S_ROOMS: {
        while(idx < SOMFY_MAX_ROOMS && somfy.rooms[idx].roomId == 0) idx++;
        if(idx >= SOMFY_MAX_ROOMS) {
          strcpy(unit, "],\"shades\":[");
          unitLen = strlen(unit);
          section = S_SHADES;
          idx = 0;
          firstInArray = true;
          return;
        }
        size_t pos = 0;
        if(!firstInArray) unit[pos++] = ',';
        resetFmt(pos);
        fmt.beginObject();
        serializeRoom(&somfy.rooms[idx], fmt);
        fmt.endObject();
        unitLen = pos + strlen(unit + pos);
        idx++;
        firstInArray = false;
        return;
      }
      case S_SHADES: {
        while(idx < SOMFY_MAX_SHADES && somfy.shades[idx].getShadeId() == 255) idx++;
        if(idx >= SOMFY_MAX_SHADES) {
          strcpy(unit, "],\"groups\":[");
          unitLen = strlen(unit);
          section = S_GROUPS;
          idx = 0;
          lsIdx = LS_OPEN;
          firstInArray = true;
          return;
        }
        size_t pos = 0;
        if(!firstInArray) unit[pos++] = ',';
        resetFmt(pos);
        fmt.beginObject();
        serializeShade(&somfy.shades[idx], fmt);
        fmt.endObject();
        unitLen = pos + strlen(unit + pos);
        idx++;
        firstInArray = false;
        return;
      }
      case S_GROUPS: {
        while(idx < SOMFY_MAX_GROUPS && somfy.groups[idx].getGroupId() == 255) idx++;
        if(idx >= SOMFY_MAX_GROUPS) {
          strcpy(unit, "],\"repeaters\":[");
          unitLen = strlen(unit);
          section = S_REPEATERS;
          idx = 0;
          firstInArray = true;
          return;
        }
        SomfyGroup *g = &somfy.groups[idx];
        if(lsIdx == LS_OPEN) {
          size_t pos = 0;
          if(!firstInArray) unit[pos++] = ',';
          resetFmt(pos);
          fmt.beginObject();
          serializeGroupRef(g, fmt);
          fmt.beginArray("linkedShades");
          unitLen = pos + strlen(unit + pos);
          lsIdx = 0;
          firstInArray = false;
          firstInLSArray = true;
          return;
        }
        SomfyShade *lshade = nullptr;
        while(lsIdx < SOMFY_MAX_GROUPED_SHADES) {
          uint8_t sid = g->linkedShades[lsIdx];
          if(sid > 0 && sid < 255) {
            lshade = somfy.getShadeById(sid);
            if(lshade) break;
          }
          lsIdx++;
        }
        if(!lshade) {
          strcpy(unit, "]}");
          unitLen = 2;
          idx++;
          lsIdx = LS_OPEN;
          return;
        }
        size_t pos = 0;
        if(!firstInLSArray) unit[pos++] = ',';
        resetFmt(pos);
        fmt.beginObject();
        serializeShadeRef(lshade, fmt);
        fmt.endObject();
        unitLen = pos + strlen(unit + pos);
        lsIdx++;
        firstInLSArray = false;
        return;
      }
      case S_REPEATERS: {
        while(idx < SOMFY_MAX_REPEATERS && somfy.repeaters[idx] == 0) idx++;
        if(idx >= SOMFY_MAX_REPEATERS) {
          strcpy(unit, "]}");
          unitLen = 2;
          section = S_DONE;
          return;
        }
        size_t pos = 0;
        if(!firstInArray) unit[pos++] = ',';
        pos += snprintf(unit + pos, sizeof(unit) - pos, "%lu", (unsigned long)somfy.repeaters[idx]);
        unitLen = pos;
        idx++;
        firstInArray = false;
        return;
      }
      case S_DONE:
      default:
        unitLen = 0;
        return;
    }
  }
};

// -- Async handler implementations --
void Web::handleDiscovery(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_POST || request->method() == HTTP_GET) {
    ESP_LOGD(TAG, "Async Discovery Requested");
    char connType[10] = "Unknown";
    if(net.connType == conn_types_t::ethernet) strcpy(connType, "Ethernet");
    else if(net.connType == conn_types_t::wifi) strcpy(connType, "Wifi");
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
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
    resp.addElem("uptime", (uint64_t)millis());
    resp.endObject();
    resp.beginArray("rooms");
    serializeRooms(resp);
    resp.endArray();
    resp.beginArray("shades");
    serializeShades(resp);
    resp.endArray();
    resp.beginArray("groups");
    serializeGroups(resp);
    resp.endArray();
    resp.endObject();
    resp.endResponse();
    net.needsBroadcast = true;
  }
  else
    request->send(500, _encoding_text, "Invalid http method");
}
void Web::handleGetRooms(AsyncWebServerRequest *request) {
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_POST || request->method() == HTTP_GET) {
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginArray();
    serializeRooms(resp);
    resp.endArray();
    resp.endResponse();
  }
  else request->send(404, _encoding_text, _response_404);
}
void Web::handleGetShades(AsyncWebServerRequest *request) {
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_POST || request->method() == HTTP_GET) {
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginArray();
    serializeShades(resp);
    resp.endArray();
    resp.endResponse();
  }
  else request->send(404, _encoding_text, _response_404);
}
void Web::handleGetGroups(AsyncWebServerRequest *request) {
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_POST || request->method() == HTTP_GET) {
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginArray();
    serializeGroups(resp);
    resp.endArray();
    resp.endResponse();
  }
  else request->send(404, _encoding_text, _response_404);
}
void Web::handleController(AsyncWebServerRequest *request) {
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_POST || request->method() == HTTP_GET) {
    settings.printAvailHeap();
    auto state = std::make_shared<ControllerChunker>();
    AsyncWebServerResponse *response = request->beginChunkedResponse(_encoding_json,
      [state](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        return state->generate(buffer, maxLen);
      });
    request->send(response);
  }
  else request->send(404, _encoding_text, _response_404);
}
void Web::handleLogin(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  char token[65];
  memset(&token, 0x00, sizeof(token));
  this->createAPIToken(request->client()->remoteIP(), token);
  if(settings.Security.type == security_types::None) {
    snprintf(g_async_content, sizeof(g_async_content),
      "{\"type\":%u,\"apiKey\":\"%s\",\"msg\":\"Success\",\"success\":true}",
      static_cast<uint8_t>(settings.Security.type), token);
    request->send(200, _encoding_json, g_async_content);
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
  bool success = false;
  if(settings.Security.type == security_types::PinEntry) {
    char ptok[65];
    memset(ptok, 0x00, sizeof(ptok));
    this->createAPIPinToken(request->client()->remoteIP(), pin, ptok);
    if(String(ptok) == String(token)) success = true;
  }
  else if(settings.Security.type == security_types::Password) {
    char ptok[65];
    memset(ptok, 0x00, sizeof(ptok));
    this->createAPIPasswordToken(request->client()->remoteIP(), username, password, ptok);
    if(String(ptok) == String(token)) success = true;
  }
  if(success) {
    snprintf(g_async_content, sizeof(g_async_content),
      "{\"type\":%u,\"apiKey\":\"%s\",\"msg\":\"Success\",\"success\":true}",
      static_cast<uint8_t>(settings.Security.type), token);
    request->send(200, _encoding_json, g_async_content);
  }
  else {
    snprintf(g_async_content, sizeof(g_async_content),
      "{\"type\":%u,\"msg\":\"Invalid credentials\",\"success\":false}",
      static_cast<uint8_t>(settings.Security.type));
    request->send(401, _encoding_json, g_async_content);
  }
}
void Web::handleShadeCommand(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  uint8_t shadeId = 255;
  uint8_t target = 255;
  uint8_t stepSize = 0;
  int8_t repeat = -1;
  somfy_commands command = somfy_commands::My;
  // Try query params
  if(asyncHasParam(request, "shadeId")) {
    shadeId = asyncParam(request, "shadeId").toInt();
    if(asyncHasParam(request, "command")) command = translateSomfyCommand(asyncParam(request, "command"));
    else if(asyncHasParam(request, "target")) target = asyncParam(request, "target").toInt();
    if(asyncHasParam(request, "repeat")) repeat = asyncParam(request, "repeat").toInt();
    if(asyncHasParam(request, "stepSize")) stepSize = asyncParam(request, "stepSize").toInt();
  }
  else if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"];
    else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}")); return; }
    if(!obj["command"].isNull()) { String scmd = obj["command"]; command = translateSomfyCommand(scmd); }
    else if(!obj["target"].isNull()) target = obj["target"].as<uint8_t>();
    if(!obj["repeat"].isNull()) repeat = obj["repeat"].as<uint8_t>();
    if(!obj["stepSize"].isNull()) stepSize = obj["stepSize"].as<uint8_t>();
  }
  else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}")); return; }
  SomfyShade *shade = somfy.getShadeById(shadeId);
  if(shade) {
    ESP_LOGI(TAG, "handleShadeCommand shade=%u target=%u command=%s", shadeId, target, translateSomfyCommand(command).c_str());
    web_command_t cmd = {};
    cmd.type = web_cmd_t::shade_command;
    cmd.shadeId = shadeId;
    cmd.target = target;
    cmd.command = command;
    cmd.repeat = repeat;
    cmd.stepSize = stepSize;
    this->queueCommand(cmd);
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeShadeRef(shade, resp);
    resp.endObject();
    resp.endResponse();
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
}
void Web::handleGroupCommand(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  uint8_t groupId = 255;
  uint8_t stepSize = 0;
  int8_t repeat = -1;
  somfy_commands command = somfy_commands::My;
  if(asyncHasParam(request, "groupId")) {
    groupId = asyncParam(request, "groupId").toInt();
    if(asyncHasParam(request, "command")) command = translateSomfyCommand(asyncParam(request, "command"));
    if(asyncHasParam(request, "repeat")) repeat = asyncParam(request, "repeat").toInt();
    if(asyncHasParam(request, "stepSize")) stepSize = asyncParam(request, "stepSize").toInt();
  }
  else if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["groupId"].isNull()) groupId = obj["groupId"];
    else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}")); return; }
    if(!obj["command"].isNull()) { String scmd = obj["command"]; command = translateSomfyCommand(scmd); }
    if(!obj["repeat"].isNull()) repeat = obj["repeat"].as<uint8_t>();
    if(!obj["stepSize"].isNull()) stepSize = obj["stepSize"].as<uint8_t>();
  }
  else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}")); return; }
  SomfyGroup *group = somfy.getGroupById(groupId);
  if(group) {
    ESP_LOGI(TAG, "handleGroupCommand group=%u command=%s", groupId, translateSomfyCommand(command).c_str());
    web_command_t cmd = {};
    cmd.type = web_cmd_t::group_command;
    cmd.groupId = groupId;
    cmd.command = command;
    cmd.repeat = repeat;
    cmd.stepSize = stepSize;
    this->queueCommand(cmd);
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeGroupRef(group, resp);
    resp.endObject();
    resp.endResponse();
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group with the specified id not found.\"}"));
}
void Web::handleTiltCommand(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  uint8_t shadeId = 255;
  uint8_t target = 255;
  somfy_commands command = somfy_commands::My;
  if(asyncHasParam(request, "shadeId")) {
    shadeId = asyncParam(request, "shadeId").toInt();
    if(asyncHasParam(request, "command")) command = translateSomfyCommand(asyncParam(request, "command"));
    else if(asyncHasParam(request, "target")) target = asyncParam(request, "target").toInt();
  }
  else if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"];
    else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}")); return; }
    if(!obj["command"].isNull()) { String scmd = obj["command"]; command = translateSomfyCommand(scmd); }
    else if(!obj["target"].isNull()) target = obj["target"].as<uint8_t>();
  }
  else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}")); return; }
  SomfyShade *shade = somfy.getShadeById(shadeId);
  if(shade) {
    ESP_LOGI(TAG, "handleTiltCommand shade=%u target=%u command=%s", shadeId, target, translateSomfyCommand(command).c_str());
    web_command_t cmd = {};
    cmd.type = web_cmd_t::tilt_command;
    cmd.shadeId = shadeId;
    cmd.target = target;
    cmd.command = command;
    this->queueCommand(cmd);
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeShadeRef(shade, resp);
    resp.endObject();
    resp.endResponse();
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
}
void Web::handleRepeatCommand(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  uint8_t shadeId = 255;
  uint8_t groupId = 255;
  uint8_t stepSize = 0;
  int8_t repeat = -1;
  somfy_commands command = somfy_commands::My;
  if(asyncHasParam(request, "shadeId")) shadeId = asyncParam(request, "shadeId").toInt();
  else if(asyncHasParam(request, "groupId")) groupId = asyncParam(request, "groupId").toInt();
  if(asyncHasParam(request, "command")) command = translateSomfyCommand(asyncParam(request, "command"));
  if(asyncHasParam(request, "repeat")) repeat = asyncParam(request, "repeat").toInt();
  if(asyncHasParam(request, "stepSize")) stepSize = asyncParam(request, "stepSize").toInt();
  if(shadeId == 255 && groupId == 255 && !json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"];
    if(!obj["groupId"].isNull()) groupId = obj["groupId"];
    if(!obj["stepSize"].isNull()) stepSize = obj["stepSize"];
    if(!obj["command"].isNull()) { String scmd = obj["command"]; command = translateSomfyCommand(scmd); }
    if(!obj["repeat"].isNull()) repeat = obj["repeat"].as<uint8_t>();
  }
  if(shadeId != 255) {
    ESP_LOGI(TAG, "handleRepeatCommand shade=%u command=%s", shadeId, translateSomfyCommand(command).c_str());
    SomfyShade *shade = somfy.getShadeById(shadeId);
    if(!shade) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade reference could not be found.\"}")); return; }
    web_command_t cmd = {};
    cmd.type = web_cmd_t::shade_repeat;
    cmd.shadeId = shadeId;
    cmd.command = command;
    cmd.repeat = repeat;
    cmd.stepSize = stepSize;
    this->queueCommand(cmd);
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginArray();
    serializeShadeRef(shade, resp);
    resp.endArray();
    resp.endResponse();
  }
  else if(groupId != 255) {
    ESP_LOGI(TAG, "handleRepeatCommand group=%u command=%s", groupId, translateSomfyCommand(command).c_str());
    SomfyGroup *group = somfy.getGroupById(groupId);
    if(!group) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group reference could not be found.\"}")); return; }
    web_command_t cmd = {};
    cmd.type = web_cmd_t::group_repeat;
    cmd.groupId = groupId;
    cmd.command = command;
    cmd.repeat = repeat;
    cmd.stepSize = stepSize;
    this->queueCommand(cmd);
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeGroupRef(group, resp);
    resp.endObject();
    resp.endResponse();
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleRoom(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_GET) {
    if(asyncHasParam(request, "roomId")) {
      int roomId = asyncParam(request, "roomId").toInt();
      SomfyRoom *room = somfy.getRoomById(roomId);
      if(room) {
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeRoom(room, resp);
        resp.endObject();
        resp.endResponse();
      }
      else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Room Id not found.\"}"));
    }
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid room id.\"}"));
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleShade(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_GET) {
    if(asyncHasParam(request, "shadeId")) {
      int shadeId = asyncParam(request, "shadeId").toInt();
      SomfyShade *shade = somfy.getShadeById(shadeId);
      if(shade) {
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeShade(shade, resp);
        resp.endObject();
        resp.endResponse();
      }
      else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
    }
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid shade id.\"}"));
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleGroup(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_GET) {
    if(asyncHasParam(request, "groupId")) {
      int groupId = asyncParam(request, "groupId").toInt();
      SomfyGroup *group = somfy.getGroupById(groupId);
      if(group) {
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeGroup(group, resp);
        resp.endObject();
        resp.endResponse();
      }
      else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
    }
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid group id.\"}"));
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleSetPositions(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  uint8_t shadeId = asyncHasParam(request, "shadeId") ? asyncParam(request, "shadeId").toInt() : 255;
  int8_t pos = asyncHasParam(request, "position") ? asyncParam(request, "position").toInt() : -1;
  int8_t tiltPos = asyncHasParam(request, "tiltPosition") ? asyncParam(request, "tiltPosition").toInt() : -1;
  if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"];
    if(!obj["position"].isNull()) pos = obj["position"];
    if(!obj["tiltPosition"].isNull()) tiltPos = obj["tiltPosition"];
  }
  if(shadeId != 255) {
    ESP_LOGI(TAG, "handleSetPositions shade=%u pos=%d tiltPos=%d", shadeId, pos, tiltPos);
    SomfyShade *shade = somfy.getShadeById(shadeId);
    if(shade) {
      web_command_t cmd = {};
      cmd.type = web_cmd_t::set_positions;
      cmd.shadeId = shadeId;
      cmd.position = pos;
      cmd.tiltPosition = tiltPos;
      this->queueCommand(cmd);
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeShade(shade, resp);
      resp.endObject();
      resp.endResponse();
    }
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"An invalid shadeId was provided\"}"));
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"shadeId was not provided\"}"));
}
void Web::handleSetSensor(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  uint8_t shadeId = asyncHasParam(request, "shadeId") ? asyncParam(request, "shadeId").toInt() : 255;
  uint8_t groupId = asyncHasParam(request, "groupId") ? asyncParam(request, "groupId").toInt() : 255;
  int8_t sunny = asyncHasParam(request, "sunny") ? (toBoolean(asyncParam(request, "sunny").c_str(), false) ? 1 : 0) : -1;
  int8_t windy = asyncHasParam(request, "windy") ? asyncParam(request, "windy").toInt() : -1;
  int8_t repeat = asyncHasParam(request, "repeat") ? asyncParam(request, "repeat").toInt() : -1;
  if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"].as<uint8_t>();
    if(!obj["groupId"].isNull()) groupId = obj["groupId"].as<uint8_t>();
    if(!obj["sunny"].isNull()) {
      if(obj["sunny"].is<bool>()) sunny = obj["sunny"].as<bool>() ? 1 : 0;
      else sunny = obj["sunny"].as<int8_t>();
    }
    if(!obj["windy"].isNull()) {
      if(obj["windy"].is<bool>()) windy = obj["windy"].as<bool>() ? 1 : 0;
      else windy = obj["windy"].as<int8_t>();
    }
    if(!obj["repeat"].isNull()) repeat = obj["repeat"].as<uint8_t>();
  }
  if(shadeId != 255) {
    SomfyShade *shade = somfy.getShadeById(shadeId);
    if(shade) {
      web_command_t cmd = {};
      cmd.type = web_cmd_t::shade_sensor;
      cmd.shadeId = shadeId;
      cmd.sunny = sunny;
      cmd.windy = windy;
      cmd.repeat = repeat;
      this->queueCommand(cmd);
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeShade(shade, resp);
      resp.endObject();
      resp.endResponse();
    }
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"An invalid shadeId was provided\"}"));
  }
  else if(groupId != 255) {
    SomfyGroup *group = somfy.getGroupById(groupId);
    if(group) {
      web_command_t cmd = {};
      cmd.type = web_cmd_t::group_sensor;
      cmd.groupId = groupId;
      cmd.sunny = sunny;
      cmd.windy = windy;
      cmd.repeat = repeat;
      this->queueCommand(cmd);
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeGroup(group, resp);
      resp.endObject();
      resp.endResponse();
    }
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"An invalid groupId was provided\"}"));
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"shadeId was not provided\"}"));
}
void Web::handleDownloadFirmware(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  GitRepo repo;
  GitRelease *rel = nullptr;
  int8_t err = repo.getReleases();
  ESP_LOGI(TAG, "Async downloadFirmware called...");
  if(err == 0) {
    if(asyncHasParam(request, "ver")) {
      String ver = asyncParam(request, "ver");
      if(ver == "latest") rel = &repo.releases[0];
      else if(ver == "main") rel = &repo.releases[GIT_MAX_RELEASES];
      else {
        for(uint8_t i = 0; i < GIT_MAX_RELEASES; i++) {
          if(repo.releases[i].id == 0) continue;
          if(strcmp(repo.releases[i].name, ver.c_str()) == 0) { rel = &repo.releases[i]; break; }
        }
      }
      if(rel) {
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeGitRelease(rel, resp);
        resp.endObject();
        resp.endResponse();
        strcpy(git.targetRelease, rel->name);
        git.status = GIT_AWAITING_UPDATE;
      }
      else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Release not found in repo.\"}"));
    }
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Release version not supplied.\"}"));
  }
  else request->send(err, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error communicating with Github.\"}"));
}
void Web::handleBackup(AsyncWebServerRequest *request) {
  if(!this->isAuthenticated(request)) return;
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
    response->addHeader("Access-Control-Expose-Headers", "Content-Disposition");
    request->send(response);
  }
  else {
    request->send(200, _encoding_text, somfy.backupData);
  }
}
void Web::handleReboot(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_POST || request->method() == HTTP_PUT) {
    ESP_LOGI(TAG, "Async Rebooting ESP...");
    rebootDelay.reboot = true;
    rebootDelay.rebootTime = millis() + 500;
    request->send(200, _encoding_json, "{\"status\":\"OK\",\"desc\":\"Successfully started reboot\"}");
  }
  else request->send(201, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method\"}");
}
void Web::handleNotFound(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  snprintf(g_async_content, sizeof(g_async_content), "404 Service Not Found: %s", request->url().c_str());
  request->send(404, _encoding_text, g_async_content);
}

void Web::begin() {
  ESP_LOGI(TAG, "Creating Web MicroServices...");
  // Async API Server (port 8081)
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "PUT,POST,GET,OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");
  // GET endpoints
  asyncApiServer.on("/discovery", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleDiscovery(r); });
  asyncApiServer.on("/rooms", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleGetRooms(r); });
  asyncApiServer.on("/shades", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleGetShades(r); });
  asyncApiServer.on("/groups", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleGetGroups(r); });
  asyncApiServer.on("/controller", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleController(r); });
  asyncApiServer.on("/room", HTTP_GET, [](AsyncWebServerRequest *r) { webServer.handleRoom(r); });
  asyncApiServer.on("/shade", HTTP_GET, [](AsyncWebServerRequest *r) { webServer.handleShade(r); });
  asyncApiServer.on("/group", HTTP_GET, [](AsyncWebServerRequest *r) { webServer.handleGroup(r); });
  asyncApiServer.on("/downloadFirmware", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleDownloadFirmware(r); });
  asyncApiServer.on("/backup", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleBackup(r); });
  asyncApiServer.on("/reboot", WebRequestMethodComposite(HTTP_POST) | HTTP_PUT, [](AsyncWebServerRequest *r) { webServer.handleReboot(r); });
  // JSON body endpoints
  asyncApiServer.addHandler(new AsyncCallbackJsonWebHandler("/shadeCommand",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleShadeCommand(r, j); }));
  asyncApiServer.addHandler(new AsyncCallbackJsonWebHandler("/groupCommand",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleGroupCommand(r, j); }));
  asyncApiServer.addHandler(new AsyncCallbackJsonWebHandler("/tiltCommand",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleTiltCommand(r, j); }));
  asyncApiServer.addHandler(new AsyncCallbackJsonWebHandler("/repeatCommand",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleRepeatCommand(r, j); }));
  asyncApiServer.addHandler(new AsyncCallbackJsonWebHandler("/setPositions",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleSetPositions(r, j); }));
  asyncApiServer.addHandler(new AsyncCallbackJsonWebHandler("/setSensor",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleSetSensor(r, j); }));
  asyncApiServer.addHandler(new AsyncCallbackJsonWebHandler("/login",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleLogin(r, j); }));
  // GET fallback for command endpoints (query params)
  asyncApiServer.on("/shadeCommand", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleShadeCommand(r, v); });
  asyncApiServer.on("/groupCommand", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleGroupCommand(r, v); });
  asyncApiServer.on("/tiltCommand", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleTiltCommand(r, v); });
  asyncApiServer.on("/repeatCommand", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleRepeatCommand(r, v); });
  asyncApiServer.on("/setPositions", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleSetPositions(r, v); });
  asyncApiServer.on("/setSensor", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleSetSensor(r, v); });
  asyncApiServer.on("/login", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleLogin(r, v); });
  // OPTIONS preflight + not found
  asyncApiServer.onNotFound([](AsyncWebServerRequest *r) {
    if(r->method() == HTTP_OPTIONS) { r->send(200); return; }
    webServer.handleNotFound(r);
  });

  // Web Interface

  // Web Interface
  // Command endpoints - delegate to async handler methods
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/tiltCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleTiltCommand(request, json); }));
  asyncServer.on("/tiltCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleTiltCommand(request, v); });

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/repeatCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleRepeatCommand(request, json); }));
  asyncServer.on("/repeatCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleRepeatCommand(request, v); });

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/shadeCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleShadeCommand(request, json); }));
  asyncServer.on("/shadeCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleShadeCommand(request, v); });

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/groupCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleGroupCommand(request, json); }));
  asyncServer.on("/groupCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleGroupCommand(request, v); });

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setPositions",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleSetPositions(request, json); }));
  asyncServer.on("/setPositions", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleSetPositions(request, v); });

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setSensor",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleSetSensor(request, json); }));
  asyncServer.on("/setSensor", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleSetSensor(request, v); });

  asyncServer.on("/upnp.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("text/xml");
    SSDP.schema(*response);
    request->send(response);
  });

  // /  and /loginContext are already handled by serveStatic and startup()

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/login",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleLogin(request, json); }));
  asyncServer.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleLogin(request, v); });

  asyncServer.on("/shades.cfg", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/shades.cfg", _encoding_text);
  });
  asyncServer.on("/shades.tmp", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/shades.tmp", _encoding_text);
  });

  asyncServer.on("/getReleases", HTTP_GET, [](AsyncWebServerRequest *request) {
    GitRepo repo;
    repo.getReleases();
    git.setCurrentRelease(repo);
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    // Inline GitRepo::toJSON
    resp.beginObject("fwVersion");
    serializeAppVersion(resp, settings.fwVersion);
    resp.endObject();
    resp.beginObject("appVersion");
    serializeAppVersion(resp, settings.appVersion);
    resp.endObject();
    resp.beginArray("releases");
    for(uint8_t i = 0; i < GIT_MAX_RELEASES + 1; i++) {
      if(repo.releases[i].id == 0) continue;
      resp.beginObject();
      serializeGitRelease(&repo.releases[i], resp);
      resp.endObject();
    }
    resp.endArray();
    resp.endObject();
    resp.endResponse();
  });

  asyncServer.on("/downloadFirmware", HTTP_GET, [](AsyncWebServerRequest *request) { webServer.handleDownloadFirmware(request); });

  asyncServer.on("/cancelFirmware", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!git.lockFS) {
      git.status = GIT_UPDATE_CANCELLING;
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeGitVersion(resp);
      resp.endObject();
      resp.endResponse();
      git.cancelled = true;
    }
    else {
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Cannot cancel during filesystem update.\"}"));
    }
  });

  asyncServer.on("/backup", HTTP_GET, [](AsyncWebServerRequest *request) { webServer.handleBackup(request); });

  asyncServer.on("/restore", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if(webServer.uploadSuccess) {
        request->send(200, _encoding_json, "{\"status\":\"Success\",\"desc\":\"Restoring Shade settings\"}");
        restore_options_t opts;
        if(asyncHasParam(request, "data")) {
          String dataStr = asyncParam(request, "data");
          ESP_LOGD(TAG, "%s", dataStr.c_str());
          JsonDocument doc;
          DeserializationError err = deserializeJson(doc, dataStr);
          if(err) {
            request->send(500, "application/json", "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
            return;
          }
          else {
            JsonObject obj = doc.as<JsonObject>();
            opts.fromJSON(obj);
          }
        }
        else {
          ESP_LOGD(TAG, "No restore options sent.  Using defaults...");
          opts.shades = true;
        }
        ShadeConfigFile::restore(&somfy, "/shades.tmp", opts);
        ESP_LOGI(TAG, "Rebooting ESP for restored settings...");
        rebootDelay.reboot = true;
        rebootDelay.rebootTime = millis() + 1000;
      }
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      esp_task_wdt_reset();
      if(index == 0) {
        webServer.uploadSuccess = false;
        ESP_LOGD(TAG, "Restore: %s", filename.c_str());
        File fup = LittleFS.open("/shades.tmp", "w");
        fup.close();
      }
      if(len > 0) {
        File fup = LittleFS.open("/shades.tmp", "a");
        fup.write(data, len);
        fup.close();
      }
      if(final) {
        webServer.uploadSuccess = true;
      }
    });

  // Static file routes removed - handled by serveStatic in startup()

  asyncServer.on("/controller", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleController(request); });
  asyncServer.on("/rooms", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleGetRooms(request); });
  asyncServer.on("/shades", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleGetShades(request); });
  asyncServer.on("/groups", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleGetGroups(request); });
  asyncServer.on("/room", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleRoom(request); });
  asyncServer.on("/shade", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleShade(request); });
  asyncServer.on("/group", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleGroup(request); });

  asyncServer.on("/getNextRoom", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    resp.addElem("roomId", somfy.getNextRoomId());
    resp.endObject();
    resp.endResponse();
  });

  asyncServer.on("/getNextShade", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint8_t shadeId = somfy.getNextShadeId();
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    resp.addElem("shadeId", shadeId);
    resp.addElem("remoteAddress", (uint32_t)somfy.getNextRemoteAddress(shadeId));
    resp.addElem("bitLength", somfy.transceiver.config.type);
    resp.addElem("stepSize", (uint8_t)100);
    resp.addElem("proto", static_cast<uint8_t>(somfy.transceiver.config.proto));
    resp.endObject();
    resp.endResponse();
  });

  asyncServer.on("/getNextGroup", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint8_t groupId = somfy.getNextGroupId();
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    resp.addElem("groupId", groupId);
    resp.addElem("remoteAddress", (uint32_t)somfy.getNextRemoteAddress(groupId));
    resp.addElem("bitLength", somfy.transceiver.config.type);
    resp.addElem("proto", static_cast<uint8_t>(somfy.transceiver.config.proto));
    resp.endObject();
    resp.endResponse();
  });

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/addRoom",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room object supplied.\"}")); return; }
      ESP_LOGD(TAG, "Adding a room");
      JsonObject obj = json.as<JsonObject>();
      if(somfy.roomCount() > SOMFY_MAX_ROOMS) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Maximum number of rooms exceeded.\"}"));
        return;
      }
      SomfyRoom *room = somfy.addRoom(obj);
      if(room) {
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeRoom(room, resp);
        resp.endObject();
        resp.endResponse();
      }
      else {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error adding room.\"}"));
      }
    }));

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/addShade",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}")); return; }
      ESP_LOGD(TAG, "Adding a shade");
      JsonObject obj = json.as<JsonObject>();
      if(somfy.shadeCount() > SOMFY_MAX_SHADES) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Maximum number of shades exceeded.\"}"));
        return;
      }
      SomfyShade *shade = somfy.addShade(obj);
      if(shade) {
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeShade(shade, resp);
        resp.endObject();
        resp.endResponse();
      }
      else {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error adding shade.\"}"));
      }
    }));

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/addGroup",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}")); return; }
      ESP_LOGD(TAG, "Adding a group");
      JsonObject obj = json.as<JsonObject>();
      if(somfy.groupCount() > SOMFY_MAX_GROUPS) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Maximum number of groups exceeded.\"}"));
        return;
      }
      SomfyGroup *group = somfy.addGroup(obj);
      if(group) {
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeGroup(group, resp);
        resp.endObject();
        resp.endResponse();
      }
      else {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error adding group.\"}"));
      }
    }));

  asyncServer.on("/groupOptions", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(asyncHasParam(request, "groupId")) {
      int groupId = atoi(asyncParam(request, "groupId").c_str());
      SomfyGroup* group = somfy.getGroupById(groupId);
      if(group) {
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeGroup(group, resp);
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
              serializeShadeRef(shade, resp);
              resp.endObject();
            }
          }
        }
        resp.endArray();
        resp.endObject();
        resp.endResponse();
      }
      else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
    }
    else {
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid group id.\"}"));
    }
  });

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/saveRoom",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room object supplied.\"}")); return; }
      ESP_LOGD(TAG, "Updating a room");
      JsonObject obj = json.as<JsonObject>();
      if(!obj["roomId"].isNull()) {
        SomfyRoom* room = somfy.getRoomById(obj["roomId"]);
        if(room) {
          room->fromJSON(obj);
          room->save();
          AsyncJsonResp resp;
          resp.beginResponse(request, g_async_content, sizeof(g_async_content));
          resp.beginObject();
          serializeRoom(room, resp);
          resp.endObject();
          resp.endResponse();
        }
        else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Room Id not found.\"}"));
      }
      else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room id was supplied.\"}"));
    }));

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/saveShade",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}")); return; }
      ESP_LOGD(TAG, "Updating a shade");
      JsonObject obj = json.as<JsonObject>();
      if(!obj["shadeId"].isNull()) {
        SomfyShade* shade = somfy.getShadeById(obj["shadeId"]);
        if(shade) {
          int8_t err = shade->fromJSON(obj);
          if(err == 0) {
            shade->save();
            AsyncJsonResp resp;
            resp.beginResponse(request, g_async_content, sizeof(g_async_content));
            resp.beginObject();
            serializeShade(shade, resp);
            resp.endObject();
            resp.endResponse();
          }
          else {
            snprintf(g_async_content, sizeof(g_async_content), "{\"status\":\"DATA\",\"desc\":\"Data Error.\", \"code\":%d}", err);
            request->send(500, _encoding_json, g_async_content);
          }
        }
        else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
      }
      else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
    }));

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/saveGroup",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}")); return; }
      ESP_LOGD(TAG, "Updating a group");
      JsonObject obj = json.as<JsonObject>();
      if(!obj["groupId"].isNull()) {
        SomfyGroup* group = somfy.getGroupById(obj["groupId"]);
        if(group) {
          group->fromJSON(obj);
          group->save();
          AsyncJsonResp resp;
          resp.beginResponse(request, g_async_content, sizeof(g_async_content));
          resp.beginObject();
          serializeGroup(group, resp);
          resp.endObject();
          resp.endResponse();
        }
        else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
      }
      else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
    }));

  // setMyPosition - supports both GET with query params and POST/PUT with JSON body
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setMyPosition",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      uint8_t shadeId = 255;
      int8_t pos = -1;
      int8_t tilt = -1;
      if(!json.isNull()) {
        JsonObject obj = json.as<JsonObject>();
        if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"];
        else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}")); return; }
        if(!obj["pos"].isNull()) pos = obj["pos"].as<int8_t>();
        if(!obj["tilt"].isNull()) tilt = obj["tilt"].as<int8_t>();
      }
      SomfyShade* shade = somfy.getShadeById(shadeId);
      if(shade) {
        if(tilt < 0) tilt = shade->myPos;
        if(shade->tiltType == tilt_types::none) tilt = -1;
        if(pos >= 0 && pos <= 100)
          shade->setMyPosition(shade->transformPosition(pos), shade->transformPosition(tilt));
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeShadeRef(shade, resp);
        resp.endObject();
        resp.endResponse();
      }
      else {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
      }
    }));
  asyncServer.on("/setMyPosition", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint8_t shadeId = 255;
    int8_t pos = -1;
    int8_t tilt = -1;
    if(asyncHasParam(request, "shadeId")) {
      shadeId = atoi(asyncParam(request, "shadeId").c_str());
      if(asyncHasParam(request, "pos")) pos = atoi(asyncParam(request, "pos").c_str());
      if(asyncHasParam(request, "tilt")) tilt = atoi(asyncParam(request, "tilt").c_str());
    }
    else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}")); return; }
    SomfyShade* shade = somfy.getShadeById(shadeId);
    if(shade) {
      if(tilt < 0) tilt = shade->myPos;
      if(shade->tiltType == tilt_types::none) tilt = -1;
      if(pos >= 0 && pos <= 100)
        shade->setMyPosition(shade->transformPosition(pos), shade->transformPosition(tilt));
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeShadeRef(shade, resp);
      resp.endObject();
      resp.endResponse();
    }
    else {
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
    }
  });

  // setRollingCode - supports both query params and JSON body
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setRollingCode",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      uint8_t shadeId = 255;
      uint16_t rollingCode = 0;
      if(!json.isNull()) {
        JsonObject obj = json.as<JsonObject>();
        if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"];
        if(!obj["rollingCode"].isNull()) rollingCode = obj["rollingCode"];
      }
      SomfyShade* shade = nullptr;
      if(shadeId != 255) shade = somfy.getShadeById(shadeId);
      if(!shade) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade not found to set rolling code\"}"));
      }
      else {
        shade->setRollingCode(rollingCode);
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeShade(shade, resp);
        resp.endObject();
        resp.endResponse();
      }
    }));
  asyncServer.on("/setRollingCode", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint8_t shadeId = 255;
    uint16_t rollingCode = 0;
    if(asyncHasParam(request, "shadeId")) {
      shadeId = atoi(asyncParam(request, "shadeId").c_str());
      rollingCode = atoi(asyncParam(request, "rollingCode").c_str());
    }
    SomfyShade* shade = nullptr;
    if(shadeId != 255) shade = somfy.getShadeById(shadeId);
    if(!shade) {
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade not found to set rolling code\"}"));
    }
    else {
      shade->setRollingCode(rollingCode);
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeShade(shade, resp);
      resp.endObject();
      resp.endResponse();
    }
  });

  // setPaired - supports both query params and JSON body
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setPaired",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      uint8_t shadeId = 255;
      bool paired = false;
      if(!json.isNull()) {
        JsonObject obj = json.as<JsonObject>();
        if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"];
        if(!obj["paired"].isNull()) paired = obj["paired"];
      }
      SomfyShade* shade = nullptr;
      if(shadeId != 255) shade = somfy.getShadeById(shadeId);
      if(!shade) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade not found to pair\"}"));
      }
      else {
        shade->paired = paired;
        shade->save();
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeShade(shade, resp);
        resp.endObject();
        resp.endResponse();
      }
    }));
  asyncServer.on("/setPaired", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint8_t shadeId = 255;
    bool paired = false;
    if(asyncHasParam(request, "shadeId"))
      shadeId = atoi(asyncParam(request, "shadeId").c_str());
    if(asyncHasParam(request, "paired"))
      paired = toBoolean(asyncParam(request, "paired").c_str(), false);
    SomfyShade* shade = nullptr;
    if(shadeId != 255) shade = somfy.getShadeById(shadeId);
    if(!shade) {
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade not found to pair\"}"));
    }
    else {
      shade->paired = paired;
      shade->save();
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeShade(shade, resp);
      resp.endObject();
      resp.endResponse();
    }
  });

  // unpairShade
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/unpairShade",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      uint8_t shadeId = 255;
      if(!json.isNull()) {
        JsonObject obj = json.as<JsonObject>();
        if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"];
      }
      SomfyShade* shade = nullptr;
      if(shadeId != 255) shade = somfy.getShadeById(shadeId);
      if(!shade) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade not found to unpair\"}"));
      }
      else {
        if(shade->bitLength == 56)
          shade->sendCommand(somfy_commands::Prog, 7);
        else
          shade->sendCommand(somfy_commands::Prog, 1);
        shade->paired = false;
        shade->save();
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeShade(shade, resp);
        resp.endObject();
        resp.endResponse();
      }
    }));
  asyncServer.on("/unpairShade", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint8_t shadeId = 255;
    if(asyncHasParam(request, "shadeId"))
      shadeId = atoi(asyncParam(request, "shadeId").c_str());
    SomfyShade* shade = nullptr;
    if(shadeId != 255) shade = somfy.getShadeById(shadeId);
    if(!shade) {
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade not found to unpair\"}"));
    }
    else {
      if(shade->bitLength == 56)
        shade->sendCommand(somfy_commands::Prog, 7);
      else
        shade->sendCommand(somfy_commands::Prog, 1);
      shade->paired = false;
      shade->save();
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeShade(shade, resp);
      resp.endObject();
      resp.endResponse();
    }
  });

  // linkRepeater
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/linkRepeater",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      uint32_t address = 0;
      if(!json.isNull()) {
        ESP_LOGD(TAG, "Linking a repeater");
        JsonObject obj = json.as<JsonObject>();
        if(!obj["address"].isNull()) address = obj["address"];
        else if(!obj["remoteAddress"].isNull()) address = obj["remoteAddress"];
      }
      if(address == 0) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No repeater address was supplied.\"}"));
      }
      else {
        somfy.linkRepeater(address);
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginArray();
        serializeRepeaters(resp);
        resp.endArray();
        resp.endResponse();
      }
    }));
  asyncServer.on("/linkRepeater", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint32_t address = 0;
    if(asyncHasParam(request, "address"))
      address = atoi(asyncParam(request, "address").c_str());
    if(address == 0) {
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No repeater address was supplied.\"}"));
    }
    else {
      somfy.linkRepeater(address);
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginArray();
      serializeRepeaters(resp);
      resp.endArray();
      resp.endResponse();
    }
  });

  // unlinkRepeater
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/unlinkRepeater",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      uint32_t address = 0;
      if(!json.isNull()) {
        ESP_LOGD(TAG, "Unlinking a repeater");
        JsonObject obj = json.as<JsonObject>();
        if(!obj["address"].isNull()) address = obj["address"];
        else if(!obj["remoteAddress"].isNull()) address = obj["remoteAddress"];
      }
      if(address == 0) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No repeater address was supplied.\"}"));
      }
      else {
        somfy.unlinkRepeater(address);
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginArray();
        serializeRepeaters(resp);
        resp.endArray();
        resp.endResponse();
      }
    }));
  asyncServer.on("/unlinkRepeater", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint32_t address = 0;
    if(asyncHasParam(request, "address"))
      address = atoi(asyncParam(request, "address").c_str());
    if(address == 0) {
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No repeater address was supplied.\"}"));
    }
    else {
      somfy.unlinkRepeater(address);
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginArray();
      serializeRepeaters(resp);
      resp.endArray();
      resp.endResponse();
    }
  });

  // unlinkRemote
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/unlinkRemote",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No remote object supplied.\"}")); return; }
      JsonObject obj = json.as<JsonObject>();
      if(!obj["shadeId"].isNull()) {
        SomfyShade* shade = somfy.getShadeById(obj["shadeId"]);
        if(shade) {
          if(!obj["remoteAddress"].isNull()) {
            shade->unlinkRemote(obj["remoteAddress"]);
          }
          else {
            request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Remote address not provided.\"}"));
          }
          AsyncJsonResp resp;
          resp.beginResponse(request, g_async_content, sizeof(g_async_content));
          resp.beginObject();
          serializeShade(shade, resp);
          resp.endObject();
          resp.endResponse();
        }
        else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
      }
      else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
    }));

  // linkRemote
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/linkRemote",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No remote object supplied.\"}")); return; }
      ESP_LOGD(TAG, "Linking a remote");
      JsonObject obj = json.as<JsonObject>();
      if(!obj["shadeId"].isNull()) {
        SomfyShade* shade = somfy.getShadeById(obj["shadeId"]);
        if(shade) {
          if(!obj["remoteAddress"].isNull()) {
            if(!obj["rollingCode"].isNull()) shade->linkRemote(obj["remoteAddress"], obj["rollingCode"]);
            else shade->linkRemote(obj["remoteAddress"]);
          }
          else {
            request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Remote address not provided.\"}"));
          }
          AsyncJsonResp resp;
          resp.beginResponse(request, g_async_content, sizeof(g_async_content));
          resp.beginObject();
          serializeShade(shade, resp);
          resp.endObject();
          resp.endResponse();
        }
        else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
      }
      else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
    }));

  // linkToGroup
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/linkToGroup",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No linking object supplied.\"}")); return; }
      ESP_LOGD(TAG, "Linking a shade to a group");
      JsonObject obj = json.as<JsonObject>();
      uint8_t shadeId = !obj["shadeId"].isNull() ? obj["shadeId"] : 0;
      uint8_t groupId = !obj["groupId"].isNull() ? obj["groupId"] : 0;
      if(groupId == 0) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group id not provided.\"}"));
        return;
      }
      if(shadeId == 0) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not provided.\"}"));
        return;
      }
      SomfyGroup *group = somfy.getGroupById(groupId);
      if(!group) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group id not found.\"}"));
        return;
      }
      SomfyShade *shade = somfy.getShadeById(shadeId);
      if(!shade) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not found.\"}"));
        return;
      }
      group->linkShade(shadeId);
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeGroup(group, resp);
      resp.endObject();
      resp.endResponse();
    }));

  // unlinkFromGroup
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/unlinkFromGroup",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No unlinking object supplied.\"}")); return; }
      ESP_LOGD(TAG, "Unlinking a shade from a group");
      JsonObject obj = json.as<JsonObject>();
      uint8_t shadeId = !obj["shadeId"].isNull() ? obj["shadeId"] : 0;
      uint8_t groupId = !obj["groupId"].isNull() ? obj["groupId"] : 0;
      if(groupId == 0) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group id not provided.\"}"));
        return;
      }
      if(shadeId == 0) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not provided.\"}"));
        return;
      }
      SomfyGroup *group = somfy.getGroupById(groupId);
      if(!group) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group id not found.\"}"));
        return;
      }
      SomfyShade *shade = somfy.getShadeById(shadeId);
      if(!shade) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not found.\"}"));
        return;
      }
      group->unlinkShade(shadeId);
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeGroup(group, resp);
      resp.endObject();
      resp.endResponse();
    }));

  // deleteRoom - supports GET with query params and POST/PUT with JSON body
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/deleteRoom",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      uint8_t roomId = 0;
      if(!json.isNull()) {
        ESP_LOGD(TAG, "Deleting a Room");
        JsonObject obj = json.as<JsonObject>();
        if(!obj["roomId"].isNull()) roomId = obj["roomId"];
        else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room id was supplied.\"}")); return; }
      }
      SomfyRoom* room = somfy.getRoomById(roomId);
      if(!room) request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Room with the specified id not found.\"}"));
      else {
        somfy.deleteRoom(roomId);
        request->send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Room deleted.\"}"));
      }
    }));
  asyncServer.on("/deleteRoom", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint8_t roomId = 0;
    if(asyncHasParam(request, "roomId")) {
      roomId = atoi(asyncParam(request, "roomId").c_str());
    }
    else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room object supplied.\"}")); return; }
    SomfyRoom* room = somfy.getRoomById(roomId);
    if(!room) request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Room with the specified id not found.\"}"));
    else {
      somfy.deleteRoom(roomId);
      request->send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Room deleted.\"}"));
    }
  });

  // deleteShade - supports GET with query params and POST/PUT with JSON body
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/deleteShade",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      uint8_t shadeId = 255;
      if(!json.isNull()) {
        ESP_LOGD(TAG, "Deleting a shade");
        JsonObject obj = json.as<JsonObject>();
        if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"];
        else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}")); return; }
      }
      SomfyShade* shade = somfy.getShadeById(shadeId);
      if(!shade) request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
      else if(shade->isInGroup()) {
        request->send(400, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"This shade is a member of a group and cannot be deleted.\"}"));
      }
      else {
        somfy.deleteShade(shadeId);
        request->send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Shade deleted.\"}"));
      }
    }));
  asyncServer.on("/deleteShade", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint8_t shadeId = 255;
    if(asyncHasParam(request, "shadeId")) {
      shadeId = atoi(asyncParam(request, "shadeId").c_str());
    }
    else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}")); return; }
    SomfyShade* shade = somfy.getShadeById(shadeId);
    if(!shade) request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
    else if(shade->isInGroup()) {
      request->send(400, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"This shade is a member of a group and cannot be deleted.\"}"));
    }
    else {
      somfy.deleteShade(shadeId);
      request->send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Shade deleted.\"}"));
    }
  });

  // deleteGroup - supports GET with query params and POST/PUT with JSON body
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/deleteGroup",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      uint8_t groupId = 255;
      if(!json.isNull()) {
        ESP_LOGD(TAG, "Deleting a group");
        JsonObject obj = json.as<JsonObject>();
        if(!obj["groupId"].isNull()) groupId = obj["groupId"];
        else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}")); return; }
      }
      SomfyGroup *group = somfy.getGroupById(groupId);
      if(!group) request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group with the specified id not found.\"}"));
      else {
        somfy.deleteGroup(groupId);
        request->send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Group deleted.\"}"));
      }
    }));
  asyncServer.on("/deleteGroup", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint8_t groupId = 255;
    if(asyncHasParam(request, "groupId")) {
      groupId = atoi(asyncParam(request, "groupId").c_str());
    }
    else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}")); return; }
    SomfyGroup *group = somfy.getGroupById(groupId);
    if(!group) request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group with the specified id not found.\"}"));
    else {
      somfy.deleteGroup(groupId);
      request->send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Group deleted.\"}"));
    }
  });

  // updateFirmware - file upload
  asyncServer.on("/updateFirmware", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if(Update.hasError())
        request->send(500, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Error updating firmware: \"}");
      else
        request->send(200, _encoding_json, "{\"status\":\"SUCCESS\",\"desc\":\"Successfully updated firmware\"}");
      rebootDelay.reboot = true;
      rebootDelay.rebootTime = millis() + 500;
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if(index == 0) {
        webServer.uploadSuccess = false;
        ESP_LOGI(TAG, "Update: %s", filename.c_str());
        if(!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          ESP_LOGE(TAG, "Update begin failed");
        }
        else {
          somfy.transceiver.end();
          mqtt.end();
        }
      }
      if(len > 0) {
        if(Update.write(data, len) != len) {
          ESP_LOGE(TAG, "Upload of %s aborted invalid size %d", filename.c_str(), len);
          Update.abort();
        }
      }
      if(final) {
        if(Update.end(true)) {
          ESP_LOGI(TAG, "Update Success: %u Rebooting...", index + len);
          webServer.uploadSuccess = true;
        }
        else {
          ESP_LOGE(TAG, "Update end failed");
        }
      }
      esp_task_wdt_reset();
    });

  // updateShadeConfig - file upload
  asyncServer.on("/updateShadeConfig", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if(git.lockFS) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Filesystem update in progress\"}"));
        return;
      }
      request->send(200, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Updating Shade Config: \"}");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if(index == 0) {
        ESP_LOGI(TAG, "Update: shades.cfg");
        File fup = LittleFS.open("/shades.tmp", "w");
        fup.close();
      }
      if(len > 0) {
        File fup = LittleFS.open("/shades.tmp", "a");
        fup.write(data, len);
        fup.close();
      }
      if(final) {
        somfy.loadShadesFile("/shades.tmp");
      }
    });

  // updateApplication - file upload
  asyncServer.on("/updateApplication", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if(Update.hasError())
        request->send(500, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Error updating application: \"}");
      else
        request->send(200, _encoding_json, "{\"status\":\"SUCCESS\",\"desc\":\"Successfully updated application\"}");
      rebootDelay.reboot = true;
      rebootDelay.rebootTime = millis() + 500;
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if(index == 0) {
        webServer.uploadSuccess = false;
        ESP_LOGI(TAG, "Update: %s", filename.c_str());
        if(!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
          ESP_LOGE(TAG, "Update begin failed");
        }
        else {
          somfy.transceiver.end();
          mqtt.end();
        }
      }
      if(len > 0) {
        if(Update.write(data, len) != len) {
          ESP_LOGE(TAG, "Upload of %s aborted invalid size %d", filename.c_str(), len);
          Update.abort();
        }
      }
      if(final) {
        if(Update.end(true)) {
          webServer.uploadSuccess = true;
          ESP_LOGI(TAG, "Update Success: %u Rebooting...", index + len);
          somfy.commit();
        }
        else {
          somfy.commit();
          ESP_LOGE(TAG, "Update end failed");
        }
      }
      esp_task_wdt_reset();
    });

  asyncServer.on("/scanaps", HTTP_GET, [](AsyncWebServerRequest *request) {
    esp_task_wdt_reset();
    esp_task_wdt_delete(NULL);
    if(net.softAPOpened) WiFi.disconnect(false);
    int n = WiFi.scanNetworks(false, true);
    esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "Scanned %d networks", n);
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    resp.beginObject("connected");
    resp.addElem("name", settings.WIFI.ssid);
    resp.addElem("passphrase", settings.WIFI.passphrase);
    resp.addElem("strength", (int32_t)WiFi.RSSI());
    resp.addElem("channel", (int32_t)WiFi.channel());
    resp.endObject();
    resp.beginArray("accessPoints");
    for(int i = 0; i < n; ++i) {
      if(WiFi.SSID(i).length() == 0 || WiFi.RSSI(i) < -95) continue;
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

  asyncServer.on("/reboot", WebRequestMethodComposite(HTTP_POST) | HTTP_PUT, [](AsyncWebServerRequest *request) { webServer.handleReboot(request); });

  // saveSecurity
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/saveSecurity",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(400, _encoding_html, "Error parsing JSON body");
        return;
      }
      JsonObject obj = json.as<JsonObject>();
      settings.Security.fromJSON(obj);
      settings.Security.save();
      char token[65];
      webServer.createAPIToken(request->client()->remoteIP(), token);
      JsonDocument sdoc;
      JsonObject sobj = sdoc.to<JsonObject>();
      settings.Security.toJSON(sobj);
      sobj["apiKey"] = token;
      serializeJson(sdoc, g_async_content, sizeof(g_async_content));
      request->send(200, _encoding_json, g_async_content);
    }));

  // getSecurity
  asyncServer.on("/getSecurity", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    settings.Security.toJSON(obj);
    serializeJson(doc, g_async_content, sizeof(g_async_content));
    request->send(200, _encoding_json, g_async_content);
  });

  // saveRadio
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/saveRadio",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(400, _encoding_html, "Error parsing JSON body");
        return;
      }
      JsonObject obj = json.as<JsonObject>();
      somfy.transceiver.fromJSON(obj);
      somfy.transceiver.save();
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      resp.beginObject("config");
      serializeTransceiverConfig(resp);
      resp.endObject();
      resp.endObject();
      resp.endResponse();
    }));

  // getRadio
  asyncServer.on("/getRadio", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeTransceiverConfig(resp);
    resp.endObject();
    resp.endResponse();
  });

  // sendRemoteCommand - supports GET with query params and POST/PUT with JSON body
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/sendRemoteCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      somfy_frame_t frame;
      uint8_t repeats = 0;
      if(!json.isNull()) {
        JsonObject obj = json.as<JsonObject>();
        String scmd;
        if(!obj["address"].isNull()) frame.remoteAddress = obj["address"];
        if(!obj["command"].isNull()) scmd = obj["command"].as<String>();
        if(!obj["repeats"].isNull()) repeats = obj["repeats"];
        if(!obj["rcode"].isNull()) frame.rollingCode = obj["rcode"];
        if(!obj["encKey"].isNull()) frame.encKey = obj["encKey"];
        frame.cmd = translateSomfyCommand(scmd.c_str());
      }
      if(frame.remoteAddress > 0 && frame.rollingCode > 0) {
        somfy.sendFrame(frame, repeats);
        request->send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Command Sent\"}"));
      }
      else
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No address or rolling code provided\"}"));
    }));
  asyncServer.on("/sendRemoteCommand", HTTP_GET, [](AsyncWebServerRequest *request) {
    somfy_frame_t frame;
    uint8_t repeats = 0;
    if(asyncHasParam(request, "address")) {
      frame.remoteAddress = atoi(asyncParam(request, "address").c_str());
      if(asyncHasParam(request, "encKey")) frame.encKey = atoi(asyncParam(request, "encKey").c_str());
      if(asyncHasParam(request, "command")) frame.cmd = translateSomfyCommand(asyncParam(request, "command"));
      if(asyncHasParam(request, "rcode")) frame.rollingCode = atoi(asyncParam(request, "rcode").c_str());
      if(asyncHasParam(request, "repeats")) repeats = atoi(asyncParam(request, "repeats").c_str());
    }
    if(frame.remoteAddress > 0 && frame.rollingCode > 0) {
      somfy.sendFrame(frame, repeats);
      request->send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Command Sent\"}"));
    }
    else
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No address or rolling code provided\"}"));
  });

  // setgeneral
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setgeneral",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(500, "application/json", "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
        return;
      }
      JsonObject obj = json.as<JsonObject>();
      if(!obj["hostname"].isNull() || !obj["ssdpBroadcast"].isNull() || !obj["checkForUpdate"].isNull()) {
        bool checkForUpdate = settings.checkForUpdate;
        settings.fromJSON(obj);
        settings.save();
        if(settings.checkForUpdate != checkForUpdate) git.emitUpdateCheck();
        if(!obj["hostname"].isNull()) net.updateHostname();
      }
      if(!obj["ntpServer"].isNull() || !obj["ntpServer"].isNull()) {
        settings.NTP.fromJSON(obj);
        settings.NTP.save();
      }
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set General Settings\"}");
    }));

  // setNetwork
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setNetwork",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(400, _encoding_html, "Error parsing JSON body");
        return;
      }
      JsonObject obj = json.as<JsonObject>();
      bool reboot = false;
      if(!obj["connType"].isNull() && obj["connType"].as<uint8_t>() != static_cast<uint8_t>(settings.connType)) {
        settings.connType = static_cast<conn_types_t>(obj["connType"].as<uint8_t>());
        settings.save();
        reboot = true;
      }
      if(!obj["wifi"].isNull()) {
        JsonObject objWifi = obj["wifi"];
        if(settings.connType == conn_types_t::wifi) {
          if(!objWifi["ssid"].isNull() && objWifi["ssid"].as<String>().compareTo(settings.WIFI.ssid) != 0) {
            if(WiFi.softAPgetStationNum() == 0) reboot = true;
          }
          if(!objWifi["passphrase"].isNull() && objWifi["passphrase"].as<String>().compareTo(settings.WIFI.passphrase) != 0) {
            if(WiFi.softAPgetStationNum() == 0) reboot = true;
          }
        }
        settings.WIFI.fromJSON(objWifi);
        settings.WIFI.save();
      }
      if(!obj["ethernet"].isNull()) {
        JsonObject objEth = obj["ethernet"];
        if(settings.connType == conn_types_t::ethernet || settings.connType == conn_types_t::ethernetpref)
          reboot = true;
#ifndef CONFIG_IDF_TARGET_ESP32C6
        settings.Ethernet.fromJSON(objEth);
        settings.Ethernet.save();
#endif
      }
      if(reboot) {
        ESP_LOGI(TAG, "Rebooting ESP for new Network settings...");
        rebootDelay.reboot = true;
        rebootDelay.rebootTime = millis() + 1000;
      }
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set Network Settings\"}");
    }));

  // setIP
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setIP",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(500, "application/json", "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
        return;
      }
      ESP_LOGD(TAG, "Setting IP...");
      JsonObject obj = json.as<JsonObject>();
      settings.IP.fromJSON(obj);
      settings.IP.save();
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set Network Settings\"}");
    }));

  // connectwifi
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/connectwifi",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(500, "application/json", "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
        return;
      }
      JsonObject obj = json.as<JsonObject>();
      ESP_LOGD(TAG, "Settings WIFI connection...");
      String ssid = "";
      String passphrase = "";
      if(!obj["ssid"].isNull()) ssid = obj["ssid"].as<String>();
      if(!obj["passphrase"].isNull()) passphrase = obj["passphrase"].as<String>();
      bool reboot = false;
      if(ssid.compareTo(settings.WIFI.ssid) != 0) reboot = true;
      if(passphrase.compareTo(settings.WIFI.passphrase) != 0) reboot = true;
      if(!settings.WIFI.ssidExists(ssid.c_str()) && ssid.length() > 0) {
        request->send(400, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"WiFi Network Does not exist\"}");
      }
      else {
        SETCHARPROP(settings.WIFI.ssid, ssid.c_str(), sizeof(settings.WIFI.ssid));
        SETCHARPROP(settings.WIFI.passphrase, passphrase.c_str(), sizeof(settings.WIFI.passphrase));
        settings.WIFI.save();
        settings.WIFI.print();
        request->send(201, _encoding_json, "{\"status\":\"OK\",\"desc\":\"Successfully set server connection\"}");
        if(reboot) {
          ESP_LOGI(TAG, "Rebooting ESP for new WiFi settings...");
          rebootDelay.reboot = true;
          rebootDelay.rebootTime = millis() + 1000;
        }
      }
    }));

  // modulesettings
  asyncServer.on("/modulesettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    resp.addElem("fwVersion", settings.fwVersion.name);
    resp.addElem("ssdpBroadcast", settings.ssdpBroadcast);
    resp.addElem("hostname", settings.hostname);
    resp.addElem("connType", static_cast<uint8_t>(settings.connType));
    resp.addElem("chipModel", settings.chipModel);
    resp.addElem("checkForUpdate", settings.checkForUpdate);
    resp.addElem("ntpServer", settings.NTP.ntpServer);
    resp.addElem("posixZone", settings.NTP.posixZone);
    resp.endObject();
    resp.endResponse();
  });

  // networksettings
  asyncServer.on("/networksettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    settings.toJSON(obj);
    obj["fwVersion"] = settings.fwVersion.name;
    JsonObject eth = obj.createNestedObject("ethernet");
#ifndef CONFIG_IDF_TARGET_ESP32C6
    settings.Ethernet.toJSON(eth);
#endif
    JsonObject wifi = obj.createNestedObject("wifi");
    settings.WIFI.toJSON(wifi);
    JsonObject ip = obj.createNestedObject("ip");
    settings.IP.toJSON(ip);
    serializeJson(doc, g_async_content, sizeof(g_async_content));
    request->send(200, _encoding_json, g_async_content);
  });

  // connectmqtt
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/connectmqtt",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(500, "application/json", "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
        return;
      }
      JsonObject obj = json.as<JsonObject>();
      ESP_LOGD(TAG, "Saving MQTT");
      mqtt.disconnect();
      settings.MQTT.fromJSON(obj);
      settings.MQTT.save();
      JsonDocument sdoc;
      JsonObject sobj = sdoc.to<JsonObject>();
      settings.MQTT.toJSON(sobj);
      serializeJson(sdoc, g_async_content, sizeof(g_async_content));
      request->send(200, _encoding_json, g_async_content);
    }));

  // mqttsettings
  asyncServer.on("/mqttsettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    settings.MQTT.toJSON(obj);
    serializeJson(doc, g_async_content, sizeof(g_async_content));
    request->send(200, _encoding_json, g_async_content);
  });

  // roomSortOrder
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/roomSortOrder",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(500, "application/json", "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
        return;
      }
      JsonArray arr = json.as<JsonArray>();
      uint8_t order = 0;
      for(JsonVariant v : arr) {
        uint8_t roomId = v.as<uint8_t>();
        if(roomId != 0) {
          SomfyRoom *room = somfy.getRoomById(roomId);
          if(room) room->sortOrder = order++;
        }
      }
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set room order\"}");
    }));

  // shadeSortOrder
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/shadeSortOrder",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(500, "application/json", "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
        return;
      }
      JsonArray arr = json.as<JsonArray>();
      uint8_t order = 0;
      for(JsonVariant v : arr) {
        uint8_t shadeId = v.as<uint8_t>();
        if(shadeId != 255) {
          SomfyShade *shade = somfy.getShadeById(shadeId);
          if(shade) shade->sortOrder = order++;
        }
      }
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set shade order\"}");
    }));

  // groupSortOrder
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/groupSortOrder",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(500, "application/json", "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
        return;
      }
      JsonArray arr = json.as<JsonArray>();
      uint8_t order = 0;
      for(JsonVariant v : arr) {
        uint8_t groupId = v.as<uint8_t>();
        if(groupId != 255) {
          SomfyGroup *group = somfy.getGroupById(groupId);
          if(group) group->sortOrder = order++;
        }
      }
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set group order\"}");
    }));

  asyncServer.on("/beginFrequencyScan", HTTP_GET, [](AsyncWebServerRequest *request) {
    somfy.transceiver.beginFrequencyScan();
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeTransceiverConfig(resp);
    resp.endObject();
    resp.endResponse();
  });

  asyncServer.on("/endFrequencyScan", HTTP_GET, [](AsyncWebServerRequest *request) {
    somfy.transceiver.endFrequencyScan();
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeTransceiverConfig(resp);
    resp.endObject();
    resp.endResponse();
  });

  asyncServer.on("/recoverFilesystem", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) {
    if(git.status == GIT_UPDATING)
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Filesystem is updating.  Please wait!!!\"}");
    else if(git.status != GIT_STATUS_READY)
      request->send(200, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Cannot recover file system at this time.\"}");
    else {
      git.recoverFilesystem();
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Recovering filesystem from github please wait!!!\"}");
    }
  });

  asyncServer.onNotFound([](AsyncWebServerRequest *r) {
    if(r->method() == HTTP_OPTIONS) { r->send(200); return; }
    webServer.handleNotFound(r);
  });

  // serveStatic MUST be registered AFTER all route handlers so it doesn't shadow them
  asyncServer.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  asyncServer.begin();
}
