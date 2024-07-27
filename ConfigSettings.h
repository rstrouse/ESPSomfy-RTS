#include <ArduinoJson.h>
#include <ETH.h>
#ifndef configsettings_h
#define configsettings_h
#include "WResp.h"
#define FW_VERSION "v2.4.7"
enum class conn_types_t : byte {
    unset = 0x00,
    wifi = 0x01,
    ethernet = 0x02,
    ethernetpref = 0x03,
    ap = 0x04
};

enum DeviceStatus {
  DS_OK = 0,
  DS_ERROR = 1,
  DS_FWUPDATE = 2
};
struct restore_options_t {
  bool settings = false;
  bool shades = false;
  bool network = false;
  bool transceiver = false;
  bool repeaters = false;
  bool mqtt = false;
  void fromJSON(JsonObject &obj);
};
struct appver_t {
  char name[15] = "";
  uint8_t major = 0;
  uint8_t minor = 0;
  uint8_t build = 0;
  char suffix[4] = "";
  void parse(const char *ver);
  bool toJSON(JsonObject &obj);
  void toJSON(JsonResponse &json);
  void toJSON(JsonSockEvent *json);
  int8_t compare(appver_t &ver);
  void copy(appver_t &ver);
};


class BaseSettings {
  public:
    bool loadFile(const char* filename);
    bool fromJSON(JsonObject &obj);
    bool toJSON(JsonObject &obj);
    void toJSON(JsonResponse &json);
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
    char ntpServer[65] = "pool.ntp.org";
    char posixZone[64] = "";
    bool fromJSON(JsonObject &obj);
    bool toJSON(JsonObject &obj);
    void toJSON(JsonResponse &json);
    bool apply();
    bool begin();
    bool save();
    bool load();
    void print();
};
class WifiSettings: BaseSettings {
  public:
    WifiSettings();
    bool roaming = true;
    bool hidden = false;
    char ssid[65] = "";
    char passphrase[65] = "";
    //bool ssdpBroadcast = true;
    bool begin();
    bool fromJSON(JsonObject &obj);
    bool toJSON(JsonObject &obj);
    void toJSON(JsonResponse &json);
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
    eth_phy_type_t phyType = ETH_PHY_LAN8720;
    eth_clock_mode_t CLKMode = ETH_CLOCK_GPIO0_IN;
    int8_t phyAddress = ETH_PHY_ADDR;
    int8_t PWRPin = ETH_PHY_POWER;
    int8_t MDCPin = ETH_PHY_MDC;
    int8_t MDIOPin = ETH_PHY_MDIO;
    
    bool begin();
    bool fromJSON(JsonObject &obj);
    bool toJSON(JsonObject &obj);
    void toJSON(JsonResponse &json);
    bool load();
    bool save();
    void print();
    bool usesPin(uint8_t pin);
};
class IPSettings: BaseSettings {
  public:
    IPSettings();
    bool dhcp = true;
    IPAddress ip;
    IPAddress subnet = IPAddress(255,255,255,0);
    IPAddress gateway = IPAddress(0,0,0,0);
    IPAddress dns1 = IPAddress(0,0,0,0);
    IPAddress dns2 = IPAddress(0,0,0,0);
    bool begin();
    bool fromJSON(JsonObject &obj);
    bool toJSON(JsonObject &obj);
    void toJSON(JsonResponse &json);
    bool load();
    bool save();
    void print();
};
enum class security_types : byte {
  None = 0x00,
  PinEntry = 0x01,
  Password = 0x02
};
enum class security_permissions : byte {
  ConfigOnly = 0x01
};
class SecuritySettings: BaseSettings {
  public:
    security_types type = security_types::None;
    char username[33] = "";
    char password[33] = "";
    char pin[5] = "";
    uint8_t permissions = 0;
    bool begin();
    bool save();
    bool load();
    void print();
    bool toJSON(JsonObject &obj);
    void toJSON(JsonResponse &json);
    bool fromJSON(JsonObject &obj);
};
class MQTTSettings: BaseSettings {
  public:
    bool enabled = false;
    bool pubDisco = false;
    char hostname[65] = "ESPSomfyRTS";
    char protocol[10] = "mqtt://";
    uint16_t port = 1883;
    char username[33] = "";
    char password[33] = "";
    char rootTopic[65] = "";
    char discoTopic[65] = "homeassistant";
    bool begin();
    bool save();
    bool load();
    bool toJSON(JsonObject &obj);
    void toJSON(JsonResponse &json);
    bool fromJSON(JsonObject &obj);
};
class ConfigSettings: BaseSettings {
  public:
    static void printAvailHeap();
    char serverId[10] = "";
    char hostname[32] = "ESPSomfyRTS";
    char chipModel[10] = "ESP32";
    conn_types_t connType = conn_types_t::unset;
    appver_t fwVersion;
    appver_t appVersion;
    bool ssdpBroadcast = true;
    bool checkForUpdate = true;
    uint8_t status;
    IPSettings IP;
    WifiSettings WIFI;
    EthernetSettings Ethernet;
    NTPSettings NTP;
    MQTTSettings MQTT;
    SecuritySettings Security;
    bool requiresAuth();
    bool fromJSON(JsonObject &obj);
    bool toJSON(JsonObject &obj);
    void toJSON(JsonResponse &json);
    bool begin();
    bool save();
    bool load();
    void print();
    void emitSockets();
    void emitSockets(uint8_t num);
    bool toJSON(DynamicJsonDocument &doc);
    uint16_t calcSettingsRecSize();
    uint16_t calcNetRecSize();
    bool getAppVersion();
};
#endif
