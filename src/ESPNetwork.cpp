#include <WiFi.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include "TaskWdt.h"
#include "esp_log.h"
#include "ConfigSettings.h"
#include "ESPNetwork.h"
#include "Somfy.h"
#include "outputs/ProtoSocket.h"
#include "Utils.h"
#include "SSDP.h"
#include "MqttConnection.h"

static const char *TAG = "Network";

extern ConfigSettings settings;
extern MqttConnection mqtt;
extern rebootDelay_t rebootDelay;
extern ESPNetwork net;
extern SomfyShadeController somfy;
extern LoopStats loopStats;

static bool _apScanning = false;
// The scan for clients: asked for, then running.  WiFi holds the results of
// one scan at a time, so it and the roaming scan never overlap.
static bool _clientScanWanted = false;
static bool _clientScanning = false;
int connectRetries = 0;
// A client that asks during a scan gets that scan's networks.
void ESPNetwork::scanForClients() { if(!_clientScanning) _clientScanWanted = true; }
static void clientScanLoop(ESPNetwork &net) {
  if(_clientScanning) {
    const int16_t n = WiFi.scanComplete();
    if(n == WIFI_SCAN_RUNNING) return;
    _clientScanning = false;
    if(n < 0) ESP_LOGW(TAG, "WiFi scan failed");
    else ESP_LOGI(TAG, "Scanned %d networks", n);
    protoSocket.wifiNetworks(n);
    WiFi.scanDelete();
    return;
  }
  // Not in the middle of joining a network either, as the roaming scan isn't.
  if(!_clientScanWanted || _apScanning || net.connecting()) return;
  _clientScanWanted = false;
  // As /scanaps does: the soft AP's connection attempts would get in its way.
  if(net.softAPOpened) WiFi.disconnect(false);
  if(WiFi.scanNetworks(true, true) == WIFI_SCAN_RUNNING) _clientScanning = true;
  else {
    ESP_LOGW(TAG, "WiFi scan didn't start");
    protoSocket.wifiNetworks(WIFI_SCAN_FAILED);
  }
}
void ESPNetwork::end() {
  SSDP.end();
  mqtt.end();
  protoSocket.end();
  delay(100);
}
bool ESPNetwork::setup() {
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
    ESP_LOGI(TAG, "WiFi Mode: %d", WiFi.getMode());
    WiFi.mode(WIFI_STA);
  }
  return true;
}
conn_types_t ESPNetwork::preferredConnType() {
  switch(settings.connType) {
    case conn_types_t::wifi:    
      return settings.WIFI.ssid[0] != '\0' ? conn_types_t::wifi : conn_types_t::ap;
    case conn_types_t::unset:
    case conn_types_t::ap:
      return conn_types_t::ap;
#if HAS_ETHERNET
    case conn_types_t::ethernetpref:
      return settings.WIFI.ssid[0] != '\0' && (!ETH.linkUp() && this->ethStarted) ? conn_types_t::wifi : conn_types_t::ethernet;
    case conn_types_t::ethernet:
      return ETH.linkUp() || !this->ethStarted ? conn_types_t::ethernet : conn_types_t::ap;
#else
    // Stored by a backup or an older UI on a chip without Ethernet.
    case conn_types_t::ethernetpref:
    case conn_types_t::ethernet:
      return settings.WIFI.ssid[0] != '\0' ? conn_types_t::wifi : conn_types_t::ap;
#endif
    default:
      return settings.connType; 
  }
}
void ESPNetwork::loop() {
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
  // Before the connection steps, which return early while connecting.
  clientScanLoop(*this);
  conn_types_t ctype = this->preferredConnType();
  this->connect(ctype); // Connection timeout handled in connect function as well as the opening of the Soft AP if needed.
  if(this->connecting()) return; // If we are currently attempting to connect to something then we need to bail here.
  if(_apScanning) {
    if(settings.WIFI.hidden ||                                    // This user has elected to use a hidden AP.
      (this->connected() && !settings.WIFI.roaming) ||            // We are already connected and should not be roaming.
      (this->softAPOpened && WiFi.softAPgetStationNum() != 0) ||  // The Soft AP is open and a user is connected.
      (ctype != conn_types_t::wifi)) {                            // The Ethernet link is up so we should ignore this scan.
      ESP_LOGI(TAG, "Cancelling WiFi STA Scan...");
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
              ESP_LOGI(TAG, "Connecting to AP %02X:%02X:%02X:%02X:%02X:%02X CH: %d", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], channel);
              this->connectWiFi(bssid, channel);
            }
            else {
              ESP_LOGI(TAG, "Found stronger AP %02X:%02X:%02X:%02X:%02X:%02X CH: %d", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], channel);
              this->changeAP(bssid, channel);
            }
          }
        }
        _apScanning = false;
      }
    }
  }
  if(!this->connecting() && !settings.WIFI.hidden && !_clientScanWanted && !_clientScanning) {
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
  if(millis() - this->lastEmit >= STATUS_EMIT_MS) {
    this->lastEmit = millis();
    if(this->connected()) this->emitStatus();
    resetTaskWdt(); // Make sure we do not reboot here.
  }
  
  mqtt.loop();
  if(settings.ssdpBroadcast && this->connected()) {
    if(!SSDP.isStarted) SSDP.begin();
    if(SSDP.isStarted) SSDP.loop();
  }
  else if(!settings.ssdpBroadcast && SSDP.isStarted) SSDP.end();
}
bool ESPNetwork::changeAP(const uint8_t *bssid, const int32_t channel) {
  resetTaskWdt(); // Make sure we do not reboot here.
  if(SSDP.isStarted) SSDP.end();
  mqtt.disconnect();
  WiFi.disconnect(false, true);
  this->connType = conn_types_t::unset;
  this->_connecting = true;
  this->connectStart = millis();
  WiFi.setScanMethod(WIFI_FAST_SCAN);
  WiFi.begin(settings.WIFI.ssid, settings.WIFI.passphrase, channel, bssid);
  this->connectStart = millis();
  return false;
}
// The status broadcast, every STATUS_EMIT_MS from loop().  A wired link goes
// out every time, the WiFi signal only when it really changed.
void ESPNetwork::emitStatus() {
  this->emitHeap();
#if HAS_ETHERNET
  if(this->connType == conn_types_t::ethernet) {
    protoSocket.ethernet();
    return;
  }
#endif
  if(this->connType == conn_types_t::wifi && !this->rssiBroadcast.due(WiFi.RSSI(), WiFi.channel(), RSSI_EMIT_MIN_DBM)) return;
  if(WiFi.status() == WL_CONNECTED) this->rssiBroadcast.sent(WiFi.RSSI(), WiFi.channel());
  else this->rssiBroadcast.sent(-100, -1);
  protoSocket.wifi();
}
// Runs on the WiFi/Ethernet event task, so it sends nothing on the socket.
// loop() broadcasts the new status and a client that reconnects gets it in its
// sync.
void ESPNetwork::setConnected(conn_types_t connType) {
  resetTaskWdt();
  this->connType = connType;
  this->connectTime = millis();
  connectRetries = 0;
  ESP_LOGI(TAG, "Setting connected...");
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
#if HAS_ETHERNET
  else if(this->connType == conn_types_t::ethernet) {
    if(this->softAPOpened) {
      ESP_LOGI(TAG, "Disconnecting from SoftAP");
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_OFF);
    }
    this->connectAttempts++;
    this->_connecting = false;
    this->wifiFallback = false;
  }
