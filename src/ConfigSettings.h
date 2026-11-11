#include <ArduinoJson.h>
#include "EthPhy.h"
#ifndef configsettings_h
#define configsettings_h

#ifndef FW_VERSION
  #define FW_VERSION "v4.0.2" // Fallback if app_version.py script fails
#endif

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
  int8_t compare(appver_t &ver);
  void copy(appver_t &ver);
};


class BaseSettings {
  public:
    bool loadFile(const char* filename);
    bool fromJSON(JsonObject &obj);

    bool parseIPAddress(JsonObject &obj, const char *prop, IPAddress *);
    bool parseValueString(JsonObject &obj, const char *prop, char *dest, size_t size);
    int parseValueInt(JsonObject &obj, const char *prop, int defVal);
    double parseValueDouble(JsonObject &obj, const char *prop, double defVal);
    bool save();
    bool load();
};
class NTPSettings: BaseSettings {
  public:
    char ntpServer[65] = "pool.ntp.org";
    char posixZone[64] = "";
    bool fromJSON(JsonObject &obj);

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

    String mapEncryptionType(int type);
    bool ssidExists(const char *ssid);
    void printNetworks();
    bool save();
    bool load();
    void print();
    
};
#if HAS_ETHERNET
class EthernetSettings: BaseSettings {
  public:
    EthernetSettings();
    uint8_t CLKMode = static_cast<uint8_t>(eth_clock_id_t::GPIO0_IN); // An eth_clock_id_t, see EthPhy.h.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    // The IP101 wiring of Espressif's P4 boards, which Waveshare's ESP32-P4-ETH
    // boards copy, and the web UI's board type for it.  The ESP32's defaults
    // would put MDIO on an SDIO pin.
    uint8_t boardType = 8;
    uint8_t phyType = static_cast<uint8_t>(eth_phy_id_t::TLK110);
    int8_t phyAddress = 1;
    int8_t PWRPin = 51;
    int8_t MDCPin = 31;
    int8_t MDIOPin = 52;
#else
    uint8_t boardType = 0; // These board types are enumerated in the ui and used to set the chip settings.
    uint8_t phyType = static_cast<uint8_t>(eth_phy_id_t::LAN8720); // An eth_phy_id_t, see EthPhy.h.
    // The LAN8720 wiring Arduino 2.x defaulted to.  3.x has no defaults.
    int8_t phyAddress = 0;
    int8_t PWRPin = -1;
    int8_t MDCPin = 23;
    int8_t MDIOPin = 18;
#endif

    bool begin();
    bool fromJSON(JsonObject &obj);

    bool load();
    bool save();
    void print();
    bool usesPin(uint8_t pin);
};
#endif
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

    bool load();
    bool save();
    void print();
};
enum class security_types : byte {
  None = 0x00,
  PinEntry = 0x01,
  Password = 0x02
};
class SecuritySettings: BaseSettings {
  public:
    security_types type = security_types::None;
    char username[33] = "";
    char password[33] = "";
    char pin[5] = "";
    bool begin();
    bool save();
    bool load();
    void print();

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

    bool fromJSON(JsonObject &obj);
};
class ConfigSettings: BaseSettings {
  public:
    static void printAvailHeap();
    char serverId[10] = "";
    char hostname[32] = "ESPSomfyRTS";
    char chipModel[10] = "ESP32";
    // Whether the partition table has a second app slot for /updateFirmware
    // to write.  Builds with one 3 MB slot (C5, C6, debug) are flashed over USB.
    bool hasOtaSlot = true;
    conn_types_t connType = conn_types_t::unset;
    appver_t fwVersion;
    appver_t appVersion;
    bool ssdpBroadcast = true;
    // Nothing reads it since the GitHub update went (docs/adr/0007-no-github-update.md).
    // NVS and the backup file still carry it so their formats stay the same.
    bool checkForUpdate = true;
    uint8_t status;
    IPSettings IP;
    WifiSettings WIFI;
#if HAS_ETHERNET
    EthernetSettings Ethernet;
#endif
    NTPSettings NTP;
    MQTTSettings MQTT;
    SecuritySettings Security;
    bool requiresAuth();
    bool fromJSON(JsonObject &obj);

    bool begin();
    bool save();
    bool load();
    void print();
    bool getAppVersion();
};
#endif
