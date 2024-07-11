#include <Arduino.h>

#ifndef Network_h
#define Network_h

//enum class conn_types_t : byte;

#define CONNECT_TIMEOUT 20000
#define SSID_SCAN_INTERVAL 60000
class Network {
  protected:
    unsigned long lastEmit = 0;
    unsigned long lastMDNS = 0;
    int lastRSSI = 0;
    int lastChannel = 0;
    int linkSpeed = 0;
    bool _connecting = false;
  public:
    unsigned long lastWifiScan = 0;
    bool ethStarted = false;
    bool wifiFallback = false;
    bool softAPOpened = false;
    bool openingSoftAP = false;
    bool needsBroadcast = true;
    conn_types_t connType = conn_types_t::unset;
    conn_types_t connTarget = conn_types_t::unset;
    bool connected();
    bool connecting();
    void clearConnecting();
    conn_types_t preferredConnType();
    String ssid;
    String mac;
    int channel;
    int strength;
    int disconnected = 0;
    int connectAttempts = 0;
    uint32_t disconnectTime = 0;
    uint32_t connectStart = 0;
    uint32_t connectTime = 0;
    bool openSoftAP();
    bool connect(conn_types_t ctype);
    bool connectWiFi(const uint8_t *bssid = nullptr, const int32_t channel = -1);
    bool connectWired();
    void setConnected(conn_types_t connType);
    bool getStrongestAP(const char *ssid, uint8_t *bssid, int32_t *channel);
    bool changeAP(const uint8_t *bssid, const int32_t channel);
    //int getStrengthByMac(const char *mac);
    //int getStrengthBySSID(const char *ssid);
    void updateHostname();
    bool setup();
    void loop();
    void end();
    void emitSockets();
    void emitSockets(uint8_t num);
    void emitHeap(uint8_t num = 255);
    uint32_t getChipId();
    static void networkEvent(WiFiEvent_t event);
};
#endif
