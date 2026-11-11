#include <Arduino.h>
#include <LittleFS.h>       // https://github.com/espressif/arduino-esp32/tree/master/libraries/LittleFS
#include <time.h>
#include <WiFi.h>
#include <Preferences.h>
#include "ConfigSettings.h"
#include "Utils.h"
#include "Secrets.h"
#include "esp_chip_info.h"
#include "esp_ota_ops.h"
#include "esp_log.h"

static const char *TAG = "Config";


void restore_options_t::fromJSON(JsonObject &obj) {
  if(!obj["shades"].isNull()) this->shades = obj["shades"];
  if(!obj["settings"].isNull()) this->settings = obj["settings"];
  if(!obj["network"].isNull()) this->network = obj["network"];
  if(!obj["transceiver"].isNull()) this->transceiver = obj["transceiver"];
  if(!obj["repeaters"].isNull()) this->repeaters = obj["repeaters"];
  if(!obj["mqtt"].isNull()) this->mqtt = obj["mqtt"];
}
int8_t appver_t::compare(appver_t &ver) {
  if(this->major == ver.major && this->minor == ver.minor && this->build == ver.build) return 0;
  if(this->major > ver.major) return 1;
  else if(this->major < ver.major) return -1;
  else {
    if(this->minor > ver.minor) return 1;
    else if(this->minor < ver.minor) return -1;
    else {
      if(this->build > ver.build) return 1;
      else if(this->build < ver.build) return -1;
    }
  }
  return 0;
}
void appver_t::copy(appver_t &ver) {
  strcpy(this->name, ver.name);
  this->major = ver.major;
  this->minor = ver.minor;
  this->build = ver.build;
  strcpy(this->suffix, ver.suffix);
}
void appver_t::parse(const char *ver) {
  // Now lets parse this pig.
  memset(this, 0x00, sizeof(appver_t));
  strlcpy(this->name, ver, sizeof(this->name));
  char num[3];
  uint8_t i = 0;
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 3 && i < strlen(ver);) {
    char ch = ver[i++];
    // Trim off all the prefix.
    if(ch == '.') break;
    if(!isdigit(ch)) continue;
    if(ch != '.')
      num[j++] = ch;
    else
      break;
  }
  this->major = static_cast<uint8_t>(atoi(num) & 0xFF);
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 3 && i < strlen(ver);) {
    char ch = ver[i++];
    if(ch != '.')
      num[j++] = ch;
    else
      break;
  }
  this->minor = static_cast<uint8_t>(atoi(num) & 0xFF);
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 3 && i < strlen(ver);) {
    char ch = ver[i++];
    if(!isdigit(ch)) break;
    if(ch != '.')
      num[j++] = ch;
    else
      break;
  }
  this->build = static_cast<uint8_t>(atoi(num) & 0xFF);
  if(strlen(ver) < i) strlcpy(this->suffix, &ver[i], sizeof(this->suffix));
}

