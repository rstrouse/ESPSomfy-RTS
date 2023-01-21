#ifndef MQTT_H
#define MQTT_H
#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
class MQTTClass {
  public:
    char clientId[32] = {'\0'};
    bool begin();
    bool loop();
    bool end();
    bool connect();
    bool disconnect();
    bool connected();
    bool publish(const char *topic, const char *payload);
    bool publish(const char *topic, JsonDocument &doc);
    bool publish(const char *topic, JsonArray &arr);
    bool publish(const char *topic, JsonObject &obj);
    bool publish(const char *topic, uint8_t val);
    bool publish(const char *topic, int8_t val);
    bool publish(const char *topic, uint32_t val);
    bool publish(const char *topic, uint16_t val);
    bool subscribe(const char *topic);
    bool unsubscribe(const char *topic);
    static void receive(const char *topic, byte *payload, uint32_t length);
    
    
};
#endif
