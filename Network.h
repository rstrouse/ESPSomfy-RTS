#include <Arduino.h>

#ifndef Network_h
#define Network_h
class Network {
  protected:
    unsigned long lastEmit = 0;
    unsigned long lastMDNS = 0;
    int lastRSSI = 0;
    int lastChannel = 0;
    int linkSpeed = 0;
    bool ethStarted = false;
  public:
    bool wifiFallback = false;
    bool softAPOpened = false;
    bool needsBroadcast = true;
    conn_types connType = conn_types::unset;
    bool connected();
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
    bool connectWiFi();
    bool connectWired();
    void setConnected(conn_types connType);
    int getStrengthByMac(const char *mac);
    int getStrengthBySSID(const char *ssid);
    void updateHostname();
    bool setup();
    void loop();
    void end();
    void emitSockets();
    void emitSockets(uint8_t num);
    uint32_t getChipId();
    static void networkEvent(WiFiEvent_t event);
};
#endif
