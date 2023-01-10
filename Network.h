#include <Arduino.h>

#ifndef Network_h
#define Network_h
class Network {
  protected:
    unsigned long lastEmit = 0;
  public:
    String ssid;
    String mac;
    int channel;
    int strength;
    int disconnected = 0;
    int connectAttempts = 0;
    long connectStart = 0;
    long connectTime = 0;
    bool openSoftAP();
    bool connect();
    void setConnected();
    int getStrengthByMac(const char *mac);
    int getStrengthBySSID(const char *ssid);
    bool setup();
    void loop();
    void end();
    void emitSockets();
    uint32_t getChipId();
};
#endif
