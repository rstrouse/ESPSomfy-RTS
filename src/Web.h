#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include "Somfy.h"
#ifndef webserver_h
#define webserver_h
class Web {
  public:
    bool uploadSuccess = false;
    void startup();
    void begin();
    void loop();
    void end();
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
};
#endif
