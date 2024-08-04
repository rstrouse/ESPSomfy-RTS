#include <ETH.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>
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
extern SomfyShadeController somfy;

static unsigned long _lastHeapEmit = 0;

static bool _apScanning = false;
static uint32_t _lastMaxHeap = 0;
static uint32_t _lastHeap = 0;
int connectRetries = 0;
void Network::end() {
  SSDP.end();
  mqtt.end();
  sockEmit.end();
  delay(100);
}
bool Network::setup() {
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.onEvent(this->networkEvent);
  this->disconnectTime = millis();
  if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(true, true);
  if(settings.connType == conn_types_t::wifi || settings.connType == conn_types_t::unset) {
    WiFi.persistent(false);
    if(settings.hostname[0] != '\0') WiFi.setHostname(settings.hostname);
    Serial.print("WiFi Mode: ");
    Serial.println(WiFi.getMode());
    WiFi.mode(WIFI_STA);
  }
  sockEmit.begin();
  return true;
}
conn_types_t Network::preferredConnType() {
  switch(settings.connType) {
    case conn_types_t::wifi:    
      return settings.WIFI.ssid[0] != '\0' ? conn_types_t::wifi : conn_types_t::ap;
    case conn_types_t::unset:
    case conn_types_t::ap:
      return conn_types_t::ap;
    case conn_types_t::ethernetpref:
      return settings.WIFI.ssid[0] != '\0' && (!ETH.linkUp() && this->ethStarted) ? conn_types_t::wifi : conn_types_t::ethernet;
    case conn_types_t::ethernet:
      return ETH.linkUp() || !this->ethStarted ? conn_types_t::ethernet : conn_types_t::ap;
    default:
      return settings.connType; 
  }
}
void Network::loop() {
  // ORDER OF OPERATIONS:
  // ----------------------------------------------
  // 1. If we are in the middle of a connection process we need to simply bail after the connect method.  The
  //    connect method will take care of our target connection for us.
  // 2. Check to see what type of target connection we need.  
  //    a. If this is an ethernet target then the connection needs to perform a fallback if applicable.
  //    b. If this is a wifi target then we need to first check to see if the SSID is available.
  //    c. If an SSID has not been set then we need to turn on the Soft AP.
  // 3. If the Soft AP is open and the target is either wifi, ethernet, or ethernetpref then
  //    we need to shut it down if there are no connections and the preferred connection is available.
  //    a. Ethernet: Check for an active ethernet connection.  We cannot rely on linkup because the PHY will
  //       report that the link is up when no IP address is being served.
  //    b. WiFi: Perform synchronous scan for APs related to the SSID.  If the SSID can be found then perform
  //       the connection process for the WiFi connection.
  //    c. SoftAP: This condition retains the Soft AP because no other connection method is available.
  conn_types_t ctype = this->preferredConnType();
  this->connect(ctype); // Connection timeout handled in connect function as well as the opening of the Soft AP if needed.
  if(this->connecting()) return; // If we are currently attempting to connect to something then we need to bail here.
  if(_apScanning) {
    if(settings.WIFI.hidden ||                                    // This user has elected to use a hidden AP.
      (this->connected() && !settings.WIFI.roaming) ||            // We are already connected and should not be roaming.
      (this->softAPOpened && WiFi.softAPgetStationNum() != 0) ||  // The Soft AP is open and a user is connected.
      (ctype != conn_types_t::wifi)) {                            // The Ethernet link is up so we should ignore this scan.
      Serial.println("Cancelling WiFi STA Scan...");
      _apScanning = false;
      WiFi.scanDelete();
    }
    else {
      int16_t n = WiFi.scanComplete();
      if( n >= 0) { // If the scan is complete but the WiFi isn't ready this can return 0.
        uint8_t bssid[6];
        int32_t channel = 0;
        if(this->getStrongestAP(settings.WIFI.ssid, bssid, &channel)) {
          if(!WiFi.BSSID() || memcmp(bssid, WiFi.BSSID(), sizeof(bssid)) != 0) {
            if(!this->connected()) {
              Serial.printf("Connecting to AP %02X:%02X:%02X:%02X:%02X:%02X CH: %d\n", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], channel);
              this->connectWiFi(bssid, channel);
            }
            else {
              Serial.printf("Found stronger AP %02X:%02X:%02X:%02X:%02X:%02X CH: %d\n", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], channel);
              this->changeAP(bssid, channel);
            }
          }
        }
        _apScanning = false;
      }
    }
  }
  if(!this->connecting() && !settings.WIFI.hidden) {
    if((this->softAPOpened && WiFi.softAPgetStationNum() == 0) ||
      (!this->connected() && ctype == conn_types_t::wifi)) {
      // If the Soft AP is opened and there are no clients connected then we need to scan for an AP.  If
      // our target exists we will exit out of the Soft AP and start that connection.  We are also
      // going to continuously scan when there is no connection and our preferred connection is wifi.
      if(ctype == conn_types_t::wifi) {
        // Scan for an AP but only if we are not already scanning.
        if(!_apScanning && WiFi.scanNetworks(true, false, true, 300, 0, settings.WIFI.ssid) == -1) {
          _apScanning = true;
        }
      }
    }
    else if(this->connected() && ctype == conn_types_t::wifi && settings.WIFI.roaming) {
      // Periodically look for a roaming AP.
      if(millis() > SSID_SCAN_INTERVAL + this->lastWifiScan) {
        //Serial.println("Started scan for access points");
        if(!_apScanning && WiFi.scanNetworks(true, false, true, 300, 0, settings.WIFI.ssid) == -1) {
          _apScanning = true;
          this->lastWifiScan = millis();
        }
      }
    }
  }
  if(millis() - this->lastEmit > 1500) {
    // Post our connection status if needed.
    this->lastEmit = millis();
    if(this->connected()) {
      this->emitSockets();
      this->lastEmit = millis();
    }
    esp_task_wdt_reset(); // Make sure we do not reboot here.
  }
  
  sockEmit.loop();
  mqtt.loop();
  if(settings.ssdpBroadcast && this->connected()) {
    if(!SSDP.isStarted) SSDP.begin();
    if(SSDP.isStarted) SSDP.loop();
  }
  else if(!settings.ssdpBroadcast && SSDP.isStarted) SSDP.end();
}
bool Network::changeAP(const uint8_t *bssid, const int32_t channel) {
  esp_task_wdt_reset(); // Make sure we do not reboot here.
  if(SSDP.isStarted) SSDP.end();
  mqtt.disconnect();
  //sockEmit.end();
  WiFi.disconnect(false, true);
  this->connType = conn_types_t::unset;
  this->_connecting = true;
  this->connectStart = millis();
  WiFi.begin(settings.WIFI.ssid, settings.WIFI.passphrase, channel, bssid);
  this->connectStart = millis();
  return false;
}
void Network::emitSockets() {
  this->emitHeap();
  if(this->needsBroadcast || 
    (this->connType == conn_types_t::wifi && (abs(abs(WiFi.RSSI()) - abs(this->lastRSSI)) > 1 || WiFi.channel() != this->lastChannel))) {
    this->emitSockets(255);
    sockEmit.loop();
    this->needsBroadcast = false;
  }
}
void Network::emitSockets(uint8_t num) {
  if(this->connType == conn_types_t::ethernet) {
      JsonSockEvent *json = sockEmit.beginEmit("ethernet");
      json->beginObject();
      json->addElem("connected", this->connected());
      json->addElem("speed", ETH.linkSpeed());
      json->addElem("fullduplex", ETH.fullDuplex());
      json->endObject();
      sockEmit.endEmit(num);
  }
  else {
      if(WiFi.status() == WL_CONNECTED) {
        JsonSockEvent *json = sockEmit.beginEmit("wifiStrength");
        json->beginObject();
        json->addElem("ssid", WiFi.SSID().c_str());
        json->addElem("strength", (int32_t)WiFi.RSSI());
        json->addElem("channel", (int32_t)this->channel);
        json->endObject();
        sockEmit.endEmit(num);
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
        this->lastRSSI = -100;
        this->lastChannel = -1;
      }
  }
  this->emitHeap(num);
}
void Network::setConnected(conn_types_t connType) {
  esp_task_wdt_reset();
  this->connType = connType;
  this->connectTime = millis();
  connectRetries = 0;
  Serial.println("Setting connected...");
  if(this->connType == conn_types_t::wifi) {
    if(this->softAPOpened && WiFi.softAPgetStationNum() == 0) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
    }
    this->_connecting = false;
    this->ssid = WiFi.SSID();
    this->mac = WiFi.BSSIDstr();
    this->strength = WiFi.RSSI();
    this->channel = WiFi.channel();
    this->connectAttempts++;
  }
  else if(this->connType == conn_types_t::ethernet) {
    if(this->softAPOpened) {
      Serial.println("Disonnecting from SoftAP");
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_OFF);
    }
    this->connectAttempts++;
    this->_connecting = false;
    this->wifiFallback = false;
  }
  // NET: Begin this in the startup.
  //sockEmit.begin();
  esp_task_wdt_reset();
  
  if(this->connectAttempts == 1) {
    Serial.println();
    if(this->connType == conn_types_t::wifi) {
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
      esp_task_wdt_reset();
      JsonSockEvent *json = sockEmit.beginEmit("ethernet");
      json->beginObject();
      json->addElem("connected", this->connected());
      json->addElem("speed", ETH.linkSpeed());
      json->addElem("fullduplex", ETH.fullDuplex());
      json->endObject();
      sockEmit.endEmit();
      esp_task_wdt_reset();
    }
  }
  else {
    Serial.println();
    Serial.print("Reconnected after ");
    Serial.print(1.0 * (millis() - this->connectStart)/1000);
      Serial.print("sec IP: ");
    if(this->connType == conn_types_t::wifi) {
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
    snprintf(sModel, sizeof(sModel), "ESP32-%s", settings.chipModel);
    SSDP.setModelNumber(0, sModel);
  }
  SSDP.setModelURL(0, "https://github.com/rstrouse/ESPSomfy-RTS");
  SSDP.setManufacturer(0, "rstrouse");
  SSDP.setManufacturerURL(0, "https://github.com/rstrouse");
  SSDP.setURL(0, "/");
  SSDP.setActive(0, true);
  esp_task_wdt_reset();
  if(MDNS.begin(settings.hostname)) {
    Serial.printf("MDNS Responder Started: serverId=%s\n", settings.serverId);
    MDNS.addService("http", "tcp", 80);
    //MDNS.addServiceTxt("http", "tcp", "board", "ESP32");
    //MDNS.addServiceTxt("http", "tcp", "model", "ESPSomfyRTS");
    
    MDNS.addService("espsomfy_rts", "tcp", 8080);
    MDNS.addServiceTxt("espsomfy_rts", "tcp", "serverId", String(settings.serverId));
    MDNS.addServiceTxt("espsomfy_rts", "tcp", "model", "ESPSomfyRTS");
    MDNS.addServiceTxt("espsomfy_rts", "tcp", "version", String(settings.fwVersion.name));
  }
  if(settings.ssdpBroadcast) {
    esp_task_wdt_reset();
    SSDP.begin();
  }
  else if(SSDP.isStarted) SSDP.end();
  esp_task_wdt_reset();
  this->emitSockets();
  settings.printAvailHeap();
  this->needsBroadcast = true;
}
bool Network::connectWired() {
  if(ETH.linkUp()) {
    // If the ethernet link is re-established then we need to shut down wifi.
    if(WiFi.status() == WL_CONNECTED) {
      //sockEmit.end();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
    if(this->connType != conn_types_t::ethernet) this->setConnected(conn_types_t::ethernet);
    return true;
  }
  else if(this->ethStarted) {
    // There is no wired connection so we need to fallback if appropriate.
    if(settings.connType == conn_types_t::ethernetpref && settings.WIFI.ssid[0] != '\0')
      return this->connectWiFi();
  }
  if(this->connectAttempts > 0) {
    Serial.printf("Ethernet Connection Lost... %d Reconnecting ", this->connectAttempts);
    Serial.println(this->mac);
  }
  else
    Serial.println("Connecting to Wired Ethernet");
  this->_connecting = true;
  this->connTarget = conn_types_t::ethernet;
  this->connType = conn_types_t::unset;
  if(!this->ethStarted) {
    // Currently the ethernet module will leak memory if you call begin more than once.
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
      if(settings.connType == conn_types_t::ethernetpref) {
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
    }
  }
  this->connectStart = millis();
  return true;
}
void Network::updateHostname() {
  if(settings.hostname[0] != '\0' && this->connected()) {
    if(this->connType == conn_types_t::ethernet &&
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
bool Network::connectWiFi(const uint8_t *bssid, const int32_t channel) {
  if(this->softAPOpened && WiFi.softAPgetStationNum() > 0) {
    // There is a client connected to the soft AP.  We do not want to close out the connection.  While both the
    // Soft AP and a wifi connection can coexist on ESP32 the performance is abysmal.
    WiFi.disconnect(false);
    this->_connecting = false;
    this->connType = conn_types_t::unset;
    return true;
  }
  WiFi.setSleep(false);
  if(!settings.IP.dhcp) {
    if(!WiFi.config(settings.IP.ip, settings.IP.gateway, settings.IP.subnet, settings.IP.dns1, settings.IP.dns2))
      WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  }
  else
      WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  if(settings.hostname[0] != '\0') WiFi.setHostname(settings.hostname);
  delay(100);
  
  if(bssid && channel > 0) {
    if(WiFi.status() == WL_CONNECTED && WiFi.SSID().compareTo(settings.WIFI.ssid) == 0
      && WiFi.channel() == channel) {
      this->disconnected = 0;
      return true;
    }
    this->connTarget = conn_types_t::wifi;
    this->connType = conn_types_t::unset;
    Serial.println("WiFi begin...");
    this->_connecting = true;
    WiFi.begin(settings.WIFI.ssid, settings.WIFI.passphrase, channel, bssid);
    this->connectStart = millis();
  }
  else if(settings.WIFI.ssid[0] != '\0') {
    if(WiFi.status() == WL_CONNECTED && WiFi.SSID().compareTo(settings.WIFI.ssid) == 0) {
      // If we are connected to the target SSID then just return.
      this->disconnected = 0;
      this->_connecting = true;
      return true;
    }
    if(this->_connecting) return true;
    this->_connecting = true;
    this->connTarget = conn_types_t::wifi;
    this->connType = conn_types_t::unset;
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
    delay(100);
    // There is also another method simply called hostname() but this is legacy for esp8266.
    if(settings.hostname[0] != '\0') WiFi.setHostname(settings.hostname);
    Serial.print("Set hostname to:");
    Serial.println(WiFi.getHostname());
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    uint8_t _bssid[6];
    int32_t _channel = 0;
    if(!settings.WIFI.hidden && this->getStrongestAP(settings.WIFI.ssid, _bssid, &_channel)) {
      Serial.printf("Found strongest AP %02X:%02X:%02X:%02X:%02X:%02X CH:%d\n", _bssid[0], _bssid[1], _bssid[2], _bssid[3], _bssid[4], _bssid[5], _channel);
      WiFi.begin(settings.WIFI.ssid, settings.WIFI.passphrase, _channel, _bssid);
    }
    else
      // If the user has the hidden flag set just connect to whatever the AP gives us.
      WiFi.begin(settings.WIFI.ssid, settings.WIFI.passphrase);
  }
  this->connectStart = millis();
  return true;
}
bool Network::connect(conn_types_t ctype) {
  esp_task_wdt_reset();
  if(this->connecting()) return true;
  if(this->disconnectTime == 0) this->disconnectTime = millis();
  if(ctype == conn_types_t::ethernet && this->connType != conn_types_t::ethernet) {
    // Here we need to call the connect to ethernet.
    this->connectWired();
  }
  else if(ctype == conn_types_t::ap || (!this->connected() && millis() > this->disconnectTime + CONNECT_TIMEOUT)) {
    if(!this->softAPOpened && !this->openingSoftAP) {
      this->disconnectTime = millis();
      this->openSoftAP();
    }
    else if(this->softAPOpened && !this->openingSoftAP && 
      (ctype == conn_types_t::wifi && this->connType != conn_types_t::wifi && settings.WIFI.hidden)) {
      // When thge softAP is open then we need to try to connect to wifi repeatedly if the user connects to a hidden SSID.
      this->connectWiFi();
    }
  }
  else if((ctype == conn_types_t::wifi && this->connType != conn_types_t::wifi && settings.WIFI.hidden)) {
    this->connectWiFi();
  }
  
  return true;
}
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
  esp_task_wdt_delete(NULL);
  int16_t n = WiFi.scanComplete();
  //int16_t n = this->connected() ? WiFi.scanComplete() : WiFi.scanNetworks(false, false, false, 300, 0, ssid);
  esp_task_wdt_add(NULL);
  for(int16_t i = 0; i < n; i++) {
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
bool Network::openSoftAP() {
  if(this->softAPOpened || this->openingSoftAP) return true;
  if(this->connected()) WiFi.disconnect(false);
  this->openingSoftAP = true;
  Serial.println();
  Serial.println("Turning the HotSpot On");
  esp_task_wdt_reset(); // Make sure we do not reboot here.
  WiFi.softAP(strlen(settings.hostname) > 0 ? settings.hostname : "ESPSomfy RTS", "");
  delay(200);
  return true;
}
bool Network::connected() {
  if(this->connecting()) return false;
  else if(this->connType == conn_types_t::unset) return false;
  else if(this->connType == conn_types_t::wifi) return WiFi.status() == WL_CONNECTED;
  else if(this->connType == conn_types_t::ethernet) return ETH.linkUp();
  else return this->connType != conn_types_t::unset;
  return false;
}
bool Network::connecting() {
  if(this->_connecting && millis() > this->connectStart + CONNECT_TIMEOUT) this->_connecting = false; 
  return this->_connecting; 
}
void Network::clearConnecting() { this->_connecting = false; }
void Network::networkEvent(WiFiEvent_t event) {
  switch(event) {
    case ARDUINO_EVENT_WIFI_READY:               Serial.println("(evt) WiFi interface ready"); break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:           
      Serial.printf("(evt) Completed scan for access points (%d)\n", WiFi.scanComplete()); 
      //Serial.println("(evt) Completed scan for access points");
      net.lastWifiScan = millis();
      break;
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("WiFi station mode started");
      if(settings.hostname[0] != '\0') WiFi.setHostname(settings.hostname);
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:            Serial.println("(evt) WiFi clients stopped"); break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:       Serial.println("(evt) Connected to WiFi STA access point"); break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:    
      Serial.printf("(evt) Disconnected from WiFi STA access point. Connecting: %d\n", net.connecting());
      net.connType = conn_types_t::unset;
      net.disconnectTime = millis();
      net.clearConnecting();
      break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE: Serial.println("(evt) Authentication mode of STA access point has changed"); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("(evt) Got WiFi STA IP: ");
      Serial.println(WiFi.localIP());
      net.connType = conn_types_t::wifi;
      net.connectTime = millis();
      net.setConnected(conn_types_t::wifi);
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:        Serial.println("Lost IP address and IP address is reset to 0"); break;    
    case ARDUINO_EVENT_ETH_GOT_IP:
      // If the Wifi is connected then drop that connection
      if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
      Serial.print("Got Ethernet IP ");
      Serial.println(ETH.localIP());
      net.connectTime = millis();
      net.connType = conn_types_t::ethernet;
      if(settings.IP.dhcp) {
        settings.IP.ip = ETH.localIP();
        settings.IP.subnet = ETH.subnetMask();
        settings.IP.gateway = ETH.gatewayIP();
        settings.IP.dns1 = ETH.dnsIP(0);
        settings.IP.dns2 = ETH.dnsIP(1);
      }     
      net.setConnected(conn_types_t::ethernet);
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.print("(evt) Ethernet Connected ");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("(evt) Ethernet Disconnected");
      net.connType = conn_types_t::unset;
      net.disconnectTime = millis();
      net.clearConnecting();
      break;
    case ARDUINO_EVENT_ETH_START:               
      Serial.println("(evt) Ethernet Started"); 
      net.ethStarted = true;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("(evt) Ethernet Stopped");
      net.connType = conn_types_t::unset;
      net.ethStarted = false;
      break;
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.print("(evt) WiFi SoftAP Started IP:");
      Serial.println(WiFi.softAPIP());
      net.openingSoftAP = false;
      net.softAPOpened = true;
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      if(!net.openingSoftAP) Serial.println("(evt) WiFi SoftAP Stopped");
      net.softAPOpened = false;
      break;      
    default:
      if(event > ARDUINO_EVENT_ETH_START)
        Serial.printf("(evt) Unknown Ethernet Event %d\n", event);
      break;
  }
}
void Network::emitHeap(uint8_t num) {
  bool bEmit = false;
  bool bTimeEmit = millis() - _lastHeapEmit > 15000;
  bool bRoomEmit = false;
  bool bValEmit = false;
  if(num != 255 || this->needsBroadcast) bEmit = true;
  if(millis() - _lastHeapEmit > 15000) bTimeEmit = true;
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t maxHeap = ESP.getMaxAllocHeap();
  uint32_t minHeap = ESP.getMinFreeHeap();
  if(abs((int)(freeHeap - _lastHeap)) > 1500) bValEmit = true;
  if(abs((int)(maxHeap - _lastMaxHeap)) > 1500) bValEmit = true;
  bRoomEmit = sockEmit.activeClients(0) > 0;
  if(bValEmit) bTimeEmit = millis() - _lastHeapEmit > 7000;
  if(bEmit || bTimeEmit || bRoomEmit || bValEmit) {
    JsonSockEvent *json = sockEmit.beginEmit("memStatus");
    json->beginObject();
    json->addElem("max", maxHeap);
    json->addElem("free", freeHeap);
    json->addElem("min", minHeap);
    json->addElem("total", ESP.getHeapSize());
    json->endObject();
    if(num == 255 && bTimeEmit && bValEmit) {
      sockEmit.endEmit(num);
      _lastHeapEmit = millis();
      _lastHeap = freeHeap;
      _lastMaxHeap = maxHeap;
      //Serial.printf("BROAD HEAP: Emit:%d TimeEmit:%d ValEmit:%d\n", bEmit, bTimeEmit, bValEmit);
    }
    else if(num != 255) {
      sockEmit.endEmit(num);
      //Serial.printf("TARGET HEAP %d: Emit:%d TimeEmit:%d ValEmit:%d\n", num, bEmit, bTimeEmit, bValEmit);
    }
    else if(bRoomEmit) {
      sockEmit.endEmitRoom(0);
      //Serial.printf("ROOM HEAP: Emit:%d TimeEmit:%d ValEmit:%d\n", bEmit, bTimeEmit, bValEmit);
    }
  }
}
