#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "ConfigSettings.h"
#include "Network.h"
#include "Web.h"
#include "Sockets.h"
#include "Utils.h"
#include "SSDP.h"
#include "MQTT.h"

extern ConfigSettings settings;
extern Web webServer;
extern SocketEmitter sockEmit;
extern MQTTClass mqtt;
extern rebootDelay_t rebootDelay;

int connectRetries = 0;
void Network::end() {
  sockEmit.end();
  SSDP.end();
  mqtt.end();
  delay(100);
}
bool Network::setup() {
  WiFi.persistent(false);
  //Serial.print("WiFi Mode: ");
  //Serial.println(WiFi.getMode());
  if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  settings.WIFI.printNetworks();
  sockEmit.begin();
  if(!this->connect()) this->openSoftAP();
  return true;
}
void Network::loop() {
  if(millis() - this->lastEmit > 1500) {
    while(!this->connect()) {
      // If we lost our connenction
      connectRetries++;
      if(connectRetries > 100) {
        this->openSoftAP();
        break;
      }
      sockEmit.loop();
    }
    connectRetries = 0;
    this->lastEmit = millis();
    this->emitSockets();
    if(WiFi.status() != WL_CONNECTED) return;
  }
  sockEmit.loop();
  if(settings.WIFI.ssdpBroadcast) {
    if(!SSDP.isStarted) SSDP.begin();
    SSDP.loop();
  }
  else if(!settings.WIFI.ssdpBroadcast && SSDP.isStarted) SSDP.end();
  mqtt.loop();
}
void Network::emitSockets() {
  if(WiFi.status() == WL_CONNECTED) {
    if(abs(abs(WiFi.RSSI()) - abs(this->lastRSSI)) > 2 || WiFi.channel() != this->lastChannel) {
      char buf[128];
      snprintf(buf, sizeof(buf), "{\"ssid\":\"%s\",\"strength\":%d,\"channel\":%d}", WiFi.SSID(), WiFi.RSSI(), WiFi.channel());
      sockEmit.sendToClients("wifiStrength", buf);
      this->lastRSSI = WiFi.RSSI();
      this->lastChannel = WiFi.channel();
    }
  }
  else
    sockEmit.sendToClients("wifiStrength", "{\"ssid\":\"\", \"strength\":-100,\"channel\":-1}");
}
void Network::emitSockets(uint8_t num) {
  if(WiFi.status() == WL_CONNECTED) {
      char buf[128];
      snprintf(buf, sizeof(buf), "{\"ssid\":\"%s\",\"strength\":%d,\"channel\":%d}", WiFi.SSID(), WiFi.RSSI(), WiFi.channel());
      sockEmit.sendToClient(num, "wifiStrength", buf);
      this->lastRSSI = WiFi.RSSI();
      this->lastChannel = WiFi.channel();
  }
  else
    sockEmit.sendToClient(num, "wifiStrength", "{\"ssid\":\"\", \"strength\":-100,\"channel\":-1}");
}
void Network::setConnected() {
  WiFi.hostname(settings.WIFI.hostname);
  this->ssid = WiFi.SSID();
  this->mac = WiFi.BSSIDstr();
  this->strength = WiFi.RSSI();
  this->channel = WiFi.channel();
  this->connectTime = millis();
  if(this->connectAttempts == 1) {
    Serial.println();
    Serial.print("Successfully Connected to WiFi!!!!");
    Serial.print(WiFi.localIP());
    Serial.print(" (");
    Serial.print(this->strength);
    Serial.println("dbm)");
  }
  else {
    Serial.println();
    Serial.print("Reconnected after ");
    Serial.print(1.0 * (millis() - this->connectStart)/1000);
    Serial.print("sec IP: ");
    Serial.print(WiFi.localIP());
    Serial.print(" ");
    Serial.print(this->mac);
    Serial.print(" CH:");
    Serial.print(this->channel);
    Serial.print(" (");
    Serial.print(this->strength);
    Serial.print(" dBm)");
    Serial.print(" Disconnected ");
    Serial.print(this->connectAttempts - 1);
    Serial.println(" times");
  }
  SSDP.setHTTPPort(80);
  SSDP.setSchemaURL(0, "upnp.xml");
  SSDP.setChipId(0, this->getChipId());
  SSDP.setDeviceType(0, "urn:schemas-rstrouse-org:device:ESPSomfyRTS:1");
  SSDP.setName(0, settings.WIFI.hostname);
  
  //SSDP.setSerialNumber(0, "C2496952-5610-47E6-A968-2FC19737A0DB");
  //SSDP.setUUID(0, settings.uuid);
  SSDP.setModelName(0, "ESPSomfy RTS");
  SSDP.setModelNumber(0, "SS v1");
  SSDP.setModelURL(0, "https://github.com/rstrouse/ESPSomfy-RTS");
  SSDP.setManufacturer(0, "rstrouse");
  SSDP.setManufacturerURL(0, "https://github.com/rstrouse");
  SSDP.setURL(0, "/");
  if(MDNS.begin(settings.WIFI.hostname)) {
    Serial.printf("MDNS Responder Started: serverId=%s\n", settings.WIFI.serverId);
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "board", "ESP32");
    MDNS.addServiceTxt("http", "tcp", "model", "ESPSomfyRTS");
    
    MDNS.addService("espsomfy_rts", "tcp", 8080);
    MDNS.addServiceTxt("espsomfy_rts", "tcp", "serverId", String(settings.WIFI.serverId));
    MDNS.addServiceTxt("espsomfy_rts", "tcp", "model", "ESPSomfyRTS");
    MDNS.addServiceTxt("espsomfy_rts", "tcp", "version", String(settings.fwVersion));
  }
  if(settings.WIFI.ssdpBroadcast) {
    if(SSDP.begin()) Serial.println("SSDP Client Started..."); 
  }
  else if(SSDP.isStarted) SSDP.end();
  this->emitSockets();
}
bool Network::connect() {
  if(settings.WIFI.hostname[0] != '\0') WiFi.hostname(settings.WIFI.hostname);
  if(settings.WIFI.ssid[0] != '\0') {
    if(WiFi.status() == WL_CONNECTED && WiFi.SSID().compareTo(settings.WIFI.ssid) == 0) {
      this->disconnected = 0;
      return true;
    }
    if(this->connectAttempts > 0) {
      Serial.print("Connection Lost...");
      Serial.print(this->mac);
      Serial.print(" CH:");
      Serial.print(this->channel);
      Serial.print(" (");
      Serial.print(this->strength);
      Serial.println("dbm)  ");
    }
    else Serial.println("Connecting to AP");
    this->connectAttempts++;
    this->connectStart = millis();
    Serial.print("Set hostname to:");
    Serial.println(WiFi.getHostname());
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.mode(WIFI_STA);
    delay(100);
    WiFi.begin(settings.WIFI.ssid, settings.WIFI.passphrase);
    delay(100);
    int retries = 0;
    while(retries < 100) {
      //digitalWrite(LED_BUILTIN, retries % 2 ? HIGH : LOW); // Flash the LED while connecting
      switch(WiFi.status()) {
        case WL_SCAN_COMPLETED:
          Serial.println("Status: Scan Completed");
          break;
        case WL_CONNECT_FAILED:
          if(this->connectAttempts == 1) Serial.println();
          Serial.println("WiFi Module connection failed");
          return false;
        case WL_DISCONNECTED:
          break;
        case WL_IDLE_STATUS:
          Serial.print("*");
          break;
        case WL_CONNECTED:
          this->setConnected();
          WiFi.setSleep(false);
          return true;
        case WL_NO_SHIELD:
          Serial.println("Connection failed - WiFi module not found");
          return false;
        case WL_NO_SSID_AVAIL:
          Serial.print(" Connection failed the SSID ");
          Serial.print(settings.WIFI.ssid);
          Serial.print(" could not be found");
          return false;
      }
      delay(500);
      if(connectAttempts == 1) Serial.print("*");
      retries++;
    }
   if(this->connectAttempts != 1) {
      int st = this->getStrengthBySSID(settings.WIFI.ssid);
      Serial.print("(");
      Serial.print(st);
      Serial.print("dBm) ");
      Serial.println("Failed");
      //if(disconnected > 0 && st == -100) settings.WIFI.PrintNetworks();
      disconnected++;
    }
  
  }
  //digitalWrite(LED_BUILTIN, HIGH); // Turn off the LED.
  return false;
}
int Network::getStrengthByMac(const char *macAddr) {
  int strength = -100;
  int n = WiFi.scanNetworks(true);
  for(int i = 0; i < n; i++) {
    if(WiFi.BSSIDstr(i).compareTo(macAddr) == 0) return WiFi.RSSI(i);
  }
}
uint32_t Network::getChipId() {
  uint32_t chipId = 0;
  uint64_t mac = ESP.getEfuseMac();
  for(int i=0; i<17; i=i+8) {
    chipId |= ((mac >> (40 - i)) & 0xff) << i;
  }
  return chipId;
}
int Network::getStrengthBySSID(const char *ssid) {
  int strength = -100;
  int n = WiFi.scanNetworks(false, true);
  for(int i = 0; i < n; i++) {
    if(WiFi.SSID(i).compareTo(ssid) == 0) strength = max(WiFi.RSSI(i), strength);
  }
  if(strength == -100) {
    Serial.print("Could not find network [");
    Serial.print(ssid);
    Serial.print("] Scanned ");
    Serial.print(n);
    Serial.println(" Networks...");
    String network;
    uint8_t encType;
    int32_t RSSI;
    uint8_t* BSSID;
    int32_t channel;
    bool isHidden;
    for(int i = 0; i < n; i++) {
      //WiFi.getNetworkInfo(i, network, encType, RSSI, BSSID, channel, isHidden);
      if(network.compareTo(this->ssid) == 0) Serial.print("*");
      else Serial.print(" ");
      Serial.print(i);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i).c_str());
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print("dBm) CH:");
      Serial.print(WiFi.channel(i));
      Serial.print(" MAC:");
      Serial.print(WiFi.BSSIDstr(i).c_str());
      if(isHidden) Serial.print(" [hidden]");
      Serial.println();
    }
  }
  return strength;
}
bool Network::openSoftAP() {
  Serial.println();
  Serial.println("Turning the HotSpot On");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  delay(100);
  WiFi.softAP("ESPSomfy RTS", "");
  Serial.println("Initializing AP for credentials modification");
  Serial.println();
  Serial.print("SoftAP IP: ");
  Serial.println(WiFi.softAPIP());
  pinMode(D0, INPUT_PULLUP);
  long startTime = millis();
  int c = 0;
  
  while ((WiFi.status() != WL_CONNECTED))
  {
    int clients = WiFi.softAPgetStationNum();
    webServer.loop();
    if(millis() - this->lastEmit > 1500) {
      //if(this->connect()) {}
      this->lastEmit = millis();
      this->emitSockets();
      if(clients > 0)
        Serial.print(clients);
      else
        Serial.print(".");
      c++;
    }
    sockEmit.loop();
    if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
      this->end();
      ESP.restart();
      break;
    }

    // If no clients have connected in 3 minutes from starting this server reboot this pig.  This will
    // force a reboot cycle until we have some response.  That is unless the SSID has been cleared.
    if(clients == 0 && strlen(settings.WIFI.ssid) > 0 && millis() - startTime > 3 * 60000) {
      Serial.println();
      Serial.println("Stopping AP Mode");
      WiFi.softAPdisconnect(true);
      return false;
    }
    if(digitalRead(D0) == LOW) {
      Serial.println();
      Serial.println("Button Pressed...Stopping AP Mode");
      WiFi.softAPdisconnect(true);
      return false;
    }
    if(c == 100) {
      Serial.println();
      c = 0;
    }
    yield();
  }
}
