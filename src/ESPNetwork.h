#include <Arduino.h>
#include <WiFi.h>
#include "Utils.h"

#ifndef ESPNetwork_h
#define ESPNetwork_h

//enum class conn_types_t : byte;

#define CONNECT_TIMEOUT 20000
#define SSID_SCAN_INTERVAL 600000
// A weak signal moves a dBm or two all the time, so only broadcast real
// changes.
#define RSSI_EMIT_MIN_DBM 3
// How often loop() broadcasts the status: memory, then wifi or ethernet.
// memory is the one event that is always sent, so this must stay well under
// Home Assistant's 60s receive timeout.  A client that connects gets all of it
// straight away in its sync.
#define STATUS_EMIT_MS 30000
// A roaming scan only moves to another AP at least this much stronger.
#define ROAM_MIN_GAIN_DB 6
class ESPNetwork {
  protected:
    unsigned long lastEmit = 0;
    unsigned long lastMDNS = 0;
    RssiBroadcast rssiBroadcast;
    int linkSpeed = 0;
    bool _connecting = false;
  public:
    unsigned long lastWifiScan = 0;
    bool ethStarted = false;
    bool wifiFallback = false;
    bool softAPOpened = false;
    bool openingSoftAP = false;
    conn_types_t connType = conn_types_t::unset;
    conn_types_t connTarget = conn_types_t::unset;
    bool connected();
    bool connecting();
    void clearConnecting();
    conn_types_t preferredConnType();
    // How the device is connected, as clients are told: Ethernet, Wifi or Unknown.
    const char *connTypeName() const {
      if(this->connType == conn_types_t::ethernet) return "Ethernet";
      if(this->connType == conn_types_t::wifi) return "Wifi";
      return "Unknown";
    }
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
    // A scan of every network, hidden ones included, for the WiFi settings.
    // It runs once no roaming scan is, and its networks go to the protocol 2
    // clients that asked (ProtoSocket::wifiNetworks).  loop() only.
    void scanForClients();
    bool setup();
    void loop();
    void end();
    void emitStatus();
    void emitHeap();
    uint32_t getChipId();
    static void networkEvent(WiFiEvent_t event);
};
#endif
