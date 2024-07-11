#include <functional>
#include <AsyncUDP.h>
#include "Utils.h"
#include "ConfigSettings.h"
#include "SSDP.h"


#define SSDP_PORT         1900
#define SSDP_METHOD_SIZE  10
#define SSDP_URI_SIZE     2
#define SSDP_BUFFER_SIZE  64
#define SSDP_MULTICAST_ADDR 239, 255, 255, 250
//#define DEBUG_SSDP Serial
//#define DEBUG_SSDP_PACKET Serial
extern ConfigSettings settings;

static const char _ssdp_uuid_template[] PROGMEM = "C2496952-5610-47E6-A968-2FC1%02X%02X%02X%02X";
static const char _ssdp_serial_number_template[] PROGMEM = "ESP32-%02x%02x%02x";
static const char _ssdp_usn_root_template[] PROGMEM = "uuid:%s::upnp:rootdevice";
static const char _ssdp_usn_uuid_template[] PROGMEM = "uuid:%s";
static const char _ssdp_usn_urn_template[] PROGMEM = "uuid:%s::%s";
static const char _ssdp_response_template[] PROGMEM =
  "HTTP/1.1 200 OK\r\n"
  "EXT:\r\n";
static const char _ssdp_notify_template[] PROGMEM =
  "NOTIFY * HTTP/1.1\r\n"
  "HOST: 239.255.255.250:1900\r\n"
  "NTS: ssdp:alive\r\n";
static const char _ssdp_bye_template[] PROGMEM =
  "NOTIFY * HTTP/1.1\r\n"
  "HOST: 239.255.255.250:1900\r\n"
  "NTS: ssdp:byebye\r\n"
  "NT: %s\r\n"
  "USN: %s\r\n"
  "BOOTID.UPNP.ORG: %lu\r\n"
  "CONFIGID.UPNP.ORG: %d\r\n"
  "\r\n";
static const char _ssdp_packet_template[] PROGMEM =
  "%s" // _ssdp_response_template / _ssdp_notify_template
  "CACHE-CONTROL: max-age=%u\r\n" // _interval
  "SERVER: Arduino/1.0 UPnP/1.1 ESPSomfyRTS/2.0\r\n"
  "USN: %s\r\n" // _uuid
  "%s: %s\r\n"  // "NT" or "ST", _deviceType
  "LOCATION: http://%u.%u.%u.%u:%u/%s\r\n" // WiFi.localIP(), _port, _schemaURL
  "BOOTID.UPNP.ORG: %lu\r\n"
  "CONFIGID.UPNP.ORG: %d\r\n"
  "\r\n";
static const char _ssdp_device_schema_template[] PROGMEM =
  "<device>"
  "<deviceType>%s</deviceType>"
  "<friendlyName>%s</friendlyName>"
  "<presentationURL>%s</presentationURL>"
  "<serialNumber>%s</serialNumber>"
  "<modelName>%s</modelName>"
  "<modelNumber>%s</modelNumber>"
  "<modelURL>%s</modelURL>"
  "<manufacturer>%s</manufacturer>"
  "<manufacturerURL>%s</manufacturerURL>"
  "<modelDescription>ESPSomfy RTS Controller</modelDescription>"
  "<firmwareVersion>%s</firmwareVersion>"
  "<UDN>%s</UDN>"
  "<serviceList></serviceList>"
  "<deviceList></deviceList>"
  "</device>";
static const char _ssdp_schema_template[] PROGMEM =
  "HTTP/1.1 200 OK\r\n"
  "Content-Type: text/xml\r\n"
  "Connection: close\r\n"
  "Access-Control-Allow-Origin: *\r\n"
  "\r\n"
  "<?xml version=\"1.0\"?>"
  "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
  "<specVersion>"
  "<major>1</major>"
  "<minor>0</minor>"
  "</specVersion>"
  "<URLBase>http://%s:%u/</URLBase>";    // WiFi.localIP(), _port


