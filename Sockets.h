#include <WebSocketsServer.h>
#ifndef sockets_h
#define sockets_h

#define SOCK_MAX_ROOMS 1
#define ROOM_EMIT_FRAME 0

struct room_t {
  uint8_t clients[5] = {255, 255, 255, 255};
  uint8_t activeClients();
  bool isJoined(uint8_t num);
  bool join(uint8_t num);
  bool leave(uint8_t num);
};


class ClientSocketEvent {
  public:
    ClientSocketEvent();
    ClientSocketEvent(const char *evt);
    ClientSocketEvent(const char *evt, const char *data);
    char msg[2048];
    void prepareMessage(const char *evt, const char *data);
    void prepareMessage(const char *evt, JsonDocument &doc);
    void appendMessage(const char *text);
    void appendElement(const char *elem, const char *val);
    
   
};
class SocketEmitter {
  ClientSocketEvent evt;
  
  public:
    room_t rooms[SOCK_MAX_ROOMS];
    uint8_t activeClients(uint8_t room);
    void startup();
    void begin();
    void loop();
    void end();
    void disconnect();
    bool sendToRoom(uint8_t room, ClientSocketEvent *evt);
    bool sendToClients(ClientSocketEvent *evt);
    bool sendToClient(uint8_t num, ClientSocketEvent *evt);
    bool sendToClients(const char *evt, const char *data);
    bool sendToClient(uint8_t num, const char *evt, const char *data);
    bool sendToClients(const char *evt, JsonDocument &doc);
    bool sendToClient(uint8_t num, const char *evt, JsonDocument &doc);
    static void wsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
};
#endif
