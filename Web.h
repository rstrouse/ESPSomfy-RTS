#include <WebServer.h>
#ifndef webserver_h
#define webserver_h
class Web {
  public:
    void sendCORSHeaders(WebServer &server);
    void sendCacheHeaders(uint32_t seconds=604800);
    void startup();
    void handleLogin(WebServer &server);
    void handleLogout(WebServer &server);
    void handleStreamFile(WebServer &server, const char *filename, const char *encoding);
    void handleController(WebServer &server);
    void handleLoginContext(WebServer &server);
    void handleGetShades(WebServer &server);
    void handleGetGroups(WebServer &server);
    void handleShadeCommand(WebServer &server);
    void handleRepeatCommand(WebServer &server);
    void begin();
    void loop();
    void end();
    // Web Handlers
    bool createAPIToken(const IPAddress ipAddress, char *token);
    bool createAPIToken(const char *payload, char *token);
    bool createAPIPinToken(const IPAddress ipAddress, const char *pin, char *token);
    bool createAPIPasswordToken(const IPAddress ipAddress, const char *username, const char *password, char *token);
    bool isAuthenticated(WebServer &server, bool cfg = false);
};
#endif
