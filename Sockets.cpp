#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <esp_task_wdt.h>
#include "Sockets.h"
#include "ConfigSettings.h"
#include "Somfy.h"
#include "Network.h"
#include "GitOTA.h"

extern ConfigSettings settings;
extern Network net;
extern SomfyShadeController somfy;
extern SocketEmitter sockEmit;
extern GitUpdater git;


WebSocketsServer sockServer = WebSocketsServer(8080);

#define MAX_SOCK_RESPONSE 2048
static char g_response[MAX_SOCK_RESPONSE];

bool room_t::isJoined(uint8_t num) {
  for(uint8_t i = 0; i < sizeof(this->clients); i++) { 
    if(this->clients[i] == num) return true; 
  } 
  return false; 
}
bool room_t::join(uint8_t num) {
  if(this->isJoined(num)) return true; 
  for(uint8_t i = 0; i < sizeof(this->clients); i++) { 
    if(this->clients[i] == 255) { 
      this->clients[i] = num; 
      return true; 
    } 
  }
  return false;  
}
bool room_t::leave(uint8_t num) { 
  if(!this->isJoined(num)) return false; 
  for(uint8_t i = 0; i < sizeof(this->clients); i++) { 
    if(this->clients[i] == num) this->clients[i] = 255; 
  } 
  return true;
}
void room_t::clear() {
  memset(this->clients, 255, sizeof(this->clients));
}
uint8_t room_t::activeClients() {
  uint8_t n = 0;
  for(uint8_t i = 0; i < sizeof(this->clients); i++) {
    if(this->clients[i] != 255) n++;
  }
  return n;
}
/*********************************************************************
 * ClientSocketEvent class members
 ********************************************************************/
/*
void ClientSocketEvent::prepareMessage(const char *evt, const char *payload) {
  if(strlen(payload) + 5 >= sizeof(this->msg)) Serial.printf("Socket buffer overflow %d > 2048\n", strlen(payload) + 5 + strlen(evt));
    snprintf(this->msg, sizeof(this->msg), "42[%s,%s]", evt, payload);
}
void ClientSocketEvent::prepareMessage(const char *evt, JsonDocument &doc) {
  memset(this->msg, 0x00, sizeof(this->msg));
  snprintf(this->msg, sizeof(this->msg), "42[%s,", evt);
  serializeJson(doc, &this->msg[strlen(this->msg)], sizeof(this->msg) - strlen(this->msg) - 2);
  strcat(this->msg, "]");
}
*/

/*********************************************************************
 * SocketEmitter class members
 ********************************************************************/
void SocketEmitter::startup() {
  
}
void SocketEmitter::begin() {
  sockServer.begin();
  sockServer.enableHeartbeat(20000, 10000, 3);
  sockServer.onEvent(this->wsEvent);
  Serial.println("Socket Server Started...");
  //settings.printAvailHeap();
}
void SocketEmitter::loop() {
  this->initClients();
  sockServer.loop();  
}
JsonSockEvent *SocketEmitter::beginEmit(const char *evt) {
  this->json.beginEvent(&sockServer, evt, g_response, sizeof(g_response));
  return &this->json;
}
void SocketEmitter::endEmit(uint8_t num) { this->json.endEvent(num); sockServer.loop(); }
void SocketEmitter::endEmitRoom(uint8_t room) {
  if(room < SOCK_MAX_ROOMS) {
    room_t *r = &this->rooms[room];
    for(uint8_t i = 0; i < sizeof(r->clients); i++) {
      if(r->clients[i] != 255) this->json.endEvent(r->clients[i]);
    }
  }
}
uint8_t SocketEmitter::activeClients(uint8_t room) {
  if(room < SOCK_MAX_ROOMS) return this->rooms[room].activeClients();
  return 0;
}
void SocketEmitter::initClients() {
  for(uint8_t i = 0; i < sizeof(this->newClients); i++) {
    uint8_t num = this->newClients[i];
    if(num != 255) {
      if(sockServer.clientIsConnected(num)) {
        Serial.printf("Initializing Socket Client %u\n", num);
        esp_task_wdt_reset();
        settings.emitSockets(num);
        somfy.emitState(num);
        git.emitUpdateCheck(num);
        net.emitSockets(num);
        esp_task_wdt_reset();
      }
      this->newClients[i] = 255;
    }
  }
}
void SocketEmitter::delayInit(uint8_t num) {
  for(uint8_t i=0; i < sizeof(this->newClients); i++) {
    if(this->newClients[i] == num) break;
    else if(this->newClients[i] == 255) {
      this->newClients[i] = num;
      break;
    }
  }
}
void SocketEmitter::end() { 
  sockServer.close(); 
  for(uint8_t i = 0; i < SOCK_MAX_ROOMS; i++)
    this->rooms[i].clear();
}
void SocketEmitter::disconnect() { sockServer.disconnect(); }
void SocketEmitter::wsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    switch(type) {
        case WStype_ERROR:
            if(length > 0)
              Serial.printf("Socket Error: %s\n", payload);
            else
              Serial.println("Socket Error: \n");
            break;
        case WStype_DISCONNECTED:
            if(length > 0)
              Serial.printf("Socket [%u] Disconnected!\n [%s]", num, payload);
            else
              Serial.printf("Socket [%u] Disconnected!\n", num);
            for(uint8_t i = 0; i < SOCK_MAX_ROOMS; i++) {
              sockEmit.rooms[i].leave(num);
            }
            break;
        case WStype_CONNECTED:
            {
                IPAddress ip = sockServer.remoteIP(num);
                Serial.printf("Socket [%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
                // Send all the current shade settings to the client.
                sockServer.sendTXT(num, "Connected");
                //sockServer.loop();
                sockEmit.delayInit(num);
            }
            break;
        case WStype_TEXT:
            if(strncmp((char *)payload, "join:", 5) == 0) {
              // In this instance the client wants to join a room.  Let's do some
              // work to get the ordinal of the room that the client wants to join.
              uint8_t roomNum = atoi((char *)&payload[5]);
              Serial.printf("Client %u joining room %u\n", num, roomNum);
              if(roomNum < SOCK_MAX_ROOMS) sockEmit.rooms[roomNum].join(num);
            }
            else if(strncmp((char *)payload, "leave:", 6) == 0) {
              uint8_t roomNum = atoi((char *)&payload[6]);
              Serial.printf("Client %u leaving room %u\n", num, roomNum);
              if(roomNum < SOCK_MAX_ROOMS) sockEmit.rooms[roomNum].leave(num);
            }
            else {
              Serial.printf("Socket [%u] text: %s\n", num, payload);
            }
            // send message to client
            // webSocket.sendTXT(num, "message here");

            // send data to all connected clients
            // sockServer.broadcastTXT("message here");
            break;
        case WStype_BIN:
            Serial.printf("[%u] get binary length: %u\n", num, length);
            //hexdump(payload, length);

            // send message to client
            // sockServer.sendBIN(num, payload, length);
            break;
        case WStype_PONG:
            //Serial.printf("Pong from %u\n", num);
            break;
        case WStype_PING:
            //Serial.printf("Ping from %u\n", num);
            break;
        default:
            break;
    }  
}