bool BaseSettings::load() { return true; }
bool BaseSettings::loadFile(const char *filename) { 
  size_t filesize = 10;
  String data = "";
  if(LittleFS.exists(filename)) {
    File file = LittleFS.open(filename, "r");
    filesize += file.size();
    while(file.available()) {
      char c = file.read();
      data += c;
    }
    JsonDocument doc;
    deserializeJson(doc, data);
    JsonObject obj = doc.as<JsonObject>();
    this->fromJSON(obj);
    file.close();
  }
  return false; 
}
bool BaseSettings::parseValueString(JsonObject &obj, const char *prop, char *pdest, size_t size) {
  if(!obj[prop].isNull()) strlcpy(pdest, obj[prop], size);
  return true;
}
bool BaseSettings::parseIPAddress(JsonObject &obj, const char *prop, IPAddress *pdest) {
  if(!obj[prop].isNull()) {
    char buff[16];
    strlcpy(buff, obj[prop], sizeof(buff));
    pdest->fromString(buff);
  }
  return true;
}
int BaseSettings::parseValueInt(JsonObject &obj, const char *prop, int defVal) {
  if(!obj[prop].isNull()) return obj[prop];
  return defVal;
}
double BaseSettings::parseValueDouble(JsonObject &obj, const char *prop, double defVal) {
  if(!obj[prop].isNull()) return obj[prop];
  return defVal;
}
bool ConfigSettings::begin() {
  uint32_t chipId = 0;
  esp_chip_info_t ci;
  esp_chip_info(&ci);
  switch(ci.model) {
    case esp_chip_model_t::CHIP_ESP32:
      strcpy(this->chipModel, "");
      break;
    case esp_chip_model_t::CHIP_ESP32S3:
      strcpy(this->chipModel, "s3");
      break;
    case esp_chip_model_t::CHIP_ESP32C3:
      strcpy(this->chipModel, "c3");
      break;
    #ifdef CONFIG_IDF_TARGET_ESP32C5
    case esp_chip_model_t::CHIP_ESP32C5:
      strcpy(this->chipModel, "c5");
      break;
    #endif
    #ifdef CONFIG_IDF_TARGET_ESP32C6
    case esp_chip_model_t::CHIP_ESP32C6:
      strcpy(this->chipModel, "c6");
      break;
    #endif
    #ifdef CONFIG_IDF_TARGET_ESP32P4
    // Chips before v2.0 run their own build (esp32p4_es libraries), so they
    // get their own release files.  v2.x and v3.00 run neither.
    case esp_chip_model_t::CHIP_ESP32P4:
      #if CONFIG_ESP32P4_SELECTS_REV_LESS_V3
      strcpy(this->chipModel, "p4es");
      #else
      strcpy(this->chipModel, "p4");
      #endif
      break;
    #endif
    default:
      sprintf(this->chipModel, "UNK%d", static_cast<int>(ci.model));
      break;
  }
  ESP_LOGD(TAG, "Chip Model ESP32-%s", this->chipModel);
  this->hasOtaSlot = esp_ota_get_next_update_partition(nullptr) != nullptr;
  this->fwVersion.parse(FW_VERSION);
  uint64_t mac = ESP.getEfuseMac();
  for(int i=0; i<17; i=i+8) {
    chipId |= ((mac >> (40 - i)) & 0xff) << i;
  }
  snprintf_P(this->serverId, sizeof(this->serverId), "%02X%02X%02X",
    (uint16_t)((chipId >> 16) & 0xff),
    (uint16_t)((chipId >> 8) & 0xff),
    (uint16_t)chipId & 0xff);
  this->load();
  this->Security.begin();
  this->IP.begin();
  this->WIFI.begin();
#if HAS_ETHERNET
  this->Ethernet.begin();
#endif
  this->NTP.begin();
  this->MQTT.begin();
  this->print();
  return true;
}
bool ConfigSettings::load() {
  Preferences pref;
  this->fwVersion.parse(FW_VERSION);
  this->getAppVersion();
  pref.begin("CFG");
  pref.getString("hostname", this->hostname, sizeof(this->hostname));
  this->ssdpBroadcast = pref.getBool("ssdpBroadcast", true);
  this->checkForUpdate = pref.getBool("checkForUpdate", true);
  this->connType = static_cast<conn_types_t>(pref.getChar("connType", 0x00));
  //Serial.printf("Preference GFG Free Entries: %d\n", pref.freeEntries());
  pref.end();
  if(this->connType == conn_types_t::unset) {
    // We are doing this to convert the data from previous versions.
    this->connType = conn_types_t::wifi;
    pref.begin("WIFI");
    pref.getString("hostname", this->hostname, sizeof(this->hostname));
    this->ssdpBroadcast = pref.getBool("ssdpBroadcast", true);
    pref.remove("hostname");
    pref.remove("ssdpBroadcast");
    pref.end();
    this->save();    
  }
  return true;
}
bool ConfigSettings::getAppVersion() {
  char app[15];
  if(!LittleFS.exists("/appversion")) return false;
  File f = LittleFS.open("/appversion", "r");
  memset(app, 0x00, sizeof(app));
  f.read((uint8_t *)app, sizeof(app) - 1);
  f.close();
  _trim(app);
  this->appVersion.parse(app);
  return true;
}
bool ConfigSettings::save() {
  Preferences pref;
  pref.begin("CFG");
  pref.putString("hostname", this->hostname);
  pref.putBool("ssdpBroadcast", this->ssdpBroadcast);
  pref.putChar("connType", static_cast<uint8_t>(this->connType));
  pref.putBool("checkForUpdate", this->checkForUpdate);
  pref.end();
  return true;
}
bool ConfigSettings::requiresAuth() { return this->Security.type != security_types::None; }
bool ConfigSettings::fromJSON(JsonObject &obj) {
    if(!obj["ssdpBroadcast"].isNull()) this->ssdpBroadcast = obj["ssdpBroadcast"];
    if(!obj["hostname"].isNull()) this->parseValueString(obj, "hostname", this->hostname, sizeof(this->hostname));
    if(!obj["connType"].isNull()) this->connType = static_cast<conn_types_t>(obj["connType"].as<uint8_t>());
    return true;
}
void ConfigSettings::print() {
  this->Security.print();
  ESP_LOGD(TAG, "Connection Type: %u", (unsigned int) this->connType);
  this->NTP.print();
  if(this->connType == conn_types_t::wifi || this->connType == conn_types_t::unset) this->WIFI.print();
#if HAS_ETHERNET
  if(this->connType == conn_types_t::ethernet || this->connType == conn_types_t::ethernetpref) this->Ethernet.print();
#endif
}
bool MQTTSettings::begin() {
  this->load();
  return true;
}
bool MQTTSettings::fromJSON(JsonObject &obj) {
  if(!obj["enabled"].isNull()) this->enabled = obj["enabled"];
  if(!obj["pubDisco"].isNull()) this->pubDisco = obj["pubDisco"];
  this->parseValueString(obj, "protocol", this->protocol, sizeof(this->protocol));
  this->parseValueString(obj, "hostname", this->hostname, sizeof(this->hostname));
  // An empty password clears the stored one only alongside a new username.
  const char *username = obj["username"].as<const char *>();
  bool newUsername = username && strcmp(username, this->username) != 0;
  this->parseValueString(obj, "username", this->username, sizeof(this->username));
  applySecret(obj["password"].as<const char *>(), this->password, sizeof(this->password), newUsername);
  this->parseValueString(obj, "rootTopic", this->rootTopic, sizeof(this->rootTopic));
  this->parseValueString(obj, "discoTopic", this->discoTopic, sizeof(this->discoTopic));
  if(!obj["port"].isNull()) this->port = obj["port"];
  return true;
}
bool MQTTSettings::save() {
  Preferences pref;
  pref.begin("MQTT");
  pref.clear();
  pref.putString("protocol", this->protocol);
  pref.putString("hostname", this->hostname);
  pref.putShort("port", this->port);
  pref.putString("username", this->username);
  pref.putString("password", this->password);
  pref.putString("rootTopic", this->rootTopic);
  pref.putBool("enabled", this->enabled);
  pref.putBool("pubDisco", this->pubDisco);
  pref.putString("discoTopic", this->discoTopic);
  pref.end();
  return true;
}
bool MQTTSettings::load() {
  Preferences pref;
  pref.begin("MQTT");
  pref.getString("protocol", this->protocol, sizeof(this->protocol));
  pref.getString("hostname", this->hostname, sizeof(this->hostname));
  this->port = pref.getShort("port", 1883);
  pref.getString("username", this->username, sizeof(this->username));
  pref.getString("password", this->password, sizeof(this->password));
  pref.getString("rootTopic", this->rootTopic, sizeof(this->rootTopic));
  this->enabled = pref.getBool("enabled", false);
  this->pubDisco = pref.getBool("pubDisco", false);
  pref.getString("discoTopic", this->discoTopic, sizeof(this->discoTopic));
  pref.end();
  return true;
}
bool NTPSettings::begin() {
  this->load();
  this->apply();
  return true;
}
bool NTPSettings::save() {
  Preferences pref;
  pref.begin("NTP");
  pref.clear();
  pref.putString("ntpServer", this->ntpServer);
  pref.putString("posixZone", this->posixZone);
  pref.end();
  return this->apply();
}
bool NTPSettings::load() {
  Preferences pref;
  pref.begin("NTP");
  pref.getString("ntpServer", this->ntpServer, sizeof(this->ntpServer));
  pref.getString("posixZone", this->posixZone, sizeof(this->posixZone));
  pref.end();
  return true;
}
void NTPSettings::print() {
  ESP_LOGD(TAG, "NTP Settings %s TZ:%s", this->ntpServer, this->posixZone);
}
bool NTPSettings::fromJSON(JsonObject &obj) {
  this->parseValueString(obj, "ntpServer", this->ntpServer, sizeof(this->ntpServer));
  this->parseValueString(obj, "posixZone", this->posixZone, sizeof(this->posixZone));
  return true;
}
bool NTPSettings::apply() {
  // Sets the zone and starts SNTP, which sets the clock once the network is
  // up.  Waiting here with getLocalTime() held up boot by 5 s before the
  // network existed, and then returned before the zone was set.
  configTzTime(this->posixZone, this->ntpServer);
  return true;
}
IPSettings::IPSettings() {}
bool IPSettings::begin() {
  this->load();
  return true;
}
bool IPSettings::fromJSON(JsonObject &obj) {
  if(!obj["dhcp"].isNull()) this->dhcp = obj["dhcp"];
  this->parseIPAddress(obj, "ip", &this->ip);
  this->parseIPAddress(obj, "gateway", &this->gateway);
  this->parseIPAddress(obj, "subnet", &this->subnet);
  this->parseIPAddress(obj, "dns1", &this->dns1);
  this->parseIPAddress(obj, "dns2", &this->dns2);
  return true;
}
bool IPSettings::save() {
  Preferences pref;
  pref.begin("IP");
  pref.clear();
  pref.putBool("dhcp", this->dhcp);
  pref.putString("ip", this->ip.toString());
  pref.putString("gateway", this->gateway.toString());
  pref.putString("subnet", this->subnet.toString());
  pref.putString("dns1", this->dns1.toString());
  pref.putString("dns2", this->dns2.toString());
  pref.end();
  return true;
}
bool IPSettings::load() {
  Preferences pref;
  pref.begin("IP");
  this->dhcp = pref.getBool("dhcp", true);
  char buff[16];
  if(pref.isKey("ip")) {
    pref.getString("ip", buff, sizeof(buff));
    this->ip.fromString(buff);
  }
  if(pref.isKey("gateway")) {
    pref.getString("gateway", buff, sizeof(buff));
    this->gateway.fromString(buff);
  }
  if(pref.isKey("subnet")) {
    pref.getString("subnet", buff, sizeof(buff));
    this->subnet.fromString(buff);
  }
  if(pref.isKey("dns1")) {
    pref.getString("dns1", buff, sizeof(buff));
    this->dns1.fromString(buff);
  }
  if(pref.isKey("dns2")) {
    pref.getString("dns2", buff, sizeof(buff));
    this->dns2.fromString(buff);
  }
  ESP_LOGD(TAG, "Preference IP Free Entries: %d", pref.freeEntries());
  pref.end();
  return true;
}
bool SecuritySettings::begin() {
  this->load();
  return true;
}
bool SecuritySettings::fromJSON(JsonObject &obj) {
  if(!obj["type"].isNull()) this->type = static_cast<security_types>(obj["type"].as<uint8_t>());
  this->parseValueString(obj, "username", this->username, sizeof(this->username));
  applySecret(obj["password"].as<const char *>(), this->password, sizeof(this->password));
  applySecret(obj["pin"].as<const char *>(), this->pin, sizeof(this->pin));
  return true;
}
bool SecuritySettings::save() {
  Preferences pref;
  pref.begin("SEC");
  pref.clear();
  pref.putChar("type", static_cast<uint8_t>(this->type));
  pref.putString("username", this->username);
  pref.putString("password", this->password);
  pref.putString("pin", this->pin);
  pref.end();
  return true;
}
// A device that had "Secure Config Only" keeps its PIN or password and so is
// Secured.  Its old permissions key goes with the next save's clear().
bool SecuritySettings::load() {
  Preferences pref;
  pref.begin("SEC");
  this->type = static_cast<security_types>(pref.getChar("type", 0));
  if(pref.isKey("username")) pref.getString("username", this->username, sizeof(this->username));
  if(pref.isKey("password")) pref.getString("password", this->password, sizeof(this->password));
  if(pref.isKey("pin")) pref.getString("pin", this->pin, sizeof(this->pin));
  pref.end();
  return true;
}
void SecuritySettings::print() {
  ESP_LOGD(TAG, "SECURITY   Type:%u Username:[%s] Password:[%s] Pin:[%s]",
    static_cast<uint8_t>(this->type), this->username, maskSecret(this->password), maskSecret(this->pin));
}

