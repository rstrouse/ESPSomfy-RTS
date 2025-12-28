#include <WebSocketsServer.h>
#include "WResp.h"
#ifndef sockets_h
#define sockets_h

#define SOCK_MAX_ROOMS 1
#define ROOM_EMIT_FRAME 0

struct room_t {
  uint8_t clients[5] = {255, 255, 255, 255, 255};
  uint8_t activeClients();
  bool isJoined(uint8_t num);
  bool join(uint8_t num);
  bool leave(uint8_t num);
  void clear();
};
class SocketEmitter {
  protected:
    uint8_t newclients = 0;
    uint8_t newClients[5] = {255,255,255,255,255};
    void delayInit(uint8_t num);
  public:
    JsonSockEvent json;
    //ClientSocketEvent evt;
    room_t rooms[SOCK_MAX_ROOMS];
    uint8_t activeClients(uint8_t room);
    void initClients();
    void startup();
    void begin();
    void loop();
    void end();
    void disconnect();
    JsonSockEvent * beginEmit(const char *evt);
    void endEmit(uint8_t num = 255);
    void endEmitRoom(uint8_t num);
    static void wsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
};
#endif
