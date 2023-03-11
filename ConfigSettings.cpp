#include <Arduino.h>
#include <LittleFS.h>       // https://github.com/espressif/arduino-esp32/tree/master/libraries/LittleFS
#include <time.h>
#include <WiFi.h>
#include <Preferences.h>
#include "ConfigSettings.h"


Preferences pref;
bool BaseSettings::load() { return true; }
bool BaseSettings::save() { return true; }
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
    DynamicJsonDocument doc(filesize);
    DeserializationError err = deserializeJson(doc, data);
    JsonObject obj = doc.as<JsonObject>();
    this->fromJSON(obj);
    file.close();
  }
  return false; 
}
bool BaseSettings::saveFile(const char *filename) {
  File file = LittleFS.open(filename, "w");
  DynamicJsonDocument doc(2048);
  JsonObject obj = doc.as<JsonObject>();
  this->toJSON(obj);
  serializeJson(doc, file);
  file.close();
}
bool BaseSettings::parseValueString(JsonObject &obj, const char *prop, char *pdest, size_t size) {
  if(obj.containsKey(prop)) strlcpy(pdest, obj[prop], size);
  return true;
}
bool BaseSettings::parseIPAddress(JsonObject &obj, const char *prop, IPAddress *pdest) {
  if(obj.containsKey(prop)) {
    char buff[16];
    strlcpy(buff, obj[prop], sizeof(buff));
    pdest->fromString(buff);
  }
  return true;
}
int BaseSettings::parseValueInt(JsonObject &obj, const char *prop, int defVal) {
  if(obj.containsKey(prop)) return obj[prop]; 
  return defVal;
}
double BaseSettings::parseValueDouble(JsonObject &obj, const char *prop, double defVal) {
  if(obj.containsKey(prop)) return obj[prop];
  return defVal;
}
bool ConfigSettings::begin() {
  uint32_t chipId = 0;
  uint64_t mac = ESP.getEfuseMac();
  for(int i=0; i<17; i=i+8) {
    chipId |= ((mac >> (40 - i)) & 0xff) << i;
  }
  snprintf_P(this->serverId, sizeof(this->serverId), "%02X%02X%02X",
    (uint16_t)((chipId >> 16) & 0xff),
    (uint16_t)((chipId >> 8) & 0xff),
    (uint16_t)chipId & 0xff);
  this->load();
  this->WIFI.begin();
  this->Ethernet.begin();
  this->NTP.begin();
  this->MQTT.begin();
  this->print();
  return true;
}
bool ConfigSettings::load() {
  pref.begin("CFG");
  pref.getString("hostname", this->hostname, sizeof(this->hostname));
  this->ssdpBroadcast = pref.getBool("ssdpBroadcast", true);
  this->connType = static_cast<conn_types>(pref.getChar("connType", 0x00));
  pref.end();
  if(this->connType == conn_types::unset) {
    // We are doing this to convert the data from previous versions.
    this->connType == conn_types::wifi;
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
bool ConfigSettings::save() {
  pref.begin("CFG");
  pref.putString("hostname", this->hostname);
  pref.putBool("ssdpBroadcast", this->ssdpBroadcast);
  pref.putChar("connType", static_cast<uint8_t>(this->connType));
  pref.end();
  return true;
}
bool ConfigSettings::toJSON(JsonObject &obj) {
  obj["ssdpBroadcast"] = this->ssdpBroadcast;
  obj["hostname"] = this->hostname;
  obj["connType"] = static_cast<uint8_t>(this->connType);
  return true;
}
bool ConfigSettings::fromJSON(JsonObject &obj) {
    if(obj.containsKey("ssdpBroadcast")) this->ssdpBroadcast = obj["ssdpBroadcast"];
    if(obj.containsKey("hostname")) this->parseValueString(obj, "hostname", this->hostname, sizeof(this->hostname));
    if(obj.containsKey("connType")) this->connType = static_cast<conn_types>(obj["connType"].as<uint8_t>());
    return true;
}
void ConfigSettings::print() {
  Serial.printf("Connection Type: %d\n", this->connType);
  this->NTP.print();
  if(this->connType == conn_types::wifi || this->connType == conn_types::unset) this->WIFI.print();
  if(this->connType == conn_types::ethernet || this->connType == conn_types::ethernetpref) this->Ethernet.print();
}
void ConfigSettings::emitSockets() {}
void ConfigSettings::emitSockets(uint8_t num) {}
bool MQTTSettings::begin() {
  this->load();
  return true;
}
bool MQTTSettings::toJSON(JsonObject &obj) {
  obj["enabled"] = this->enabled;
  obj["protocol"] = this->protocol;
  obj["hostname"] = this->hostname;
  obj["port"] = this->port;
  obj["username"] = this->username;
  obj["password"] = this->password;
  obj["rootTopic"] = this->rootTopic;
  return true;
}
bool MQTTSettings::fromJSON(JsonObject &obj) {
  if(obj.containsKey("enabled")) this->enabled = obj["enabled"];
  this->parseValueString(obj, "protocol", this->protocol, sizeof(this->protocol));
  this->parseValueString(obj, "hostname", this->hostname, sizeof(this->hostname));
  this->parseValueString(obj, "username", this->username, sizeof(this->username));
  this->parseValueString(obj, "password", this->password, sizeof(this->password));
  this->parseValueString(obj, "rootTopic", this->rootTopic, sizeof(this->rootTopic));
  return true;
}
bool MQTTSettings::save() {
  pref.begin("MQTT");
  pref.clear();
  pref.putString("protocol", this->protocol);
  pref.putString("hostname", this->hostname);
  pref.putShort("port", this->port);
  pref.putString("username", this->username);
  pref.putString("password", this->password);
  pref.putString("rootTopic", this->rootTopic);
  pref.putBool("enabled", this->enabled);
  pref.end();
  return true;
}
bool MQTTSettings::load() {
  pref.begin("MQTT");
  pref.getString("protocol", this->protocol, sizeof(this->protocol));
  pref.getString("hostname", this->hostname, sizeof(this->hostname));
  this->port = pref.getShort("port", 1883);
  pref.getString("username", this->username, sizeof(this->username));
  pref.getString("password", this->password, sizeof(this->password));
  pref.getString("rootTopic", this->rootTopic, sizeof(this->rootTopic));
  this->enabled = pref.getBool("enabled", false);
  pref.end();
  return true;
}
bool ConfigSettings::toJSON(DynamicJsonDocument &doc) {
  doc["fwVersion"] = this->fwVersion;
  JsonObject objWIFI = doc.createNestedObject("WIFI");
  this->WIFI.toJSON(objWIFI);
  JsonObject objNTP = doc.createNestedObject("NTP");
  this->NTP.toJSON(objNTP);
  JsonObject objMQTT = doc.createNestedObject("MQTT");
  this->MQTT.toJSON(objMQTT);
  return true;
}
bool NTPSettings::begin() {
  this->load();
  this->apply();
  return true;
}
bool NTPSettings::save() {
  pref.begin("NTP");
  pref.clear();
  pref.putString("ntpServer", this->ntpServer);
  pref.putString("posixZone", this->posixZone);
  pref.end();
  struct tm dt;
  configTime(0, 0, this->ntpServer);
  if(!getLocalTime(&dt)) return false;
  setenv("TZ", this->posixZone, 1);
  return true;
}
bool NTPSettings::load() {
  pref.begin("NTP");
  pref.getString("ntpServer", this->ntpServer, sizeof(this->ntpServer));
  pref.getString("posixZone", this->posixZone, sizeof(this->posixZone));
  pref.end();
  return true;
}
void NTPSettings::print() {
  Serial.println("NTP Settings ");
  Serial.print(this->ntpServer);
  Serial.print(" TZ:");
  Serial.println(this->posixZone);  
}
bool NTPSettings::fromJSON(JsonObject &obj) {
  this->parseValueString(obj, "ntpServer", this->ntpServer, sizeof(this->ntpServer));
  this->parseValueString(obj, "posixZone", this->posixZone, sizeof(this->posixZone));
  return true;
}
bool NTPSettings::toJSON(JsonObject &obj) {
  obj["ntpServer"] = this->ntpServer;
  obj["posixZone"] = this->posixZone;
  return true;
}
bool NTPSettings::apply() { 
  struct tm dt;
  configTime(0, 0, this->ntpServer);
  if(!getLocalTime(&dt)) return false;
  setenv("TZ", this->posixZone, 1);
  return true;
}
WifiSettings::WifiSettings() {}

bool WifiSettings::begin() {
  this->load();
  return true;
}
bool WifiSettings::fromJSON(JsonObject &obj) {
  this->parseValueString(obj, "ssid", this->ssid, sizeof(this->ssid));
  this->parseValueString(obj, "passphrase", this->passphrase, sizeof(this->passphrase));
  return true;
}
bool WifiSettings::toJSON(JsonObject &obj) {
  obj["ssid"] = this->ssid;
  obj["passphrase"] = this->passphrase;
  return true;
}
bool WifiSettings::save() {
  pref.begin("WIFI");
  pref.clear();
  pref.putString("ssid", this->ssid);
  pref.putString("passphrase", this->passphrase);
  pref.end();
  return true;
}
bool WifiSettings::load() {
  pref.begin("WIFI");
  pref.getString("ssid", this->ssid, sizeof(this->ssid));
  pref.getString("passphrase", this->passphrase, sizeof(this->passphrase));
  this->ssid[sizeof(this->ssid) - 1] = '\0';
  this->passphrase[sizeof(this->passphrase) - 1] = '\0';
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
    return "Unknown";
  }
}
void WifiSettings::print() {
  Serial.println("WIFI Settings");
  Serial.print(" SSID: [");
  Serial.print(this->ssid);
  Serial.print("] PassPhrase: [");
  Serial.print(this->passphrase);
  Serial.println("]");  
}
void WifiSettings::printNetworks() {
  int n = WiFi.scanNetworks(false, true);
  Serial.print("Scanned ");
  Serial.print(n);
  Serial.println(" Networks...");
  String network;
  uint8_t encType;
  int32_t RSSI;
  uint8_t* BSSID;
  int32_t channel;
  bool isHidden;
  for(int i = 0; i < n; i++) {
    if(WiFi.SSID(i).compareTo(this->ssid) == 0) Serial.print("*");
    else Serial.print(" ");
    Serial.print(i);
    Serial.print(": ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" (");
    Serial.print(WiFi.RSSI(i));
    Serial.print("dBm) CH:");
    Serial.print(WiFi.channel(i));
    Serial.print(" MAC:");
    Serial.print(WiFi.BSSIDstr(i));
    if(isHidden) Serial.print(" [hidden]");
    Serial.println();
  }
}
bool WifiSettings::ssidExists(const char *ssid) {
  int n = WiFi.scanNetworks(false, true);
  for(int i = 0; i < n; i++) {
    if(WiFi.SSID(i).compareTo(ssid) == 0) return true;
  }
  return false;
}
EthernetSettings::EthernetSettings() {}
bool EthernetSettings::begin() {
  this->load();
  return true;
}
bool EthernetSettings::fromJSON(JsonObject &obj) {
  if(obj.containsKey("dhcp")) this->dhcp = obj["dhcp"];
  if(obj.containsKey("boardType")) this->boardType = obj["boardType"];
  if(obj.containsKey("phyAddress")) this->phyAddress = obj["phyAddress"];
  if(obj.containsKey("CLKMode")) this->CLKMode = static_cast<eth_clock_mode_t>(obj["CLKMode"]);
  if(obj.containsKey("phyType")) this->phyType = static_cast<eth_phy_type_t>(obj["phyType"]);
  if(obj.containsKey("PWRPin")) this->PWRPin = obj["PWRPin"];
  if(obj.containsKey("MDCPin")) this->MDCPin = obj["MDCPin"];
  if(obj.containsKey("MDIOPin")) this->MDIOPin = obj["MDIOPin"];
  this->parseIPAddress(obj, "ip", &this->ip);
  this->parseIPAddress(obj, "gateway", &this->gateway);
  this->parseIPAddress(obj, "subnet", &this->subnet);
  this->parseIPAddress(obj, "dns1", &this->dns1);
  this->parseIPAddress(obj, "dns2", &this->dns2);
  return true;
}
bool EthernetSettings::toJSON(JsonObject &obj) {
  obj["boardType"] = this->boardType;
  obj["phyAddress"] = this->phyAddress;
  obj["dhcp"] = this->dhcp;
  obj["CLKMode"] = static_cast<uint8_t>(this->CLKMode);
  obj["phyType"] = static_cast<uint8_t>(this->phyType);
  obj["PWRPin"] = this->PWRPin;
  obj["MDCPin"] = this->MDCPin;
  obj["MDIOPin"] = this->MDIOPin;
  obj["ip"] = this->ip.toString();
  obj["gateway"] = this->gateway.toString();
  obj["subnet"] = this->subnet.toString();
  obj["dns1"] = this->dns1.toString();
  obj["dns2"] = this->dns2.toString();
  return true;
}
bool EthernetSettings::save() {
  pref.begin("ETH");
  pref.clear();
  pref.putBool("dhcp", this->dhcp);
  pref.putChar("boardType", this->boardType);
  pref.putChar("phyAddress", this->phyAddress);
  pref.putChar("phyType", static_cast<uint8_t>(this->phyType));
  pref.putChar("CLKMode", static_cast<uint8_t>(this->CLKMode));
  pref.putChar("PWRPin", this->PWRPin);
  pref.putChar("MDCPin", this->MDCPin);
  pref.putChar("MDIOPin", this->MDIOPin);
  pref.putString("ip", this->ip.toString());
  pref.putString("gateway", this->gateway.toString());
  pref.putString("subnet", this->subnet.toString());
  pref.putString("dns1", this->dns1.toString());
  pref.putString("dns2", this->dns2.toString());
  pref.end();
  return true;
}
bool EthernetSettings::load() {
  pref.begin("ETH");
  this->dhcp = pref.getBool("dhcp", true);
  this->boardType = pref.getChar("boardType", this->boardType);
  this->phyType = static_cast<eth_phy_type_t>(pref.getChar("phyType", ETH_PHY_LAN8720));
  this->CLKMode = static_cast<eth_clock_mode_t>(pref.getChar("CLKMode", ETH_CLOCK_GPIO0_IN));
  this->phyAddress = pref.getChar("phyAddress", this->phyAddress);
  this->PWRPin = pref.getChar("PWRPin", this->PWRPin);
  this->MDCPin = pref.getChar("MDCPin", this->MDCPin);
  this->MDIOPin = pref.getChar("MDIOPin", this->MDIOPin);
  
  char buff[16];
  pref.getString("ip", buff, sizeof(buff));
  this->ip.fromString(buff);
  pref.getString("gateway", buff, sizeof(buff));
  this->gateway.fromString(buff);
  pref.getString("subnet", buff, sizeof(buff));
  this->subnet.fromString(buff);
  pref.getString("dns1", buff, sizeof(buff));
  this->dns1.fromString(buff);
  pref.getString("dns2", buff, sizeof(buff));
  this->dns2.fromString(buff);
  pref.end();
  return true;
}
void EthernetSettings::print() {
  Serial.println("Ethernet Settings");
  Serial.printf("Board:%d PHYType:%d CLK:%d ADDR:%d PWR:%d MDC:%d MDIO:%d\n", this->boardType, this->phyType, this->CLKMode, this->phyAddress, this->PWRPin, this->MDCPin, this->MDIOPin);
  Serial.print("   GATEWAY: ");
  Serial.println(this->gateway);
  Serial.print("   SUBNET: ");
  Serial.println(this->subnet);
  Serial.print("   DNS1: ");
  Serial.println(this->dns1);
  Serial.print("   DNS2: ");
  Serial.println(this->dns2);
}
