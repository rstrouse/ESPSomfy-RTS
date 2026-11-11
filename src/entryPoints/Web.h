#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include "Somfy.h"
#include "ApiToken.h"
#ifndef webserver_h
#define webserver_h

class Web {
  public:
    bool uploadSuccess = false;
    void startup();
    void begin();
    void loop();
    // Auth helpers.  token holds API_TOKEN_SIZE; false leaves it empty.
    bool createAPIToken(const IPAddress ipAddress, char *token);
    bool createAPIToken(const char *payload, char *token);
    bool createAPIPinToken(const IPAddress ipAddress, const char *pin, char *token);
    bool createAPIPasswordToken(const IPAddress ipAddress, const char *username, const char *password, char *token);
    // True under None, or when given is the token for the request's client.
    bool tokenAccepted(AsyncWebServerRequest *request, const char *given);
    // tokenAccepted with the apikey header, which is where HTTP sends it.
    bool isAuthenticated(AsyncWebServerRequest *request);

    // HTTP handlers
    void handleLogin(AsyncWebServerRequest *request, JsonVariant &json);
    void handleShadeCommand(AsyncWebServerRequest *request, JsonVariant &json);
    void handleGroupCommand(AsyncWebServerRequest *request, JsonVariant &json);
    void handleTiltCommand(AsyncWebServerRequest *request, JsonVariant &json);
    void handleBackup(AsyncWebServerRequest *request);
    void handleNotFound(AsyncWebServerRequest *request);
};
#endif
