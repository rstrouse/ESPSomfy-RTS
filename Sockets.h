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
};
/*
class ClientSocketEvent {
  private:
    uint8_t _objects = 0;
    uint8_t _arrays = 0;
    bool _nocomma = true;
    char _numbuff[25] = {0};
  protected:
    void _safecat(const char *val, bool escape = false);
    void _appendNumber(const char *name);
  public:
    ClientSocketEvent();
    ClientSocketEvent(const char *evt);
    //ClientSocketEvent(const char *evt, const char *data);
    char msg[2048] = "";
    void beginEmit(const char *evt);
    void endEmit();
    
    void prepareMessage(const char *evt, const char *data);
    void prepareMessage(const char *evt, JsonDocument &doc);
    void appendMessage(const char *text);
    void appendElement(const char *elem, const char *val);

    void beginObject(const char *name = nullptr);
    void endObject();
    void beginArray(const char *name = nullptr);
    void endArray();

    void appendElem(const char *name = nullptr);
    void addElem(const char* val);
    void addElem(float fval);
    void addElem(int8_t nval);
    void addElem(uint8_t nval);
    void addElem(int16_t nval);
    void addElem(uint16_t nval);
    void addElem(int32_t nval);
    void addElem(uint32_t nval);
    void addElem(int64_t lval);
    void addElem(uint64_t lval);
    void addElem(bool bval);
    
    void addElem(const char* name, float fval);
    void addElem(const char* name, int8_t nval);
    void addElem(const char* name, uint8_t nval);
    void addElem(const char* name, int16_t nval);
    void addElem(const char* name, uint16_t nval);
    void addElem(const char* name, int32_t nval);
    void addElem(const char* name, uint32_t nval);
    void addElem(const char* name, int64_t lval);
    void addElem(const char* name, uint64_t lval);
    void addElem(const char* name, bool bval);
    void addElem(const char *name, const char *val);
};
*/
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
    /*
    bool sendToRoom(uint8_t room, ClientSocketEvent *evt);
    bool sendToClients(ClientSocketEvent *evt);
    bool sendToClient(uint8_t num, ClientSocketEvent *evt);
    bool sendToClients(const char *evt, const char *data);
    bool sendToClient(uint8_t num, const char *evt, const char *data);
    bool sendToClients(const char *evt, JsonDocument &doc);
    bool sendToClient(uint8_t num, const char *evt, JsonDocument &doc);
    */
    static void wsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
};
#endif
