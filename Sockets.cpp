#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include "Sockets.h"
#include "ConfigSettings.h"
#include "Somfy.h"


extern ConfigSettings settings;
extern SomfyShadeController somfy;

WebSocketsServer sockServer = WebSocketsServer(8080);
char g_buffer[1024];

/*********************************************************************
 * ClientSocketEvent class members
 ********************************************************************/
void ClientSocketEvent::prepareMessage(const char *evt, const char *payload) {
    sprintf(this->msg, "42[%s,%s]", evt, payload);
}


/*********************************************************************
 * SocketEmitter class members
 ********************************************************************/
void SocketEmitter::startup() {
  
}
void SocketEmitter::begin() {
  sockServer.begin();
  sockServer.onEvent(this->wsEvent);
}
void SocketEmitter::loop() {
  sockServer.loop();  
}
bool SocketEmitter::sendToClients(const char *evt, JsonObject &obj) {
  serializeJson(obj, g_buffer, sizeof(g_buffer));
  return this->sendToClients(evt, g_buffer);
}
bool SocketEmitter::sendToClient(uint8_t num, const char *evt, JsonObject &obj) {
  serializeJson(obj, g_buffer, sizeof(g_buffer));
  return this->sendToClient(num, evt, g_buffer);
}
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
        case WStype_DISCONNECTED:
            Serial.printf("Socket [%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED:
            {
                IPAddress ip = sockServer.remoteIP(num);
                Serial.printf("Socket [%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
                // Send all the current Sensor readings to the client.
                sockServer.sendTXT(num, "Connected");
                settings.emitSockets();
                somfy.emitState(num);
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
    }  
}
