#include <ArduinoJson.h>
#include <ETH.h>
#ifndef configsettings_h
#define configsettings_h

#define FW_VERSION "v1.4.0"
enum DeviceStatus {
  DS_OK = 0,
  DS_ERROR = 1,
  DS_FWUPDATE = 2
};

class BaseSettings {
  public:
    bool loadFile(const char* filename);
    bool fromJSON(JsonObject &obj);
    bool toJSON(JsonObject &obj);
    bool parseIPAddress(JsonObject &obj, const char *prop, IPAddress *);
    bool parseValueString(JsonObject &obj, const char *prop, char *dest, size_t size);
    int parseValueInt(JsonObject &obj, const char *prop, int defVal);
    double parseValueDouble(JsonObject &obj, const char *prop, double defVal);
    bool saveFile(const char* filename);
    bool save();
    bool load();
};
class NTPSettings: BaseSettings {
  public:
    char ntpServer[64] = "pool.ntp.org";
    char posixZone[64] = "";
    bool fromJSON(JsonObject &obj);
    bool toJSON(JsonObject &obj);
    bool apply();
    bool begin();
    bool save();
    bool load();
    void print();
};
class WifiSettings: BaseSettings {
  public:
    WifiSettings();
//    char hostname[32] = "ESPSomfyRTS";
    char ssid[64] = "";
    char passphrase[32] = "";
    //bool ssdpBroadcast = true;
    bool begin();
    bool fromJSON(JsonObject &obj);
    bool toJSON(JsonObject &obj);
    String mapEncryptionType(int type);
    bool ssidExists(const char *ssid);
    void printNetworks();
    bool save();
    bool load();
    void print();
};
class EthernetSettings: BaseSettings {
  public:
    EthernetSettings();
    uint8_t boardType = 0; // These board types are enumerated in the ui and used to set the chip settings.
    bool dhcp = true;
    IPAddress ip;
    IPAddress subnet = IPAddress(255,255,255,0);
    IPAddress gateway;
    IPAddress dns1;
    IPAddress dns2;
    eth_phy_type_t phyType = ETH_PHY_LAN8720;
    eth_clock_mode_t CLKMode = ETH_CLOCK_GPIO0_IN;
    int8_t phyAddress = ETH_PHY_ADDR;
    int8_t PWRPin = ETH_PHY_POWER;
    int8_t MDCPin = ETH_PHY_MDC;
    int8_t MDIOPin = ETH_PHY_MDIO;
    
    bool begin();
    bool fromJSON(JsonObject &obj);
    bool toJSON(JsonObject &obj);
    bool load();
    bool save();
    void print();
  
};
class MQTTSettings: BaseSettings {
  public:
    bool enabled = false;
    char hostname[32] = "";
    char protocol[10] = "mqtt://";
    uint16_t port = 1883;
    char username[32] = "";
    char password[32] = "";
    char rootTopic[64] = "";
    bool begin();
    bool save();
    bool load();
    bool toJSON(JsonObject &obj);
    bool fromJSON(JsonObject &obj);
};
enum class conn_types : byte {
    unset = 0x00,
    wifi = 0x01,
    ethernet = 0x02,
    ethernetpref = 0x03
};

class ConfigSettings: BaseSettings {
  public:
    char serverId[10] = "";
    char hostname[32] = "ESPSomfyRTS";
    conn_types connType = conn_types::unset;
    const char* fwVersion = FW_VERSION;
    bool ssdpBroadcast = true;
    uint8_t status;
    WifiSettings WIFI;
    EthernetSettings Ethernet;
    NTPSettings NTP;
    MQTTSettings MQTT;
    bool fromJSON(JsonObject &obj);
    bool toJSON(JsonObject &obj);
    bool begin();
    bool save();
    bool load();
    void print();
    void emitSockets();
    void emitSockets(uint8_t num);
    bool toJSON(DynamicJsonDocument &doc);
};

#endif
