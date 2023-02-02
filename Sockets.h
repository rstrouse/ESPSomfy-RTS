#include <WebSocketsServer.h>
#ifndef sockets_h
#define sockets_h
#include <ArduinoJson.h>
class ClientSocketEvent {
  public:
    char msg[1024];
    void prepareMessage(const char *evt, const char *data);
};
class SocketEmitter {
  ClientSocketEvent evt;
  public:
    void startup();
    void begin();
    void loop();
    void end();
    void disconnect();
    bool sendToClients(const char *evt, const char *data);
    bool sendToClient(uint8_t num, const char *evt, const char *data);
    //bool sendToClients(const char *evt, JsonObject &obj);
    //bool sendToClient(uint8_t num, const char *evt, JsonObject &obj);
    static void wsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
};
#endif
