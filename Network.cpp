#include <ETH.h>
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
extern Network net;

static bool _apScanning = false;
int connectRetries = 0;
void Network::end() {
  sockEmit.end();
  SSDP.end();
  mqtt.end();
  delay(100);
}
bool Network::setup() {
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.persistent(false);
  WiFi.onEvent(this->networkEvent);
  if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(true, true);
  if(settings.connType == conn_types::wifi || settings.connType == conn_types::unset) {
    WiFi.persistent(false);
    if(settings.hostname[0] != '\0') WiFi.setHostname(settings.hostname);
    Serial.print("WiFi Mode: ");
    Serial.println(WiFi.getMode());
    WiFi.mode(WIFI_STA);
    settings.WIFI.printNetworks();
  }
  sockEmit.begin();
  if(!this->connect()) this->openSoftAP();
  return true;
}
void Network::loop() {
  if(millis() - this->lastEmit > 1500) {
    this->lastEmit = millis();
    if(!this->softAPOpened) {
      while(!this->connect()) {
        // If we lost our connection
        connectRetries++;
        if(connectRetries > 100) {
          if(!this->connected()) this->openSoftAP();
          break;
        }
        sockEmit.loop();
      }
      connectRetries = 0;
    }
    this->emitSockets();
    this->lastEmit = millis();
    if(!this->connected()) return;
  }
  if(this->connected() && millis() - this->lastMDNS > 60000) {
    // We are doing this every 60 seconds because of the BS related to
    // the MDNS library.  The original library required manual updates
    // to the MDNS or it would lose its hostname after 2 minutes.
    if(this->lastMDNS != 0) MDNS.setInstanceName(settings.hostname);
    // Every 60 seconds we are going to look at wifi connectivity
    // to get around the roaming issues with ESP32.  We will try to do this in an async manner.  If
    // there is a channel that is better we will stop the radio and reconnect
    if(this->connType == conn_types::wifi && settings.WIFI.roaming && !this->softAPOpened) {
      // If we are not already scanning then we need to start a passive scan
      // and only respond if there is a better connection. 
      // 1. If there is currently a waiting scan don't do anything
      if(!_apScanning && WiFi.scanNetworks(true, false, true, 300, 0, settings.WIFI.ssid) == -1) {
        _apScanning = true;
      }
    }
    this->lastMDNS = millis();
  }
  if(_apScanning) {
    if(!settings.WIFI.roaming || this->connType != conn_types::wifi || this->softAPOpened) _apScanning = false;
    else {
      uint16_t n = WiFi.scanComplete();
      if( n > 0) {
        _apScanning = false;
        uint8_t bssid[6];
        int32_t channel = 0;
        if(this->getStrongestAP(settings.WIFI.ssid, bssid, &channel)) {
          if(memcmp(bssid, WiFi.BSSID(), sizeof(bssid)) != 0) {
            Serial.printf("Found stronger AP %d %02X:%02X:%02X:%02X:%02X:%02X\n", channel, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
            this->changeAP(bssid, channel);
          }
        }
      }
    }
  }
  if(settings.ssdpBroadcast) {
    if(!SSDP.isStarted) SSDP.begin();
    if(SSDP.isStarted) SSDP.loop();
  }
  else if(!settings.ssdpBroadcast && SSDP.isStarted) SSDP.end();
  mqtt.loop();
}
bool Network::changeAP(const uint8_t *bssid, const int32_t channel) {
  if(SSDP.isStarted) SSDP.end();
  mqtt.disconnect();
  WiFi.disconnect(false, true);
  WiFi.begin(settings.WIFI.ssid, settings.WIFI.passphrase, channel, bssid);
  uint8_t retries = 0;
  while(retries < 100) {
    wl_status_t stat = WiFi.status();
    if(stat == WL_CONNECTED) {
      Serial.println("WiFi module connected");
      this->ssid = WiFi.SSID();
      this->mac = WiFi.BSSIDstr();
      this->strength = WiFi.RSSI();
      this->channel = WiFi.channel();
      return true;
    }
    else if(stat == WL_CONNECT_FAILED) {
      Serial.println("WiFi Module connection failed");
      return false;
    }
    else if(stat == WL_NO_SSID_AVAIL) {
        Serial.println(" Connection failed the SSID ");
        return false;
    }
    else if(stat == WL_NO_SHIELD) {
        Serial.println("Connection failed - WiFi module not found");
        return false;
    }
    else if(stat == WL_IDLE_STATUS) {
        Serial.print("*");
    }
    else if(stat == WL_DISCONNECTED) {
        Serial.print("-");
    }
    else {
      Serial.printf("Unknown status %d\n", stat);
    }
    delay(300);
  }
  return false;
}
void Network::emitSockets() {
  if(this->needsBroadcast || 
    (this->connType == conn_types::wifi && (abs(abs(WiFi.RSSI()) - abs(this->lastRSSI)) > 1 || WiFi.channel() != this->lastChannel))) {
    this->emitSockets(255);
    sockEmit.loop();
    this->needsBroadcast = false;
  }
}
void Network::emitSockets(uint8_t num) {
  char buf[128];
  if(this->connType == conn_types::ethernet) {
      JsonSockEvent *json = sockEmit.beginEmit("ethernet");
      json->beginObject();
      json->addElem("connected", this->connected());
      json->addElem("speed", ETH.linkSpeed());
      json->addElem("fullduplex", ETH.fullDuplex());
      json->endObject();
      sockEmit.endEmit(num);
      /*
      snprintf(buf, sizeof(buf), "{\"connected\":%s,\"speed\":%d,\"fullduplex\":%s}", this->connected() ? "true" : "false", ETH.linkSpeed(), ETH.fullDuplex() ? "true" : "false");
      if(num == 255) 
        sockEmit.sendToClients("ethernet", buf);
      else
        sockEmit.sendToClient(num, "ethernet", buf);
      */
  }
  else {
      if(WiFi.status() == WL_CONNECTED) {
        JsonSockEvent *json = sockEmit.beginEmit("wifiStrength");
        json->beginObject();
        json->addElem("ssid", WiFi.SSID().c_str());
        json->addElem("strength", (uint32_t)WiFi.RSSI());
        json->addElem("channel", (uint32_t)this->channel);
        json->endObject();
        sockEmit.endEmit(num);
        /*
        snprintf(buf, sizeof(buf), "{\"ssid\":\"%s\",\"strength\":%d,\"channel\":%d}", WiFi.SSID().c_str(), WiFi.RSSI(), this->channel);
        if(num == 255)
          sockEmit.sendToClients("wifiStrength", buf);
        else
          sockEmit.sendToClient(num, "wifiStrength", buf);
        */
        this->lastRSSI = WiFi.RSSI();
        this->lastChannel = WiFi.channel();
      }
      else {
        JsonSockEvent *json = sockEmit.beginEmit("wifiStrength");
        json->beginObject();
        json->addElem("ssid", "");
        json->addElem("strength", (int8_t)-100);
        json->addElem("channel", (int8_t)-1);
        json->endObject();
        sockEmit.endEmit(num);
        
        json = sockEmit.beginEmit("ethernet");
        json->beginObject();
        json->addElem("connected", false);
        json->addElem("speed", (uint8_t)0);
        json->addElem("fullduplex", false);
        json->endObject();
        sockEmit.endEmit(num);
        /*

        if(num == 255) {
          sockEmit.sendToClients("wifiStrength", "{\"ssid\":\"\", \"strength\":-100,\"channel\":-1}");
          sockEmit.sendToClients("ethernet", "{\"connected\":false,\"speed\":0,\"fullduplex\":false}");
        }
        else {
          sockEmit.sendToClient(num, "wifiStrength", "{\"ssid\":\"\", \"strength\":-100,\"channel\":-1}");
          sockEmit.sendToClient(num, "ethernet", "{\"connected\":false,\"speed\":0,\"fullduplex\":false}");
        }
        */
        this->lastRSSI = -100;
        this->lastChannel = -1;
      }
  }
}
void Network::setConnected(conn_types connType) {
  this->connType = connType;
  this->connectTime = millis();
  connectRetries = 0;
  if(this->connType == conn_types::wifi) {
    if(this->softAPOpened) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
    }
    this->ssid = WiFi.SSID();
    this->mac = WiFi.BSSIDstr();
    this->strength = WiFi.RSSI();
    this->channel = WiFi.channel();
  }
  else if(this->connType == conn_types::ethernet) {
    if(this->softAPOpened) {
      Serial.println("Disonnecting from SoftAP");
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_OFF);
    }
    this->wifiFallback = false;
  }
  if(this->connectAttempts == 1) {
    Serial.println();
    if(this->connType == conn_types::wifi) {
      Serial.print("Successfully Connected to WiFi!!!!");
      Serial.print(WiFi.localIP());
      Serial.print(" (");
      Serial.print(this->strength);
      Serial.println("dbm)");
      if(settings.IP.dhcp) {
        settings.IP.ip = WiFi.localIP();
        settings.IP.subnet = WiFi.subnetMask();
        settings.IP.gateway = WiFi.gatewayIP();
        settings.IP.dns1 = WiFi.dnsIP(0);
        settings.IP.dns2 = WiFi.dnsIP(1);
      }
    }
    else {
      Serial.print("Successfully Connected to Ethernet!!! ");
      Serial.print(ETH.localIP());
      if(ETH.fullDuplex()) {
        Serial.print(" FULL DUPLEX");
      }
      Serial.print(" ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      if(settings.IP.dhcp) {
        settings.IP.ip = ETH.localIP();
        settings.IP.subnet = ETH.subnetMask();
        settings.IP.gateway = ETH.gatewayIP();
        settings.IP.dns1 = ETH.dnsIP(0);
        settings.IP.dns2 = ETH.dnsIP(1);
      }
      JsonSockEvent *json = sockEmit.beginEmit("ethernet");
      json->beginObject();
      json->addElem("connected", this->connected());
      json->addElem("speed", ETH.linkSpeed());
      json->addElem("fullduplex", ETH.fullDuplex());
      json->endObject();
      sockEmit.endEmit();
      /*
      char buf[128];
      snprintf(buf, sizeof(buf), "{\"connected\":true,\"speed\":%d,\"fullduplex\":%s}", ETH.linkSpeed(), ETH.fullDuplex() ? "true" : "false");
      sockEmit.sendToClients("ethernet", buf);
      */
    }
  }
  else {
    Serial.println();
    Serial.print("Reconnected after ");
    Serial.print(1.0 * (millis() - this->connectStart)/1000);
      Serial.print("sec IP: ");
    if(this->connType == conn_types::wifi) {
      Serial.print(WiFi.localIP());
      Serial.print(" ");
      Serial.print(this->mac);
      Serial.print(" CH:");
      Serial.print(this->channel);
      Serial.print(" (");
      Serial.print(this->strength);
      Serial.print(" dBm)");
    }
    else {
      Serial.print(ETH.localIP());
      if(ETH.fullDuplex()) {
        Serial.print(" FULL DUPLEX");
      }
      Serial.print(" ");
      Serial.print(ETH.linkSpeed());
      Serial.print("Mbps");
    }
    Serial.print(" Disconnected ");
    Serial.print(this->connectAttempts - 1);
    Serial.println(" times");
  }
  SSDP.setHTTPPort(80);
  SSDP.setSchemaURL(0, "upnp.xml");
  SSDP.setChipId(0, this->getChipId());
  SSDP.setDeviceType(0, "urn:schemas-rstrouse-org:device:ESPSomfyRTS:1");
  SSDP.setName(0, settings.hostname);
  
  //SSDP.setSerialNumber(0, "C2496952-5610-47E6-A968-2FC19737A0DB");
  //SSDP.setUUID(0, settings.uuid);
  SSDP.setModelName(0, "ESPSomfy RTS");
  if(strlen(settings.chipModel) == 0) SSDP.setModelNumber(0, "ESP32");
  else {
    char sModel[20] = "";
    snprintf(sModel, sizeof(sModel), "ESP32-%S", settings.chipModel);
    SSDP.setModelNumber(0, sModel);
  }
  SSDP.setModelURL(0, "https://github.com/rstrouse/ESPSomfy-RTS");
  SSDP.setManufacturer(0, "rstrouse");
  SSDP.setManufacturerURL(0, "https://github.com/rstrouse");
  SSDP.setURL(0, "/");
  SSDP.setActive(0, true);
  if(MDNS.begin(settings.hostname)) {
    Serial.printf("MDNS Responder Started: serverId=%s\n", settings.serverId);
    //MDNS.addService("http", "tcp", 80);
    //MDNS.addServiceTxt("http", "tcp", "board", "ESP32");
    //MDNS.addServiceTxt("http", "tcp", "model", "ESPSomfyRTS");
    
    MDNS.addService("espsomfy_rts", "tcp", 8080);
    MDNS.addServiceTxt("espsomfy_rts", "tcp", "serverId", String(settings.serverId));
    MDNS.addServiceTxt("espsomfy_rts", "tcp", "model", "ESPSomfyRTS");
    MDNS.addServiceTxt("espsomfy_rts", "tcp", "version", String(settings.fwVersion.name));
  }
  if(settings.ssdpBroadcast) {
    SSDP.begin();
  }
  else if(SSDP.isStarted) SSDP.end();
  this->emitSockets();
  settings.printAvailHeap();
  this->needsBroadcast = true;
}
bool Network::connectWired() {
  //if(this->connType == conn_types::ethernet && ETH.linkUp()) {
  if(ETH.linkUp()) {
    if(WiFi.status() == WL_CONNECTED) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
    if(this->connType != conn_types::ethernet) this->setConnected(conn_types::ethernet);
    this->wifiFallback = false;
    return true;
  }
  else if(this->ethStarted) {
    if(settings.connType == conn_types::ethernetpref && settings.WIFI.ssid[0] != '\0')
      return this->connectWiFi();
  }
  if(this->connectAttempts > 0) {
    Serial.printf("Ethernet Connection Lost... %d Reconnecting ", this->connectAttempts);
    Serial.println(this->mac);
  }
  else
    Serial.println("Connecting to Wired Ethernet");
  this->connectAttempts++;
  if(!this->ethStarted) {
      this->ethStarted = true;
      WiFi.mode(WIFI_OFF);
      if(settings.hostname[0] != '\0') 
        ETH.setHostname(settings.hostname);
      else
        ETH.setHostname("ESPSomfy-RTS");
     
      Serial.print("Set hostname to:");
      Serial.println(ETH.getHostname());
      if(!ETH.begin(settings.Ethernet.phyAddress, settings.Ethernet.PWRPin, settings.Ethernet.MDCPin, settings.Ethernet.MDIOPin, settings.Ethernet.phyType, settings.Ethernet.CLKMode)) { 
          Serial.println("Ethernet Begin failed");
          this->ethStarted = false;
          if(settings.connType == conn_types::ethernetpref) {
            this->wifiFallback = true;
            return connectWiFi();
          }
          return false;
      }
      else {
        if(!settings.IP.dhcp) {
          if(!ETH.config(settings.IP.ip, settings.IP.gateway, settings.IP.subnet, settings.IP.dns1, settings.IP.dns2)) {
              Serial.println("Unable to configure static IP address....");
              ETH.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
          }
        }
        else
            ETH.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        
        uint32_t wait = millis();
        while(millis() - wait < 14000) {
          if(ETH.linkUp() && ETH.localIP() != INADDR_NONE) {
            net.mac = ETH.macAddress();
            net.setConnected(conn_types::ethernet);
            return true;
          }
          delay(500);
        }
        if(settings.connType == conn_types::ethernetpref) {
          this->wifiFallback = true;
          return connectWiFi();
        }
      }
  }
  return false;
}
void Network::updateHostname() {
  if(settings.hostname[0] != '\0' && this->connected()) {
    if(this->connType == conn_types::ethernet &&
      strcmp(settings.hostname, ETH.getHostname()) != 0) {
      Serial.printf("Updating host name to %s...\n", settings.hostname);
      ETH.setHostname(settings.hostname);
      MDNS.setInstanceName(settings.hostname);        
      SSDP.setName(0, settings.hostname);
     }
     else if(strcmp(settings.hostname, WiFi.getHostname()) != 0) {
      Serial.printf("Updating host name to %s...\n", settings.hostname);
      WiFi.setHostname(settings.hostname);
      MDNS.setInstanceName(settings.hostname);        
      SSDP.setName(0, settings.hostname);
     }
  }
}
bool Network::connectWiFi() {
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
    WiFi.setSleep(false);
    WiFi.mode(WIFI_MODE_NULL);
    //WiFi.onEvent(this->networkEvent);

    if(!settings.IP.dhcp) {
      if(!WiFi.config(settings.IP.ip, settings.IP.gateway, settings.IP.subnet, settings.IP.dns1, settings.IP.dns2))
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }
    else
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    delay(100);
    // There is also another method simply called hostname() but this is legacy for esp8266.
    if(settings.hostname[0] != '\0') WiFi.setHostname(settings.hostname);
    Serial.print("Set hostname to:");
    Serial.println(WiFi.getHostname());
    WiFi.mode(WIFI_STA);
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    uint8_t bssid[6];
    int32_t channel = 0;
    if(this->getStrongestAP(settings.WIFI.ssid, bssid, &channel)) {
      Serial.printf("Found strongest AP %d %02X:%02X:%02X:%02X:%02X:%02X\n", channel, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
      WiFi.begin(settings.WIFI.ssid, settings.WIFI.passphrase, channel, bssid);
    }
    else
      WiFi.begin(settings.WIFI.ssid, settings.WIFI.passphrase);
    delay(100);
    int retries = 0;
    while(retries < 100) {
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
          //WiFi.hostname(settings.hostname);
          this->ssid = WiFi.SSID();
          this->mac = WiFi.BSSIDstr();
          this->strength = WiFi.RSSI();
          this->channel = WiFi.channel();
          this->setConnected(conn_types::wifi);
          WiFi.setSleep(false);
          return true;
        case WL_NO_SHIELD:
          Serial.println("Connection failed - WiFi module not found");
          return false;
        case WL_NO_SSID_AVAIL:
          Serial.print(" Connection failed the SSID ");
          Serial.print(settings.WIFI.ssid);
          Serial.println(" could not be found");
          return false;
        default:
          break;
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
  return false;
}
bool Network::connect() {
  if(settings.connType == conn_types::unset) return true;
  else if(settings.connType == conn_types::ethernet || (settings.connType == conn_types::ethernetpref)) {
    bool bConnected = this->connectWired();
    if(!bConnected && settings.connType == conn_types::ethernetpref && settings.WIFI.ssid[0] != '\0') {
      bConnected = this->connectWiFi();
      this->wifiFallback = true;
    }
    return bConnected;
  }
  return this->connectWiFi();
}
/* DEPRECATED 03-02-24
int Network::getStrengthByMac(const char *macAddr) {
  int n = WiFi.scanNetworks(true);
  for(int i = 0; i < n; i++) {
    if (WiFi.BSSIDstr(i).compareTo(macAddr) == 0)
      return WiFi.RSSI(i);
  }
  return -100;
}
*/
uint32_t Network::getChipId() {
  uint32_t chipId = 0;
  uint64_t mac = ESP.getEfuseMac();
  for(int i=0; i<17; i=i+8) {
    chipId |= ((mac >> (40 - i)) & 0xff) << i;
  }
  return chipId;
}

bool Network::getStrongestAP(const char *ssid, uint8_t *bssid, int32_t *channel) {
  // The new AP must be at least 10dbm greater.
  int32_t strength = this->connected() ? WiFi.RSSI() + 10 : -127;
  int32_t chan = -1;
  memset(bssid, 0x00, 6);
  uint8_t n = this->connected() ? WiFi.scanComplete() : WiFi.scanNetworks(false, false, false, 300, 0, ssid);
  for(uint8_t i = 0; i < n; i++) {
    if(WiFi.SSID(i).compareTo(ssid) == 0) {
      if(WiFi.RSSI(i) > strength) { 
        strength = WiFi.RSSI(i); 
        memcpy(bssid, WiFi.BSSID(i), 6); 
        *channel = chan = WiFi.channel(i);
      }
    }
  }  
  WiFi.scanDelete();
  return chan > 0;
}
int Network::getStrengthBySSID(const char *ssid) {
  int32_t strength = -100;
  int n = WiFi.scanNetworks(false, false);
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
      Serial.println();
    }
  }
  return strength;
}
bool Network::openSoftAP() {
  Serial.println();
  Serial.println("Turning the HotSpot On");
  WiFi.disconnect(true);
  WiFi.hostname("ESPSomfy RTS");
  WiFi.mode(WIFI_AP_STA);
  delay(100);
  WiFi.softAP("ESPSomfy RTS", "");
  Serial.println("Initializing AP for credentials modification");
  Serial.println();
  Serial.print("SoftAP IP: ");
  Serial.println(WiFi.softAPIP());
  //pinMode(D0, INPUT_PULLUP);
  long startTime = millis();
  int c = 0;
  
  while (!this->connected())
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
    if(clients == 0 && 
      (strlen(settings.WIFI.ssid) > 0 || settings.connType == conn_types::ethernet || settings.connType == conn_types::ethernetpref) && 
      millis() - startTime > 3 * 60000) {
      Serial.println();
      Serial.println("Stopping AP Mode");
      WiFi.softAPdisconnect(true);
      return false;
    }
    if(c == 100) {
      Serial.println();
      c = 0;
    }
    yield();
  }
  return true;
}
bool Network::connected() {
  if(this->connType == conn_types::unset) return false;
  else if(this->connType == conn_types::wifi) return WiFi.status() == WL_CONNECTED;
  else if(this->connType == conn_types::ethernet) return ETH.linkUp();
  else return this->connType != conn_types::unset;
  return false;
}
void Network::networkEvent(WiFiEvent_t event) {
  switch(event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("Ethernet Started");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      // If the Wifi is connected then drop that connection
      if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
      Serial.print("Got Ethernet IP ");
      Serial.println(ETH.localIP());
      break;
/*
    case ARDUINO_EVENT_ETH_LOST_IP:
      Serial.println("Ethernet Lost IP");
      sockEmit.sendToClients("ethernet", "{\"connected\":false, \"speed\":0,\"fullduplex\":false}");
      net.connType = conn_types::unset;
      break;
*/
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.print("Ethernet Connected ");
      // We don't want to call setConnected if we do not have an IP address yet
      //if(ETH.localIP() != INADDR_NONE)
      //  net.setConnected(conn_types::ethernet);
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("Ethernet Disconnected");
      {
        JsonSockEvent *json = sockEmit.beginEmit("ethernet");
        json->beginObject();
        json->addElem("connected", false);
        json->addElem("speed", (uint8_t)0);
        json->addElem("fullduplex", false);
        json->endObject();
        sockEmit.endEmit();
      }
      /*
      sockEmit.sendToClients("ethernet", "{\"connected\":false,\"speed\":0,\"fullduplex\":false}");
      */
      net.connType = conn_types::unset;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("Ethernet Stopped");
      net.connType = conn_types::unset;
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      Serial.println("WiFi AP Stopped");
      net.softAPOpened = false;
      break;      
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.println("WiFi AP Started");
      net.softAPOpened = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_START:
      if(settings.hostname[0] != '\0') WiFi.setHostname(settings.hostname);
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      break;
    default:
      if(event > ARDUINO_EVENT_ETH_START)
        Serial.printf("Unknown Ethernet Event %d\n", event);
      break;
  }
}
