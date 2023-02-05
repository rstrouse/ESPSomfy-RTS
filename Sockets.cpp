#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include "Sockets.h"
#include "ConfigSettings.h"
#include "Somfy.h"
#include "Network.h"


extern ConfigSettings settings;
extern Network net;
extern SomfyShadeController somfy;

WebSocketsServer sockServer = WebSocketsServer(8080);
char g_buffer[1024];

/*********************************************************************
 * ClientSocketEvent class members
 ********************************************************************/
void ClientSocketEvent::prepareMessage(const char *evt, const char *payload) {
    snprintf(this->msg, sizeof(this->msg), "42[%s,%s]", evt, payload);
}


/*********************************************************************
 * SocketEmitter class members
 ********************************************************************/
void SocketEmitter::startup() {
  
}
void SocketEmitter::begin() {
  sockServer.begin();
  sockServer.enableHeartbeat(20000, 10000, 3);
  sockServer.onEvent(this->wsEvent);
}
void SocketEmitter::loop() {
  sockServer.loop();  
}
/*
bool SocketEmitter::sendToClients(const char *evt, JsonObject &obj) {
  serializeJson(obj, g_buffer, sizeof(g_buffer));
  return this->sendToClients(evt, g_buffer);
}
bool SocketEmitter::sendToClient(uint8_t num, const char *evt, JsonObject &obj) {
  serializeJson(obj, g_buffer, sizeof(g_buffer));
  return this->sendToClient(num, evt, g_buffer);
}
*/
bool SocketEmitter::sendToClients(const char *evt, const char *payload) {
    if(settings.status == DS_FWUPDATE) return true;
    this->evt.prepareMessage(evt, payload);
    return sockServer.broadcastTXT(this->evt.msg);
}
bool SocketEmitter::sendToClient(uint8_t num, const char *evt, const char *payload) {
    if(settings.status == DS_FWUPDATE) return true;
    this->evt.prepareMessage(evt, payload);
    return sockServer.sendTXT(num, this->evt.msg);
}
void SocketEmitter::end() { sockServer.close(); }
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
            break;
        case WStype_CONNECTED:
            {
                IPAddress ip = sockServer.remoteIP(num);
                Serial.printf("Socket [%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
                // Send all the current shade settings to the client.
                sockServer.sendTXT(num, "Connected");
                settings.emitSockets(num);
                somfy.emitState(num);
                net.emitSockets(num);
            }
            break;
        case WStype_TEXT:
            Serial.printf("Socket [%u] get Text: %s\n", num, payload);

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
       
    }  
}