WifiSettings::WifiSettings() {}
bool WifiSettings::begin() {
  this->load();
  return true;
}
bool WifiSettings::fromJSON(JsonObject &obj) {
  // An empty passphrase clears the stored one only alongside a new SSID (an open network).
  const char *ssid = obj["ssid"].as<const char *>();
  bool newSsid = ssid && strcmp(ssid, this->ssid) != 0;
  this->parseValueString(obj, "ssid", this->ssid, sizeof(this->ssid));
  applySecret(obj["passphrase"].as<const char *>(), this->passphrase, sizeof(this->passphrase), newSsid);
  if(!obj["roaming"].isNull()) this->roaming = obj["roaming"];
  if(!obj["hidden"].isNull()) this->hidden = obj["hidden"];
  return true;
}
bool WifiSettings::save() {
  Preferences pref;
  pref.begin("WIFI");
  pref.clear();
  pref.putString("ssid", this->ssid);
  pref.putString("passphrase", this->passphrase);
  pref.putBool("roaming", this->roaming);
  pref.putBool("hidden", this->hidden);
  pref.end();
  return true;
}
bool WifiSettings::load() {
  Preferences pref;
  pref.begin("WIFI");
  pref.getString("ssid", this->ssid, sizeof(this->ssid));
  pref.getString("passphrase", this->passphrase, sizeof(this->passphrase));
  this->ssid[sizeof(this->ssid) - 1] = '\0';
  this->passphrase[sizeof(this->passphrase) - 1] = '\0';
  this->roaming = pref.getBool("roaming", true);
  this->hidden = pref.getBool("hidden", false);
  pref.end();
  return true;
}
String WifiSettings::mapEncryptionType(int type) {
  switch(type) {
    case WIFI_AUTH_OPEN:
      return "Open";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA/PSK";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2/PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/WPA2/PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA/Enterprise";
  }
  return "Unknown";
}
void WifiSettings::print() {
  ESP_LOGD(TAG, "WIFI Settings SSID: [%s] PassPhrase: [%s]", this->ssid, maskSecret(this->passphrase));
}
void WifiSettings::printNetworks() {
  int n = WiFi.scanNetworks(false, false);
  ESP_LOGI(TAG, "Scanned %d Networks...", n);
  String network;
  for(int i = 0; i < n; i++) {
    ESP_LOGI(TAG, "%s%d: %s (%ddBm) CH:%d MAC:%s",
      (WiFi.SSID(i).compareTo(this->ssid) == 0) ? "*" : " ",
      i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i), WiFi.BSSIDstr(i).c_str());
  }
}
bool WifiSettings::ssidExists(const char *ssid) {
  int n = WiFi.scanNetworks(false, true);
  for(int i = 0; i < n; i++) {
    if(WiFi.SSID(i).compareTo(ssid) == 0) return true;
  }
  return false;
}
#if HAS_ETHERNET
EthernetSettings::EthernetSettings() {}
bool EthernetSettings::begin() {
  this->load();
  return true;
}
bool EthernetSettings::fromJSON(JsonObject &obj) {
  if(!obj["boardType"].isNull()) this->boardType = obj["boardType"];
  if(!obj["phyAddress"].isNull()) this->phyAddress = obj["phyAddress"];
  if(!obj["CLKMode"].isNull()) this->CLKMode = obj["CLKMode"];
  if(!obj["phyType"].isNull()) this->phyType = obj["phyType"];
  if(!obj["PWRPin"].isNull()) this->PWRPin = obj["PWRPin"];
  if(!obj["MDCPin"].isNull()) this->MDCPin = obj["MDCPin"];
  if(!obj["MDIOPin"].isNull()) this->MDIOPin = obj["MDIOPin"];
  return true;
}
bool EthernetSettings::usesPin(uint8_t pin) {
  if(ethClockPin(this->CLKMode) == pin) return true;
  else if(this->PWRPin == pin) return true;
  else if(this->MDCPin == pin) return true;
  else if(this->MDIOPin == pin) return true;
  return false;  
}
bool EthernetSettings::save() {
  Preferences pref;
  pref.begin("ETH");
  pref.clear();
  pref.putChar("boardType", this->boardType);
  pref.putChar("phyAddress", this->phyAddress);
  pref.putChar("phyType", this->phyType);
  pref.putChar("CLKMode", this->CLKMode);
  pref.putChar("PWRPin", this->PWRPin);
  pref.putChar("MDCPin", this->MDCPin);
  pref.putChar("MDIOPin", this->MDIOPin);
  pref.end();
  return true;
}
bool EthernetSettings::load() {
  Preferences pref;
  pref.begin("ETH");
  this->boardType = pref.getChar("boardType", this->boardType);
  this->phyType = pref.getChar("phyType", this->phyType);
  this->CLKMode = pref.getChar("CLKMode", this->CLKMode);
  this->phyAddress = pref.getChar("phyAddress", this->phyAddress);
  this->PWRPin = pref.getChar("PWRPin", this->PWRPin);
  this->MDCPin = pref.getChar("MDCPin", this->MDCPin);
  this->MDIOPin = pref.getChar("MDIOPin", this->MDIOPin);
  pref.end();
  return true;
}
void EthernetSettings::print() {
  ESP_LOGD(TAG, "Ethernet Settings Board:%d PHYType:%d CLK:%d ADDR:%d PWR:%d MDC:%d MDIO:%d",
    this->boardType, this->phyType, this->CLKMode, this->phyAddress, this->PWRPin, this->MDCPin, this->MDIOPin);
}
#endif // HAS_ETHERNET
void ConfigSettings::printAvailHeap() {
  ESP_LOGD(TAG, "Max Heap: %u", (unsigned int)ESP.getMaxAllocHeap());
  ESP_LOGD(TAG, "Free Heap: %u", (unsigned int)ESP.getFreeHeap());
  ESP_LOGD(TAG, "Min Heap: %u", (unsigned int)ESP.getMinFreeHeap());
}
