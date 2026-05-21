#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <vector>
#ifndef webserver_h
#define webserver_h

using HTTPMethod = uint8_t;

struct AsyncRequestContext {
    String body;
    bool bodyEnabled = true;
    std::vector<std::pair<String, String>> headers;
};

class WebClientCompat {
  public:
    explicit WebClientCompat(AsyncWebServerRequest *request = nullptr) : request(request) {}
    IPAddress remoteIP() const;
    void bind(AsyncWebServerRequest *request);
  private:
    AsyncWebServerRequest *request;
};

class WebRequestCompat {
  public:
    explicit WebRequestCompat(AsyncWebServerRequest *request);
    HTTPMethod method() const;
    bool hasArg(const char *name) const;
    String arg(const char *name) const;
    bool hasHeader(const char *name) const;
    String header(const char *name) const;
    void sendHeader(const char *name, const char *value);
    void sendHeader(const char *name, const String &value);
    void sendHeader(const String &name, const String &value);
    void sendHeader(const __FlashStringHelper *name, const __FlashStringHelper *value);
    void sendHeader(const __FlashStringHelper *name, const char *value);
    void send(int code);
    void send(int code, const char *body);
    void send(int code, const char *contentType, const char *content);
    void send(int code, const char *contentType, const String &content);
    void send(AsyncWebServerResponse *response);
    AsyncResponseStream *beginResponseStream(const char *contentType);
    String uri() const;
    WebClientCompat &client();
    AsyncWebServerRequest *raw() const;
  private:
    AsyncRequestContext *ctx() const;
    void addPendingHeaders(AsyncWebServerResponse *response);
    void clearContext();

    AsyncWebServerRequest *request;
    mutable WebClientCompat requestClient;
};

class Web {
  public:
    bool uploadSuccess = false;
    void sendCORSHeaders(WebRequestCompat &server);
    void sendCacheHeaders(WebRequestCompat &server, uint32_t seconds=604800);
    void startup();
    void handleLogin(WebRequestCompat &server);
    void handleLogout(WebRequestCompat &server);
    void handleStreamFile(WebRequestCompat &server, const char *filename, const char *encoding);
    void handleController(WebRequestCompat &server);
    void handleLoginContext(WebRequestCompat &server);
    void handleGetRepeaters(WebRequestCompat &server);
    void handleGetRooms(WebRequestCompat &server);
    void handleGetShades(WebRequestCompat &server);
    void handleGetGroups(WebRequestCompat &server);
    void handleShadeCommand(WebRequestCompat &server);
    void handleRepeatCommand(WebRequestCompat &server);
    void handleGroupCommand(WebRequestCompat &server);
    void handleTiltCommand(WebRequestCompat &server);
    void handleDiscovery(WebRequestCompat &server);
    void handleNotFound(WebRequestCompat &server);
    void handleRoom(WebRequestCompat &server);
    void handleShade(WebRequestCompat &server);
    void handleGroup(WebRequestCompat &server);
    void handleSetPositions(WebRequestCompat &server);
    void handleSetSensor(WebRequestCompat &server);
    void handleDownloadFirmware(WebRequestCompat &server);
    void handleBackup(WebRequestCompat &server, bool attach = false);
    void handleReboot(WebRequestCompat &server);
    void handleDeserializationError(WebRequestCompat &server, DeserializationError &err);
    void begin();
    void loop();
    void end();
    // Web Handlers
    bool createAPIToken(const IPAddress ipAddress, char *token);
    bool createAPIToken(const char *payload, char *token);
    bool createAPIPinToken(const IPAddress ipAddress, const char *pin, char *token);
    bool createAPIPasswordToken(const IPAddress ipAddress, const char *username, const char *password, char *token);
    bool isAuthenticated(WebRequestCompat &server, bool cfg = false);

    static AsyncRequestContext *ensureRequestContext(AsyncWebServerRequest *request);
    static AsyncRequestContext *findRequestContext(AsyncWebServerRequest *request);
    static void releaseRequestContext(AsyncWebServerRequest *request);
    static void collectBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

    //void chunkRoomsResponse(WebRequestCompat &server, const char *elem = nullptr);
    //void chunkShadesResponse(WebRequestCompat &server, const char *elem = nullptr);
    //void chunkGroupsResponse(WebRequestCompat &server, const char *elem = nullptr);
    //void chunkGroupResponse(WebRequestCompat &server, SomfyGroup *, const char *prefix = nullptr);
};
#endif