#endif // HAS_ETHERNET
  resetTaskWdt();

  if(this->connectAttempts == 1) {
    if(this->connType == conn_types_t::wifi) {
      ESP_LOGI(TAG, "Successfully Connected to WiFi!!!! %s (%ddbm)", WiFi.localIP().toString().c_str(), this->strength);
      if(settings.IP.dhcp) {
        settings.IP.ip = WiFi.localIP();
        settings.IP.subnet = WiFi.subnetMask();
        settings.IP.gateway = WiFi.gatewayIP();
        settings.IP.dns1 = WiFi.dnsIP(0);
        settings.IP.dns2 = WiFi.dnsIP(1);
      }
    }
#if HAS_ETHERNET
    else {
      ESP_LOGI(TAG, "Successfully Connected to Ethernet!!! %s%s %dMbps", ETH.localIP().toString().c_str(), ETH.fullDuplex() ? " FULL DUPLEX" : "", ETH.linkSpeed());
      if(settings.IP.dhcp) {
        settings.IP.ip = ETH.localIP();
        settings.IP.subnet = ETH.subnetMask();
        settings.IP.gateway = ETH.gatewayIP();
        settings.IP.dns1 = ETH.dnsIP(0);
        settings.IP.dns2 = ETH.dnsIP(1);
      }
    }
#endif
  }
  else {
    if(this->connType == conn_types_t::wifi) {
      ESP_LOGI(TAG, "Reconnected after %.3fsec IP: %s %s CH:%d (%d dBm) Disconnected %d times", 1.0 * (millis() - this->connectStart)/1000, WiFi.localIP().toString().c_str(), this->mac.c_str(), this->channel, this->strength, this->connectAttempts - 1);
    }
#if HAS_ETHERNET
    else {
      ESP_LOGI(TAG, "Reconnected after %.3fsec IP: %s%s %dMbps Disconnected %d times", 1.0 * (millis() - this->connectStart)/1000, ETH.localIP().toString().c_str(), ETH.fullDuplex() ? " FULL DUPLEX" : "", ETH.linkSpeed(), this->connectAttempts - 1);
    }
#endif
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
  SSDP.setModelURL(0, "https://github.com/cjkas/ESPSomfy-RTS");
  SSDP.setManufacturer(0, "cjkas");
  SSDP.setManufacturerURL(0, "https://github.com/cjkas");
  SSDP.setURL(0, "/");
  SSDP.setActive(0, true);
  resetTaskWdt();
  if(MDNS.begin(settings.hostname)) {
    ESP_LOGI(TAG, "MDNS Responder Started: serverId=%s", settings.serverId);
    MDNS.addService("http", "tcp", 80);
    //MDNS.addServiceTxt("http", "tcp", "board", "ESP32");
    //MDNS.addServiceTxt("http", "tcp", "model", "ESPSomfyRTS");

    // Home Assistant's discovery matches this record.  The socket is on the web
    // server's port, at path.
    MDNS.addService("espsomfy_rts", "tcp", 80);
    MDNS.addServiceTxt("espsomfy_rts", "tcp", "path", "/ws");
    MDNS.addServiceTxt("espsomfy_rts", "tcp", "serverId", String(settings.serverId));
    MDNS.addServiceTxt("espsomfy_rts", "tcp", "model", "ESPSomfyRTS");
    MDNS.addServiceTxt("espsomfy_rts", "tcp", "version", String(settings.fwVersion.name));
  }
  if(settings.ssdpBroadcast) {
    resetTaskWdt();
    SSDP.begin();
  }
  else if(SSDP.isStarted) SSDP.end();
  resetTaskWdt();
  settings.printAvailHeap();
}
#if HAS_ETHERNET
bool ESPNetwork::connectWired() {
  if(ETH.linkUp()) {
    // If the ethernet link is re-established then we need to shut down wifi.
    if(WiFi.status() == WL_CONNECTED) {
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
    ESP_LOGW(TAG, "Ethernet Connection Lost... %d Reconnecting %s", this->connectAttempts, this->mac.c_str());
  }
  else
    ESP_LOGI(TAG, "Connecting to Wired Ethernet");
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
    ESP_LOGD(TAG, "Set hostname to: %s", ETH.getHostname());
    eth_phy_type_t phyType;
    eth_clock_mode_t clkMode;
    bool begun = false;
    if(!ethPhyType(settings.Ethernet.phyType, phyType))
      ESP_LOGE(TAG, "Ethernet PHY type %u is not supported", (unsigned int)settings.Ethernet.phyType);
    else if(!ethClockMode(settings.Ethernet.CLKMode, clkMode))
      ESP_LOGE(TAG, "Ethernet clock mode %u is not supported", (unsigned int)settings.Ethernet.CLKMode);
    else
      begun = ETH.begin(phyType, settings.Ethernet.phyAddress, settings.Ethernet.MDCPin, settings.Ethernet.MDIOPin, settings.Ethernet.PWRPin, clkMode);
    if(!begun) {
      ESP_LOGE(TAG, "Ethernet Begin failed");
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
          ESP_LOGE(TAG, "Unable to configure static IP address....");
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
#endif // HAS_ETHERNET
void ESPNetwork::updateHostname() {
  if(settings.hostname[0] != '\0' && this->connected()) {
#if HAS_ETHERNET
    if(this->connType == conn_types_t::ethernet &&
      strcmp(settings.hostname, ETH.getHostname()) != 0) {
      ESP_LOGD(TAG, "Updating host name to %s...", settings.hostname);
      ETH.setHostname(settings.hostname);
      MDNS.setInstanceName(settings.hostname);
      SSDP.setName(0, settings.hostname);
     }
     else
#endif
     if(strcmp(settings.hostname, WiFi.getHostname()) != 0) {
      ESP_LOGD(TAG, "Updating host name to %s...", settings.hostname);
      WiFi.setHostname(settings.hostname);
      MDNS.setInstanceName(settings.hostname);        
      SSDP.setName(0, settings.hostname);
     }
  }
}
bool ESPNetwork::connectWiFi(const uint8_t *bssid, const int32_t channel) {
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
    ESP_LOGI(TAG, "WiFi begin...");
    this->_connecting = true;
    // Our own scan already picked the AP.  An all-channel scan here makes the
    // driver sweep every channel again first, about 17s, before it even
    // authenticates.  A fast scan starts on the AP's channel and joins the
    // first match.
    WiFi.setScanMethod(WIFI_FAST_SCAN);
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
      ESP_LOGW(TAG, "Connection Lost... %s CH:%d (%ddbm)", this->mac.c_str(), this->channel, this->strength);
    }
    else ESP_LOGI(TAG, "Connecting to AP");
    delay(100);
    // There is also another method simply called hostname() but this is legacy for esp8266.
    if(settings.hostname[0] != '\0') WiFi.setHostname(settings.hostname);
    ESP_LOGD(TAG, "Set hostname to: %s", WiFi.getHostname());
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    uint8_t _bssid[6];
    int32_t _channel = 0;
    if(!settings.WIFI.hidden && this->getStrongestAP(settings.WIFI.ssid, _bssid, &_channel)) {
      ESP_LOGD(TAG, "Found strongest AP %02X:%02X:%02X:%02X:%02X:%02X CH:%d", _bssid[0], _bssid[1], _bssid[2], _bssid[3], _bssid[4], _bssid[5], _channel);
      WiFi.setScanMethod(WIFI_FAST_SCAN);  // The AP is known, see connectWiFi(bssid).
      WiFi.begin(settings.WIFI.ssid, settings.WIFI.passphrase, _channel, _bssid);
    }
    else {
      // If the user has the hidden flag set just connect to whatever the AP gives us.
      WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
      WiFi.begin(settings.WIFI.ssid, settings.WIFI.passphrase);
    }
  }
  this->connectStart = millis();
  return true;
}
bool ESPNetwork::connect(conn_types_t ctype) {
  resetTaskWdt();
  if(this->connecting()) return true;
  if(this->disconnectTime == 0) this->disconnectTime = millis();
#if HAS_ETHERNET
  if(ctype == conn_types_t::ethernet && this->connType != conn_types_t::ethernet) {
    // Here we need to call the connect to ethernet.
    this->connectWired();
  }
  else
#endif
  if(ctype == conn_types_t::ap || (!this->connected() && millis() > this->disconnectTime + CONNECT_TIMEOUT)) {
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
uint32_t ESPNetwork::getChipId() {
  uint32_t chipId = 0;
  uint64_t mac = ESP.getEfuseMac();
  for(int i=0; i<17; i=i+8) {
    chipId |= ((mac >> (40 - i)) & 0xff) << i;
  }
  return chipId;
}
bool ESPNetwork::getStrongestAP(const char *ssid, uint8_t *bssid, int32_t *channel) {
  const bool connected = this->connected();
  const int32_t currentRssi = connected ? WiFi.RSSI() : 0;
  int32_t strength = -127;
  int32_t chan = -1;
  memset(bssid, 0x00, 6);
  const bool wdtPaused = pauseTaskWdt();
  int16_t n = WiFi.scanComplete();
  //int16_t n = this->connected() ? WiFi.scanComplete() : WiFi.scanNetworks(false, false, false, 300, 0, ssid);
  resumeTaskWdt(wdtPaused);
  for(int16_t i = 0; i < n; i++) {
    if(WiFi.SSID(i).compareTo(ssid) == 0) {
      // Only on the roaming scans, so the log shows whether a better AP is
      // around without filling up while we scan to reconnect.
      if(connected) {
        const uint8_t *b = WiFi.BSSID(i);
        const uint8_t *cur = WiFi.BSSID();
        ESP_LOGI(TAG, "AP %02X:%02X:%02X:%02X:%02X:%02X ch %ld %ld dBm %s%s", b[0], b[1], b[2], b[3], b[4], b[5],
          (long)WiFi.channel(i), (long)WiFi.RSSI(i), wifiAuthModeName(WiFi.encryptionType(i)),
          (cur && memcmp(b, cur, 6) == 0) ? " (connected)" : "");
      }
      if(worthRoaming(connected, currentRssi, WiFi.RSSI(i), ROAM_MIN_GAIN_DB) && WiFi.RSSI(i) > strength) {
        strength = WiFi.RSSI(i); 
        memcpy(bssid, WiFi.BSSID(i), 6); 
        *channel = chan = WiFi.channel(i);
      }
    }
  }  
  WiFi.scanDelete();
  return chan > 0;
}
bool ESPNetwork::openSoftAP() {
  if(this->softAPOpened || this->openingSoftAP) return true;
  if(this->connected()) WiFi.disconnect(false);
  this->openingSoftAP = true;
  ESP_LOGI(TAG, "Turning the HotSpot On");
  resetTaskWdt(); // Make sure we do not reboot here.
  WiFi.softAP(strlen(settings.hostname) > 0 ? settings.hostname : "ESPSomfy RTS", "");
  delay(200);
  return true;
}
bool ESPNetwork::connected() {
  if(this->connecting()) return false;
  else if(this->connType == conn_types_t::unset) return false;
  else if(this->connType == conn_types_t::wifi) return WiFi.status() == WL_CONNECTED;
#if HAS_ETHERNET
  else if(this->connType == conn_types_t::ethernet) return ETH.linkUp();
#endif
  else return this->connType != conn_types_t::unset;
  return false;
}
bool ESPNetwork::connecting() {
  if(this->_connecting && millis() > this->connectStart + CONNECT_TIMEOUT) this->_connecting = false; 
  return this->_connecting; 
}
void ESPNetwork::clearConnecting() { this->_connecting = false; }
void ESPNetwork::networkEvent(WiFiEvent_t event) {
  switch(event) {
    case ARDUINO_EVENT_WIFI_READY:               ESP_LOGI(TAG, "(evt) WiFi interface ready"); break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:           
      ESP_LOGI(TAG, "(evt) Completed scan for access points (%d)", WiFi.scanComplete());
      //Serial.println("(evt) Completed scan for access points");
      net.lastWifiScan = millis();
      break;
    case ARDUINO_EVENT_WIFI_STA_START:
      ESP_LOGI(TAG, "WiFi station mode started");
      if(settings.hostname[0] != '\0') WiFi.setHostname(settings.hostname);
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:            ESP_LOGI(TAG, "(evt) WiFi clients stopped"); break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: {
      // How long the join took and the security the AP offers, so a slow
      // connect shows in the log.
      wifi_ap_record_t ap;
      const bool haveAp = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
      ESP_LOGI(TAG, "(evt) Connected to WiFi STA access point after %lums (%s)",
        (unsigned long)(millis() - net.connectStart), haveAp ? wifiAuthModeName(ap.authmode) : "?");
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:    
      ESP_LOGI(TAG, "(evt) Disconnected from WiFi STA access point. Connecting: %d", net.connecting());
      net.connType = conn_types_t::unset;
      net.disconnectTime = millis();
      net.clearConnecting();
      break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE: ESP_LOGI(TAG, "(evt) Authentication mode of STA access point has changed"); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      ESP_LOGI(TAG, "(evt) Got WiFi STA IP: %s", WiFi.localIP().toString().c_str());
      net.connType = conn_types_t::wifi;
      net.connectTime = millis();
      net.setConnected(conn_types_t::wifi);
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:        ESP_LOGW(TAG, "Lost IP address and IP address is reset to 0"); break;
#if HAS_ETHERNET
    case ARDUINO_EVENT_ETH_GOT_IP:
      // If the Wifi is connected then drop that connection
      if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
      ESP_LOGI(TAG, "Got Ethernet IP %s", ETH.localIP().toString().c_str());
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
      ESP_LOGI(TAG, "(evt) Ethernet Connected");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      ESP_LOGI(TAG, "(evt) Ethernet Disconnected");
      net.connType = conn_types_t::unset;
      net.disconnectTime = millis();
      net.clearConnecting();
      break;
    case ARDUINO_EVENT_ETH_START:
      ESP_LOGI(TAG, "(evt) Ethernet Started");
      net.ethStarted = true;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      ESP_LOGI(TAG, "(evt) Ethernet Stopped");
      net.connType = conn_types_t::unset;
      net.ethStarted = false;
      break;
#endif
    case ARDUINO_EVENT_WIFI_AP_START:
      ESP_LOGI(TAG, "(evt) WiFi SoftAP Started IP: %s", WiFi.softAPIP().toString().c_str());
      net.openingSoftAP = false;
      net.softAPOpened = true;
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      if(!net.openingSoftAP) ESP_LOGI(TAG, "(evt) WiFi SoftAP Stopped");
      net.softAPOpened = false;
      break;      
    default:
#if HAS_ETHERNET
      if(event > ARDUINO_EVENT_ETH_START)
        ESP_LOGW(TAG, "(evt) Unknown Ethernet Event %d", event);
#endif
      break;
  }
}
// memory, the device's heap and loop rate.  Each one closes the loop rate's
// window and reports it.
void ESPNetwork::emitHeap() {
  loopStats.publish(millis());
  protoSocket.memory();
}
