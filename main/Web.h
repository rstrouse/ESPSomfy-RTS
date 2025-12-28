#include <WebServer.h>
#include "Somfy.h"
#ifndef webserver_h
#define webserver_h
class Web {
  public:
    bool uploadSuccess = false;
    void sendCORSHeaders(WebServer &server);
    void sendCacheHeaders(uint32_t seconds=604800);
    void startup();
    void handleLogin(WebServer &server);
    void handleLogout(WebServer &server);
    void handleStreamFile(WebServer &server, const char *filename, const char *encoding);
    void handleController(WebServer &server);
    void handleLoginContext(WebServer &server);
    void handleGetRepeaters(WebServer &server);
    void handleGetRooms(WebServer &server);
    void handleGetShades(WebServer &server);
    void handleGetGroups(WebServer &server);
    void handleShadeCommand(WebServer &server);
    void handleRepeatCommand(WebServer &server);
    void handleGroupCommand(WebServer &server);
    void handleTiltCommand(WebServer &server);
    void handleDiscovery(WebServer &server);
    void handleNotFound(WebServer &server);
    void handleRoom(WebServer &server);
    void handleShade(WebServer &server);
    void handleGroup(WebServer &server);
    void handleSetPositions(WebServer &server);
    void handleSetSensor(WebServer &server);
    void handleDownloadFirmware(WebServer &server);
    void handleBackup(WebServer &server, bool attach = false);
    void handleReboot(WebServer &server);
    void handleDeserializationError(WebServer &server, DeserializationError &err);
    void begin();
    void loop();
    void end();
    // Web Handlers
    bool createAPIToken(const IPAddress ipAddress, char *token);
    bool createAPIToken(const char *payload, char *token);
    bool createAPIPinToken(const IPAddress ipAddress, const char *pin, char *token);
    bool createAPIPasswordToken(const IPAddress ipAddress, const char *username, const char *password, char *token);
    bool isAuthenticated(WebServer &server, bool cfg = false);

    //void chunkRoomsResponse(WebServer &server, const char *elem = nullptr);
    //void chunkShadesResponse(WebServer &server, const char *elem = nullptr);
    //void chunkGroupsResponse(WebServer &server, const char *elem = nullptr);
    //void chunkGroupResponse(WebServer &server, SomfyGroup *, const char *prefix = nullptr);
};
#endif