UPNPDeviceType::UPNPDeviceType() {
    lastNotified = 0;
    schemaURL[0] = '\0';
    uuid[0] = '\0';
    deviceType[0] = '\0';
    friendlyName[0] = '\0';
    serialNumber[0] = '\0';
    presentationURL[0] = '\0';
    manufacturer[0] = '\0';
    manufacturerURL[0] = '\0';
    modelName[0] = '\0';
    modelURL[0] = '\0';
    modelNumber[0] = '\0';  
    m_usn[0] = '\0';
    isActive = true;
}
UPNPDeviceType::UPNPDeviceType(const char *deviceType) { this->setDeviceType(deviceType); }
UPNPDeviceType::UPNPDeviceType(const char *deviceType, const char *uuid){ this->setDeviceType(deviceType); this->setUUID(uuid); }
UPNPDeviceType::UPNPDeviceType(const char *deviceType, const char *uuid, const char*friendlyName) { this->setDeviceType(deviceType); this->setUUID(uuid); this->setName(friendlyName);}
void UPNPDeviceType::setSchemaURL(const char *url) { strlcpy(schemaURL, url, sizeof(schemaURL)); }
void UPNPDeviceType::setDeviceType(const char *dtype) { strlcpy(this->deviceType, dtype, sizeof(deviceType)); }
void UPNPDeviceType::setUUID(const char *id) { snprintf_P(uuid, sizeof(uuid), PSTR("uuid:%s"), id); }
void UPNPDeviceType::setName(const char *name) { strlcpy(friendlyName, name, sizeof(friendlyName)); }
void UPNPDeviceType::setURL(const char *url) { strlcpy(presentationURL, url, sizeof(presentationURL)); }
void UPNPDeviceType::setSerialNumber(const char *sn) { strlcpy(serialNumber, sn, sizeof(serialNumber)); }
void UPNPDeviceType::setSerialNumber(const uint32_t sn) { snprintf(serialNumber, sizeof(uint32_t) * 2 + 1, "%08X", sn); }
void UPNPDeviceType::setModelName(const char *name) { strlcpy(modelName, name, sizeof(modelName)); }
void UPNPDeviceType::setModelNumber(const char *num) { strlcpy(modelNumber, num, sizeof(modelNumber));}
void UPNPDeviceType::setModelURL(const char *url) { strlcpy(modelURL, url, sizeof(modelURL)); }
void UPNPDeviceType::setManufacturer(const char *name) { strlcpy(manufacturer, name, sizeof(manufacturer)); }
void UPNPDeviceType::setManufacturerURL(const char *url) { strlcpy(manufacturerURL, url, sizeof(manufacturerURL)); }
//char * UPNPDeviceType::getUSN() { return this->getUSN(this->deviceType); }
char * UPNPDeviceType::getUSN(response_types_t responseType) {
  switch(responseType) {
    case response_types_t::root:
      snprintf_P(this->m_usn, sizeof(this->m_usn) - 1, _ssdp_usn_root_template, this->uuid); 
      break;
    case response_types_t::deviceType:
      snprintf_P(this->m_usn, sizeof(this->m_usn) -1, _ssdp_usn_urn_template, this->uuid, this->deviceType);
      break;
    default:
      snprintf_P(this->m_usn, sizeof(this->m_usn) - 1, _ssdp_usn_uuid_template, this->uuid); 
      break;
  }
  return this->m_usn;
}
char * UPNPDeviceType::getUSN(const char *st) { 
  //#ifdef DEBUG_SSDP
  //DEBUG_SSDP.print("GETUSN ST: ");
  //DEBUG_SSDP.println(st);
  //DEBUG_SSDP.print("GETUSN UUID: ");
  //DEBUG_SSDP.println(this->uuid);
  //DEBUG_SSDP.print("sizeof(this->m_usn)");
  //DEBUG_SSDP.println(sizeof(this->m_usn));
  //#endif
  if(strncmp("upnp:rootdevice", st, strlen(st)) == 0) {
    snprintf_P(this->m_usn, sizeof(this->m_usn) - 1, _ssdp_usn_root_template, this->uuid); 
  }
  else if(strncmp("uuid:", st, 5) == 0)
    snprintf_P(this->m_usn, sizeof(this->m_usn) - 1, _ssdp_usn_uuid_template, this->uuid);
  else if(strncmp("urn:", st, 4) == 0)
    snprintf_P(this->m_usn, sizeof(this->m_usn) -1, _ssdp_usn_urn_template, this->uuid, this->deviceType);
  else {
    snprintf_P(this->m_usn, sizeof(this->m_usn) - 1, _ssdp_usn_uuid_template, this->uuid); 
  }
  //#ifdef DEBUG_SSDP
  //DEBUG_SSDP.print("RESUSN UUID: ");
  //DEBUG_SSDP.println(this->m_usn);
  //#endif
  return this->m_usn; 
}
void UPNPDeviceType::setChipId(uint32_t chipId) {
  snprintf_P(this->uuid, sizeof(this->uuid), _ssdp_uuid_template,
    0x40,
    (uint16_t)((chipId >> 16) & 0xff),
    (uint16_t)((chipId >> 8) & 0xff),
    (uint16_t)chipId & 0xff);
  snprintf_P(this->serialNumber, sizeof(this->serialNumber),
    _ssdp_serial_number_template,
    (uint16_t)((chipId >> 16) & 0xff),
    (uint16_t)((chipId >> 8) & 0xff),
    (uint16_t)chipId & 0xff);    
}
SSDPClass::SSDPClass():sendQueue{false, INADDR_NONE, 0, nullptr, false, 0, "", response_types_t::root} {}
SSDPClass::~SSDPClass() { end(); this->isStarted = false; }
bool SSDPClass::begin() { 
  for(int i = 0; i < SSDP_QUEUE_SIZE; i++) {
    this->sendQueue[i].waiting = false;
  }
  if(this->_server.connected()) this->end();
  //assert(NULL == _server);
  if(_server.connected()) {
    #ifdef DEBUG_SSDP
    DEBUG_SSDP.println(PSTR("Already connected to SSDP."));
    this->isStarted = true;
    #endif
    return false;
  }
  this->bootId = Timestamp::epoch();
  if(this->bootId < 1000) {
    this->isStarted = false;
    return false;
  }
  this->configId = (settings.fwVersion.major * 100) + (settings.fwVersion.minor * 10) + settings.fwVersion.build;
  _server.onPacket([](void * arg, AsyncUDPPacket& packet) { ((SSDPClass*)(arg))->_processRequest(packet); }, this);
  if(!_server.listenMulticast(IPAddress(SSDP_MULTICAST_ADDR), SSDP_PORT)) {
    #ifdef DEBUG_SSDP
    IPAddress mcast(SSDP_MULTICAST_ADDR);
    DEBUG_SSDP.println(PSTR("Error starting SSDP listener on: "));
    DEBUG_SSDP.print(mcast);
    DEBUG_SSDP.print(":");
    DEBUG_SSDP.println(SSDP_PORT);
    #endif
    return false;
  }
  for(uint8_t i = 0; i < this->m_cdeviceTypes; i++) {
    Serial.printf("SSDP: %s - %s\n", this->deviceTypes[i].deviceType, this->deviceTypes[i].isActive ? "true" : "false");
  }
  this->isStarted = true;
  this->_sendByeBye();
  this->_sendNotify();
  Serial.println("Connected to SSDP..."); 
  return true;
}
void SSDPClass::end() { 
  if(!this->_server || !this->_server.connected()) return; // server isn't connected nothing to do
  #ifdef DEBUG_SSDP
  DEBUG_SSDP.printf(PSTR("SSDP end ...\n "));
  #endif
  if(this->_server.connected()) {
    this->_sendByeBye();
    this->_server.close();
    Serial.println("Disconnected from SSDP...");
  }
  this->isStarted = false;
  // Clear out the last notified so if the user starts us up again it will notify
  // that we exist again.
  for(uint8_t i = 0; i < this->m_cdeviceTypes; i++) {
    this->deviceTypes[i].lastNotified = 0;
  }
}
UPNPDeviceType* SSDPClass::getDeviceType(uint8_t ndx) { if(ndx < this->m_cdeviceTypes) return &this->deviceTypes[ndx];  return nullptr; }
UPNPDeviceType* SSDPClass::findDeviceByType(char *devType) { 
  for(uint8_t i = 0; i < this->m_cdeviceTypes; i++) {
    if (strcasecmp(devType, this->deviceTypes[i].deviceType) == 0) return &this->deviceTypes[i];
  }
  return nullptr;
}
UPNPDeviceType* SSDPClass::findDeviceByUUID(char *uuid) {
  for(uint8_t i = 0; i < this->m_cdeviceTypes; i++) {
    if (strcasecmp(uuid, this->deviceTypes[i].uuid) == 0) return &this->deviceTypes[i];
  }
  return nullptr;
}

