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
int BaseSettings::parseValueInt(JsonObject &obj, const char *prop, int defVal) {
  if(obj.containsKey(prop)) return obj[prop]; 
  return defVal;
}
double BaseSettings::parseValueDouble(JsonObject &obj, const char *prop, double defVal) {
  if(obj.containsKey(prop)) return obj[prop];
  return defVal;
}
bool ConfigSettings::begin() {
  this->WIFI.begin();
  this->NTP.begin();
  this->MQTT.begin();
  this->print();
  return true;
}
void ConfigSettings::print() {
  this->NTP.print();
  this->WIFI.print();
}
void ConfigSettings::emitSockets() {
  
}
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
bool WifiSettings::begin() {
  this->load();
  return true;
}
bool WifiSettings::fromJSON(JsonObject &obj) {
  this->parseValueString(obj, "hostname", this->hostname, sizeof(this->hostname));
  this->parseValueString(obj, "ssid", this->ssid, sizeof(this->ssid));
  this->parseValueString(obj, "passphrase", this->passphrase, sizeof(this->passphrase));
  if(obj.containsKey("ssdpBroadcast")) this->ssdpBroadcast = obj["ssdpBroadcast"];
  return true;
}
bool WifiSettings::toJSON(JsonObject &obj) {
  obj["hostname"] = this->hostname;
  obj["ssid"] = this->ssid;
  obj["passphrase"] = this->passphrase;
  obj["ssdpBroadcast"] = this->ssdpBroadcast;
  return true;
}
bool WifiSettings::save() {
  pref.begin("WIFI");
  pref.clear();
  pref.putString("hostname", this->hostname);
  pref.putString("ssid", this->ssid);
  pref.putString("passphrase", this->passphrase);
  pref.putBool("ssdpBroadcast", this->ssdpBroadcast);
  pref.end();
  return true;
}
bool WifiSettings::load() {
  pref.begin("WIFI");
  pref.getString("hostname", this->hostname, sizeof(this->hostname));
  pref.getString("ssid", this->ssid, sizeof(this->ssid));
  pref.getString("passphrase", this->passphrase, sizeof(this->passphrase));
  this->ssdpBroadcast = pref.getBool("ssdpBroadcast", true);
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
  Serial.print("HOST: ");
  Serial.print(this->hostname);
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
