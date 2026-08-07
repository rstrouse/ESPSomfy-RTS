#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "Somfy.h"
#ifndef webserver_h
#define webserver_h

#define WEB_CMD_QUEUE_SIZE 8
#define WEB_CMD_TIMEOUT_MS 3000

enum class web_cmd_t : uint8_t {
    shade_command,      // moveToTarget or sendCommand
    group_command,      // group sendCommand
    tilt_command,       // moveToTiltTarget or sendTiltCommand
    shade_repeat,       // shade sendCommand/repeatFrame
    group_repeat,       // group sendCommand/repeatFrame
    set_positions,      // set shade position directly
    shade_sensor,       // shade sensor command
    group_sensor,       // group sensor command
};

struct web_command_t {
    web_cmd_t type;
    uint8_t shadeId;
    uint8_t groupId;
    uint8_t target;         // 0-100 or 255 (none)
    somfy_commands command;
    int8_t repeat;
    uint8_t stepSize;
    int8_t position;        // for setPositions
    int8_t tiltPosition;    // for setPositions/tilt
    int8_t sunny;           // for sensor
    int8_t windy;           // for sensor
};

class Web {
  public:
    bool uploadSuccess = false;
    void startup();
    void begin();
    void loop();
    // Auth helpers
    bool createAPIToken(const IPAddress ipAddress, char *token);
    bool createAPIToken(const char *payload, char *token);
    bool createAPIPinToken(const IPAddress ipAddress, const char *pin, char *token);
    bool createAPIPasswordToken(const IPAddress ipAddress, const char *username, const char *password, char *token);
    bool isAuthenticated(AsyncWebServerRequest *request, bool cfg = false);

    // Async API handlers
    void handleDiscovery(AsyncWebServerRequest *request);
    void handleGetRooms(AsyncWebServerRequest *request);
    void handleGetShades(AsyncWebServerRequest *request);
    void handleGetGroups(AsyncWebServerRequest *request);
    void handleController(AsyncWebServerRequest *request);
    void handleRoom(AsyncWebServerRequest *request);
    void handleShade(AsyncWebServerRequest *request);
    void handleGroup(AsyncWebServerRequest *request);
    void handleLogin(AsyncWebServerRequest *request, JsonVariant &json);
    void handleShadeCommand(AsyncWebServerRequest *request, JsonVariant &json);
    void handleGroupCommand(AsyncWebServerRequest *request, JsonVariant &json);
    void handleTiltCommand(AsyncWebServerRequest *request, JsonVariant &json);
    void handleRepeatCommand(AsyncWebServerRequest *request, JsonVariant &json);
    void handleSetPositions(AsyncWebServerRequest *request, JsonVariant &json);
    void handleSetSensor(AsyncWebServerRequest *request, JsonVariant &json);
    void handleDownloadFirmware(AsyncWebServerRequest *request);
    void handleBackup(AsyncWebServerRequest *request);
    void handleReboot(AsyncWebServerRequest *request);
    void handleNotFound(AsyncWebServerRequest *request);
  private:
    void processQueue();
    bool queueCommand(const web_command_t &cmd);
};
#endif