bool SSDPClass::_startsWith(const char *sw, const char *str) { return strncmp(sw, str, strlen(sw)) == 0; }
void SSDPClass::_parsePacket(ssdp_packet_t *pkt, AsyncUDPPacket &p) {
  //MSEARCH = "M-SEARCH * HTTP/1.1\nHost: 239.255.255.250:1900\nMan: \"ssdp:discover\"\nST: roku:ecp\n";   
  memset(pkt, 0x00, sizeof(ssdp_packet_t));
  pkt->method = NONE; 
  pkt->type = UNKNOWN;
  pkt->recvd = millis();
  pkt->valid = true;
  char buffer[SSDP_BUFFER_SIZE] = {0};
  uint8_t pos = 0;
  while(p.available() > 0) {
    char c = p.read();
    if(pkt->method == NONE) {
      if(c == ' ') {
        _trim(buffer);
        pos = 0;
        if(strcmp(buffer, "M-SEARCH") == 0) pkt->method = SEARCH;
        else if(strcmp(buffer, "NOTIFY") == 0) pkt->method = NOTIFY;
      }
      else {
        if(pos < SSDP_BUFFER_SIZE - 1) {
          buffer[pos++] = c;
          buffer[pos] = '\0';
        }
      }
    }
    else {
      if(c == '\r' || c == '\n') {
        _trim(buffer);
        if(strcasecmp(buffer, "* HTTP/1.1") != 0) pkt->valid = false;
        break;
      }
      else {
        if(pos < SSDP_BUFFER_SIZE) {
          buffer[pos++] = c;
          buffer[pos] = '\0';
        }
      }
    }
    if(c == ' ' && pkt->method == NONE) {
      pkt->valid = false;
      break; // We didn't get a method we want. 
    }
  }
  enum {KEY, HOST, MAN, ST, MX, AGENT, HEAD, SERVER, NTS, LOCATION, USN, NT} keys;
  keys = KEY;
  if(pkt->method == SEARCH) {
    // Get all the key:value pairs
    pos = 0;
    buffer[0] = '\0';
    while(p.available() > 0) {
      char c = p.read();
      if(keys == KEY) {      
        if(c == ':') {
          _trim(buffer);
          if(strcasecmp(buffer, "MAN") == 0) keys = MAN;
          else if(strcasecmp(buffer, "ST") == 0) keys = ST; 
          else if(strcasecmp(buffer, "MX") == 0) keys = MX;
          else if(strcasecmp(buffer, "HOST") == 0) keys = HOST;
          else if(strcasecmp(buffer, "USER-AGENT") == 0) keys = AGENT;
          else if(strcasecmp(buffer, "SERVER") == 0) keys = SERVER;
          else if(strcasecmp(buffer, "NTS") == 0) keys = NTS;
          else if(strcasecmp(buffer, "NT") == 0) keys = NT;
          else if(strcasecmp(buffer, "LOCATION") == 0) keys = LOCATION;
          else if(strcasecmp(buffer, "USN") == 0) keys = USN;
          else keys = HEAD;
          pos = 0;
          buffer[0] = '\0';
        }
        else if(c == '\r' || c == '\n') {
          // If we find a key that ends before a : then we need to bugger out.
          keys = KEY;
          pos = 0;
          buffer[0] = '\0';
          //break;
        }
        else {
          if(pos < SSDP_BUFFER_SIZE - 1) {
            buffer[pos++] = c;
            buffer[pos] = '\0';
          }
        }
      }
      else {
        // We are reading a value
        if(c == '\r' || c == '\n') {
          _trim(buffer);
          switch(keys) {
            case HOST:
              if(strcasecmp(buffer, "239.255.255.250:1900") == 0) pkt->type = MULTICAST;
              else pkt->type = UNICAST;
              strcpy(pkt->host, buffer);
              break;
            case MAN:
              strcpy(pkt->man, buffer);
              break;
            case ST:
              strcpy(pkt->st, buffer);
              break;
            case MX:
              pkt->mx = atoi(buffer);
              break;
            case AGENT:
              strcpy(pkt->agent, buffer);
              break;
            default:
              break;
          }
          keys = KEY;
          pos = 0;
          buffer[0] = '\0';
        }
        else {
          if(pos < SSDP_BUFFER_SIZE - 1) {
            buffer[pos++] = c;
            buffer[pos] = '\0';
          }
        }
      }
    }
  }
  if(pkt->valid) {
    if(pkt->method == NONE) pkt->valid = false;
    if(pkt->method == SEARCH) {
      if(strcmp(pkt->man, "ssdp:discover") != 0) pkt->valid = false;
      if(strlen(pkt->st) == 0) pkt->valid = false;
      else if(strcmp(pkt->st, "ssdp:all") != 0 && 
        strcmp(pkt->st, "upnp:rootdevice") != 0 &&
        !this->_startsWith("uuid:", pkt->st) &&
        !this->_startsWith("urn:", pkt->st)) 
        pkt->valid = false;
      if(pkt->type == UNICAST) {
        if(strlen(pkt->host) == 0) pkt->valid = false;
      }
      else if(pkt->type == MULTICAST) {
        if(pkt->mx == 0 || isnan(pkt->mx)) pkt->valid = false;
      }
    }
    else if(pkt->method == NOTIFY) {
      // This is somebody else's notify so we should blow it off.
    }
    else {
      pkt->valid = false;
    }
  }
}
IPAddress SSDPClass::localIP()
{
    // Make sure we don't get a null IPAddress.
    tcpip_adapter_ip_info_t ip;
    if (WiFi.getMode() == WIFI_STA) {
        if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip)) {
            return IPAddress();
        }
    } else if (WiFi.getMode() == WIFI_OFF) {
        if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_ETH, &ip)) {
            return IPAddress();
        }
    }
    return IPAddress(ip.ip.addr);
}    
void SSDPClass::_sendResponse(IPAddress addr, uint16_t port, UPNPDeviceType *d, const char *st, response_types_t responseType) {
  char buffer[1460];
  IPAddress ip = this->localIP();
  char *pbuff = (char *)malloc(strlen_P(_ssdp_response_template)+1);
  if(!pbuff) {
    #ifdef DEBUG_SSDP
    DEBUG_SSDP.println("Out of memory for SSDP response");
    #endif
    return;
  }
  strcpy_P(pbuff, _ssdp_response_template);

  // Don't use ip.toString as this fragments the heap like no tomorrow.
  snprintf_P(buffer, sizeof(buffer)-1,
                       _ssdp_packet_template,
                       pbuff,
                       this->_interval,
                       d->getUSN(responseType),
                       "ST", st,
                       ip[0], ip[1], ip[2], ip[3], this->_port, d->schemaURL,
                       this->bootId, this->configId);
  buffer[sizeof(buffer) - 1] = '\0';
  this->_sendResponse(addr, port, buffer);
  free(pbuff);
}
void SSDPClass::_sendResponse(IPAddress addr, uint16_t port, const char *buff) {
  #ifdef DEBUG_SSDP
  DEBUG_SSDP.print("Sending Response to ");
  DEBUG_SSDP.print(IPAddress(addr));
  DEBUG_SSDP.print(":");
  DEBUG_SSDP.println(port);
  DEBUG_SSDP.println(buff);
  #endif
  
  _server.writeTo((const uint8_t *)buff, strlen(buff), addr, port);
}
void SSDPClass::_sendNotify() {
  for(uint8_t i = 0; i < this->m_cdeviceTypes; i++) {
    UPNPDeviceType *dev = &this->deviceTypes[i];
    if(i == 0 && (strlen(dev->deviceType) == 0 || !dev->isActive)) Serial.printf("The device type is empty: %s\n", dev->isActive ? "true" : "false");
    if(strlen(dev->deviceType) > 0 && dev->isActive) {
      unsigned long elapsed = (millis() - dev->lastNotified);
      if(!dev->lastNotified || (elapsed * 5) > (this->_interval * 1000)) {
        #ifdef DEBUG_SSDP
        DEBUG_SSDP.print(dev->deviceType);
        DEBUG_SSDP.print(" Time since last notified: ");
        DEBUG_SSDP.print(elapsed/1000);
        DEBUG_SSDP.print("sec ");
        DEBUG_SSDP.print(this->_interval);
        DEBUG_SSDP.println("sec");
        #endif
        this->_sendNotify(dev, i == 0);
      }
      else {
        /*
        #ifdef DEBUG_SSDP
        DEBUG_SSDP.print(dev->deviceType);
        DEBUG_SSDP.print(" Time since last notified: ");
        DEBUG_SSDP.print(elapsed/1000);
        DEBUG_SSDP.print("sec ");
        DEBUG_SSDP.print(this->_interval);
        DEBUG_SSDP.println("sec -- SKIPPING");
        #endif
        */
      }
    }
  }
}
void SSDPClass::_sendNotify(const char *msg) {
    //_server->append(msg, strlen(msg));
    //_server->send(IPAddress(SSDP_MULTICAST_ADDR), SSDP_PORT);
    #ifdef DEBUG_SSDP
    DEBUG_SSDP.println("--------------- SEND NOTIFY PACKET ----------------");
    DEBUG_SSDP.println(msg);
    #endif
    _server.writeTo((uint8_t *)msg, strlen(msg), IPAddress(SSDP_MULTICAST_ADDR), SSDP_PORT);
}
void SSDPClass::_sendNotify(UPNPDeviceType *d) { this->_sendNotify(d, strcmp(d->deviceType, this->deviceTypes[0].deviceType) == 0); }
void SSDPClass::_sendNotify(UPNPDeviceType *d, bool root) {
  char buffer[1460];
  IPAddress ip = this->localIP();
  /*
  "NOTIFY * HTTP/1.1\r\n"
  "HOST: 239.255.255.250:1900\r\n"
  "NTS: ssdp:alive\r\n";
  "CACHE-CONTROL: max-age=%u\r\n" // _interval
  "SERVER: Arduino/1.0 UPNP/1.1 %s/%s\r\n" // _modelName, _modelNumber
  "USN: %s\r\n" // _uuid
  "%s: %s\r\n"  // "NT" or "ST", _deviceType
  "LOCATION: http://%s:%u/%s\r\n" // WiFi.localIP(), _port, _schemaURL
  */
  char *pbuff = (char *)malloc(strlen_P(_ssdp_notify_template)+1);
  if(!pbuff) {
    #ifdef DEBUG_SSDP
    DEBUG_SSDP.println(PSTR("Out of memory for SSDP response"));
    #endif
    return;
  }
  strcpy_P(pbuff, _ssdp_notify_template);
  if(root) {
    // Send 1 for root
    snprintf_P(buffer, sizeof(buffer),
                         _ssdp_packet_template,
                         pbuff,
                         this->_interval,
                         d->getUSN(response_types_t::root), // USN
                         "NT", "upnp:rootdevice",
                         ip[0], ip[1], ip[2], ip[3], this->_port, d->schemaURL, this->bootId, this->configId);
     this->_sendNotify(buffer);
  }
  // Send 1 for uuid
  snprintf_P(buffer, sizeof(buffer),
                       _ssdp_packet_template,
                       pbuff,
                       this->_interval,
                       d->getUSN(response_types_t::uuid),
                       "NT", d->getUSN(response_types_t::uuid),
                       ip[0], ip[1], ip[2], ip[3], _port, d->schemaURL, this->bootId, this->configId);
   this->_sendNotify(buffer);
  // Send 1 for deviceType
  snprintf_P(buffer, sizeof(buffer),
                       _ssdp_packet_template,
                       pbuff,
                       this->_interval,
                       d->getUSN(response_types_t::deviceType),
                       "NT", d->deviceType,
                       ip[0], ip[1], ip[2], ip[3], _port, d->schemaURL, this->bootId, this->configId);
  this->_sendNotify(buffer);
  d->lastNotified = millis();
  free(pbuff);
}
void SSDPClass::setActive(uint8_t ndx, bool isActive) {
  UPNPDeviceType *d = &this->deviceTypes[ndx];
  if(!isActive) {
    if(this->isStarted) this->_sendByeBye(d, ndx == 0);
    d->isActive = false;
  }
  else {
    d->isActive = true;
    d->lastNotified = 0;
    if(this->isStarted) this->_sendNotify(d, ndx == 0);
  }
}
void SSDPClass::_sendByeBye() {
  for(uint8_t i = 0; i < this->m_cdeviceTypes; i++) {
    UPNPDeviceType *dev = &this->deviceTypes[i];
    if(strlen(dev->deviceType) > 0) {
      #ifdef DEBUG_SSDP
      DEBUG_SSDP.print(dev->deviceType);
      DEBUG_SSDP.print(" ");
      DEBUG_SSDP.print(this->_interval);
      DEBUG_SSDP.println("sec");
      #endif
      this->_sendByeBye(dev, i == 0);
    }
  }
}
void SSDPClass::_sendByeBye(const char *msg) {
    //_server->append(msg, strlen(msg));
    //_server->send(IPAddress(SSDP_MULTICAST_ADDR), SSDP_PORT);
    _server.writeTo((uint8_t *)msg, strlen(msg), IPAddress(SSDP_MULTICAST_ADDR), SSDP_PORT);
}
void SSDPClass::_sendByeBye(UPNPDeviceType *d) { this->_sendByeBye(d, strcmp(d->deviceType, this->deviceTypes[0].deviceType) == 0); }
void SSDPClass::_sendByeBye(UPNPDeviceType *d, bool root) {
  char buffer[1460];
  IPAddress ip = this->localIP();
  /*
  "NOTIFY * HTTP/1.1\r\n"
  "HOST: 239.255.255.250:1900\r\n"
  "NTS: ssdp:byebye\r\n";
  "CACHE-CONTROL: max-age=%u\r\n" // _interval
  "SERVER: Arduino/1.0 UPNP/1.1 %s/%s\r\n" // _modelName, _modelNumber
  "USN: %s\r\n" // _uuid
  "%s: %s\r\n"  // "NT" or "ST", _deviceType
  "LOCATION: http://%s:%u/%s\r\n" // WiFi.localIP(), _port, _schemaURL
  */
  if(root) {
    // Send 1 for root
    snprintf_P(buffer, sizeof(buffer)-1,
                         _ssdp_bye_template,
                         "upnp:rootdevice", d->getUSN(response_types_t::root), this->bootId, this->configId);
     this->_sendNotify(buffer);
  }
  // Send 1 for uuid
  snprintf_P(buffer, sizeof(buffer)-1,
                       _ssdp_bye_template,
                       d->getUSN(response_types_t::uuid),
                       d->getUSN(response_types_t::uuid), this->bootId, this->configId);
   this->_sendNotify(buffer);
  // Send 1 for deviceType
  snprintf_P(buffer, sizeof(buffer)-1,
                       _ssdp_bye_template,
                       d->deviceType,
                       d->getUSN(response_types_t::deviceType), this->bootId, this->configId);
   this->_sendNotify(buffer);
}
void SSDPClass::_addToSendQueue(IPAddress addr, uint16_t port, UPNPDeviceType *d, const char *st, response_types_t responseType, uint8_t sec) {
  /*
  typedef struct ssdp_response_t {
    IPAddress address;
    uint16_t port;
    UPNPDeviceType *dev;
    bool sendUUID;
    unsigned long sendTime;
    char st[SSDP_DEVICE_TYPE_SIZE];
  };
  */
  for(uint8_t i = 0; i < SSDP_QUEUE_SIZE; i++) {
    if(this->sendQueue[i].waiting) {
      // Check to see if this is a reply to the same place.
      ssdp_response_t *q = &this->sendQueue[i];
      if(q->address == addr && q->port == port && q->responseType == responseType) {
        #ifdef DEBUG_SSDP
        DEBUG_SSDP.printf("There is already a response to this query in slot %u\n", i);
        #endif
        return;
      }
    }
  }
  // Find an empty queue slot and fill it.
  for(uint8_t i = 0; i < SSDP_QUEUE_SIZE; i++) {
    if(!this->sendQueue[i].waiting) {
        #ifdef DEBUG_SSDP
        DEBUG_SSDP.printf("Queueing SSDP response in slot %u\n", i);
        DEBUG_SSDP.println("*********************************");
        #endif
        ssdp_response_t *q = &this->sendQueue[i];
        q->dev = d;
        q->sendTime = millis() + (random(0, sec - 1) * 1000L);
        q->address = addr;
        q->port = port;
        q->responseType = responseType;
        strlcpy(q->st, st, sizeof(ssdp_response_t::st)-1);
        q->waiting = true;
        return;
    }
  }
  // If we made it here then there were was not space available on the queue.
  #ifdef DEBUG_SSDP
  DEBUG_SSDP.println("The SSDP response queue was full.  Dropping request");
  #endif
}
void SSDPClass::_sendQueuedResponses() {
  for(uint8_t i = 0; i < SSDP_QUEUE_SIZE; i++) {
    if(this->sendQueue[i].waiting) {
      ssdp_response_t *q = &this->sendQueue[i];
      if(q->sendTime < millis()) {
          // Send the response and delete the pointer.
          #ifdef DEBUG_SSDP
            DEBUG_SSDP.print("Sending SSDP queued response ");
            DEBUG_SSDP.println(i);
          #endif
          this->_sendResponse(q->address, q->port, q->dev, q->st, q->responseType);
          q->waiting = false;
          return;
      }
    }
  }
}
void SSDPClass::_printPacket(ssdp_packet_t *pkt) {
  Serial.printf("Rec: %lu\n", pkt->recvd);
  switch(pkt->method) {
    case NONE:
      Serial.println("Method: NONE");
      break;
    case SEARCH:
      Serial.println("Method: SEARCH");
      break;
    case NOTIFY:
      Serial.println("Method: NOTIFY");
      break;
    default:
      Serial.println("Method: UNKOWN");
      break;
  }
  Serial.printf("ST: %s\n", pkt->st);
  Serial.printf("MAN: %s\n", pkt->man);
  Serial.printf("AGENT: %s\n", pkt->agent);
  Serial.printf("HOST: %s\n", pkt->host);
  Serial.printf("MX: %d\n", pkt->mx);
  Serial.printf("type: %d\n", pkt->type);
  Serial.printf("valid: %d\n", pkt->valid);
}
void SSDPClass::_processRequest(AsyncUDPPacket &p) {
  // This pending BS should probably be for unicast request only but we will play along for now.
  if(!this->_server.connected()) return;
  if(p.length() == 0) {
    //this->_sendQueuedResponses();
    return;
  }
  ssdp_packet_t pkt;
  this->_parsePacket(&pkt, p);
  if(pkt.valid && pkt.method == SEARCH) {
    // Check to see if we have anything to respond to from this packet.
    if(strcmp("ssdp:all", pkt.st) == 0) {
      #ifdef DEBUG_SSDP
      DEBUG_SSDP.println("---------------   ALL   ---------------------");
      this->_printPacket(&pkt);
      #endif
      for(uint8_t i = 0; i < this->m_cdeviceTypes; i++) {
        UPNPDeviceType *dev = &this->deviceTypes[i];
        if(strlen(this->deviceTypes[i].deviceType) > 0) {
          this->_addToSendQueue(p.remoteIP(), p.remotePort(), dev, pkt.st, response_types_t::root, pkt.mx);
          this->_addToSendQueue(p.remoteIP(), p.remotePort(), dev, pkt.st, response_types_t::uuid, pkt.mx);
          this->_addToSendQueue(p.remoteIP(), p.remotePort(), dev, pkt.st, response_types_t::deviceType, pkt.mx);
        }
      }
    }
    else if(strcmp("upnp:rootdevice", pkt.st) == 0) {
      UPNPDeviceType *dev = &this->deviceTypes[0];
      #ifdef DEBUG_SSDP
      DEBUG_SSDP.println("---------------   ROOT   ---------------------");
      this->_printPacket(&pkt);
      #endif
      if(pkt.type == MULTICAST) 
        this->_addToSendQueue(IPAddress(SSDP_MULTICAST_ADDR), SSDP_PORT, dev, pkt.st, response_types_t::root, pkt.mx);
      else 
        this->_sendResponse(p.remoteIP(), p.remotePort(), dev, pkt.st, response_types_t::root);
    }
    else {
      UPNPDeviceType *dev = nullptr;
      bool useUUID = false;
      if(this->_startsWith("uuid:", pkt.st)) {
        dev = this->findDeviceByUUID(pkt.st);
        useUUID = true;
      }
      else if(this->_startsWith("urn:", pkt.st)) { dev = this->findDeviceByType(pkt.st); }
      if(dev) {
        #ifdef DEBUG_SSDP
        this->_printPacket(&pkt);
        DEBUG_SSDP.println("--------------   ACCEPT   --------------------");
        #endif
        if(pkt.type == MULTICAST)
          this->_addToSendQueue(IPAddress(SSDP_MULTICAST_ADDR), SSDP_PORT, dev, pkt.st, useUUID ? response_types_t::uuid : response_types_t::root, pkt.mx);
        else {
          this->_sendResponse(p.remoteIP(), p.remotePort(), dev, pkt.st, useUUID ? response_types_t::uuid : response_types_t::root);
        }
      }
    }
  }
}
void SSDPClass::loop() {
  this->_sendQueuedResponses();
  // We need to send the notify if required.
  this->_sendNotify();
}
void SSDPClass::schema(Print &client) {
  IPAddress ip = this->localIP();
  uint8_t devCount = 0;
  for(uint8_t i = 0; i < this->m_cdeviceTypes; i++) {
    if(this->deviceTypes[i].deviceType && strlen(this->deviceTypes[i].deviceType) > 0) devCount++;
  }
  char schema_template[strlen_P(_ssdp_schema_template)+1];
  char device_template[strlen_P(_ssdp_device_schema_template)+1];
  strcpy_P(schema_template, _ssdp_schema_template);
  strcpy_P(device_template, _ssdp_device_schema_template);
  char buff[sizeof(device_template) + sizeof(UPNPDeviceType)];
  buff[0] = '\0';
  client.printf(schema_template,
    ip.toString().c_str(), _port);
  UPNPDeviceType *r = &this->deviceTypes[0];
  sprintf(buff, device_template,
        r->deviceType,
        r->friendlyName,
        r->presentationURL,
        r->serialNumber,
        r->modelName,
        r->modelNumber,
        r->modelURL,
        r->manufacturer,
        r->manufacturerURL,
        settings.fwVersion.name,
        r->uuid );
  char *devList = strstr(buff, "</device>") - strlen("</deviceList>");
  devList[0] = '\0';
  client.print(buff);
  for(uint8_t i = 1; i < this->m_cdeviceTypes; i++) {
    UPNPDeviceType *dev = &this->deviceTypes[i];
    if(strlen(dev->deviceType) > 0) {
        //Serial.print(devList);
        client.printf(device_template,
          dev->deviceType,
          dev->friendlyName,
          dev->presentationURL,
          dev->serialNumber,
          dev->modelName,
          dev->modelNumber,
          dev->modelURL,
          dev->manufacturer,
          dev->manufacturerURL,
          settings.fwVersion.name,
          dev->uuid);
    }
  }
  client.print(F("</deviceList></device></root>\r\n"));
  #ifdef DEBUG_SSDP
  DEBUG_SSDP.println("Sending upnp.xml");
  #endif
}
/*
void SSDPClass::setDeviceType(uint8_t ndx, UPNPDeviceType *dt) {
  this->setDeviceType(ndx, dt->deviceType);
  this->setName(ndx, dt->friendlyName);
  this->setURL(ndx, dt->presentationURL);
  this->setSerialNumber(ndx, dt->serialNumber);
  this->setUUID(ndx, dt->uuid);
  this->setModelName(ndx, dt->modelName);
  this->setModelNumber(ndx, dt->modelNumber);
  this->setManufacturer(ndx, dt->manufacturer);
  this->setManufacturerURL(ndx, dt->manufacturerURL);
}
*/
void SSDPClass::setTTL(const uint8_t ttl) { _ttl = ttl; }
void SSDPClass::setHTTPPort(uint16_t port) { _port = port; }
void SSDPClass::setInterval(uint32_t interval) { _interval = interval; }
void SSDPClass::setDeviceType(uint8_t ndx, const char *type) { this->deviceTypes[ndx].setDeviceType(type); }
void SSDPClass::setSchemaURL(uint8_t ndx, const char *url) {  this->deviceTypes[ndx].setSchemaURL(url); }
void SSDPClass::setUUID(uint8_t ndx, const char *uuid) { this->deviceTypes[ndx].setUUID(uuid); }
void SSDPClass::setUUID(uint8_t ndx, const char *prefix, uint32_t id) { 
    char svcuuid[50];
    sprintf(svcuuid, "%s%02X%02X%02X",
      prefix,
      (uint16_t)((id >> 16) & 0xff),
      (uint16_t)((id >> 8) & 0xff),
      (uint16_t)id & 0xff);
    this->setUUID(ndx, svcuuid);
}
void SSDPClass::setName(uint8_t ndx, const char *name) { this->deviceTypes[ndx].setName(name); }
void SSDPClass::setURL(uint8_t ndx, const char *url) { this->deviceTypes[ndx].setURL(url); }
void SSDPClass::setSerialNumber(uint8_t ndx, const char *serialNumber) { this->deviceTypes[ndx].setSerialNumber(serialNumber); }
//void SSDPClass::setSerialNumber(uint8_t ndx, const uint32_t serialNumber) { this->deviceTypes[ndx].setSerialNumber(serialNumber); }
void SSDPClass::setModelName(uint8_t ndx, const char *name) { this->deviceTypes[ndx].setModelName(name); }
void SSDPClass::setModelNumber(uint8_t ndx, const char *num) { this->deviceTypes[ndx].setModelNumber(num); }
void SSDPClass::setModelURL(uint8_t ndx, const char *url) { this->deviceTypes[ndx].setModelURL(url); }
void SSDPClass::setManufacturer(uint8_t ndx, const char *name) { this->deviceTypes[ndx].setManufacturer(name); }
void SSDPClass::setManufacturerURL(uint8_t ndx, const char *url) { this->deviceTypes[ndx].setManufacturerURL(url); }
void SSDPClass::setChipId(uint8_t ndx, uint32_t chipId) { this->deviceTypes[ndx].setChipId(chipId); }
#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_SSDP)
SSDPClass SSDP;
#endif
