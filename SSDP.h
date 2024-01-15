#ifndef SSDP_h
#define SSDP_h
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncUDP.h>

#define SSDP_UUID_SIZE              42
#define SSDP_SCHEMA_URL_SIZE        64
#define SSDP_DEVICE_TYPE_SIZE       64
#define SSDP_SERIAL_NUMBER_SIZE     37
#define SSDP_PRESENTATION_URL_SIZE  64
#define SSDP_MODEL_NAME_SIZE        64
#define SSDP_MODEL_URL_SIZE         64
#define SSDP_MODEL_VERSION_SIZE     32
#define SSDP_MANUFACTURER_SIZE      64
#define SSDP_USN_SIZE               128
#define SSDP_MANUFACTURER_URL_SIZE  64
#define SSDP_FRIENDLY_NAME_SIZE     64
#define SSDP_INTERVAL_SECONDS       1800
#define SSDP_MULTICAST_TTL          2
#define SSDP_HTTP_PORT              80
#define SSDP_CHILD_DEVICES          0
#define SSDP_QUEUE_SIZE             5

//#define DEBUG_SSDP Serial
//#define DEBUG_SSDP_PACKET Serial

typedef enum {
  NONE,
  SEARCH,
  NOTIFY
} ssdp_method_t;
typedef enum {
  root,
  uuid,
  deviceType
} response_types_t;

#define SSDP_FIELD_LEN             128
#define SSDP_LOCATION_LEN          256

typedef enum {
  UNKNOWN,
  MULTICAST,
  UNICAST
} ssdp_req_types_t;
struct ssdp_packet_t {
  bool pending;
  unsigned long recvd;
  ssdp_method_t                   method;
  char req                        [SSDP_FIELD_LEN];
  char st                         [SSDP_FIELD_LEN];
  char man                        [SSDP_FIELD_LEN];
  char agent                      [SSDP_FIELD_LEN];
  char host                       [SSDP_LOCATION_LEN];
  uint8_t mx;
  ssdp_req_types_t type;
  bool valid;
};

class UPNPDeviceType {
  char m_usn[SSDP_USN_SIZE];
  public:
    UPNPDeviceType();
    UPNPDeviceType(const char *deviceType);
    UPNPDeviceType(const String& deviceType) { UPNPDeviceType(deviceType.c_str()); }
    UPNPDeviceType(const String& deviceType, const String& uuid);
    UPNPDeviceType(const char *deviceType, const char *uuid, const char *friendlyName);
    UPNPDeviceType(const char *deviceType, const char *uuid);
    UPNPDeviceType(const String& deviceType, const String& uuid, const String& friendlyName) { UPNPDeviceType(deviceType.c_str(), uuid.c_str(), friendlyName.c_str()); }
    unsigned long lastNotified;
    char schemaURL[SSDP_SCHEMA_URL_SIZE];
    char uuid[SSDP_UUID_SIZE];
    char deviceType[SSDP_DEVICE_TYPE_SIZE];
    char friendlyName[SSDP_FRIENDLY_NAME_SIZE];
    char serialNumber[SSDP_SERIAL_NUMBER_SIZE];
    char presentationURL[SSDP_PRESENTATION_URL_SIZE];
    char manufacturer[SSDP_MANUFACTURER_SIZE];
    char manufacturerURL[SSDP_MANUFACTURER_URL_SIZE];
    char modelName[SSDP_MODEL_NAME_SIZE];
    char modelURL[SSDP_MODEL_URL_SIZE];
    char modelNumber[SSDP_MODEL_VERSION_SIZE];
    bool isActive;
    void setSchemaURL(const char *url);
    void setDeviceType(const char *dtype);
    void setUUID(const char *id);
    void setName(const char *name);
    void setURL(const char *url);
    void setSerialNumber(const char *sn);
    void setSerialNumber(const uint32_t sn);
    void setModelName(const char *name);
    void setModelNumber(const char *num);
    void setModelURL(const char *url);
    void setManufacturer(const char *name);
    void setManufacturerURL(const char *url);
    //char *getUSN();
    char *getUSN(const char *st);
    char *getUSN(response_types_t responseType);
    void setChipId(uint32_t chipId);
};
struct ssdp_response_t {
  bool waiting;
  IPAddress address;
  uint16_t port;
  UPNPDeviceType *dev;
  bool sendUUID;
  unsigned long sendTime;
  char st[SSDP_DEVICE_TYPE_SIZE];
  response_types_t responseType;
};
class SSDPClass {
  uint8_t m_cdeviceTypes = SSDP_CHILD_DEVICES + 1;
  protected:
    ssdp_response_t sendQueue[SSDP_QUEUE_SIZE];
    //ssdp_response_t sendQueue[SSDP_QUEUE_SIZE];
    //void _send(ssdp_method_t method, UPNPDeviceType *dev, bool useUUID);
    //void _sendAll(ssdp_method_t method, bool useUUID);
    void _startTimer();
    void _stopTimer();
    void _sendNotify();
    void _sendNotify(UPNPDeviceType *d);
    void _sendNotify(UPNPDeviceType *d, bool root);
    void _sendNotify(const char *);
    void _sendByeBye();
    void _sendByeBye(UPNPDeviceType *d);
    void _sendByeBye(UPNPDeviceType *d, bool root);
    void _sendByeBye(const char *);
    void _sendQueuedResponses();
    void _sendResponse(IPAddress addr, uint16_t port, UPNPDeviceType *d, const char *st, response_types_t responseType);
    void _sendResponse(IPAddress addr, uint16_t port, const char *msg);
    AsyncUDP _server;
    hw_timer_t* _timer = nullptr;
    uint16_t _port = SSDP_HTTP_PORT;
    uint8_t _ttl = SSDP_MULTICAST_TTL;
    uint32_t _interval = SSDP_INTERVAL_SECONDS;
    void _processRequest(AsyncUDPPacket &p);
    void _parsePacket(ssdp_packet_t *pkt, AsyncUDPPacket &p);
    void _printPacket(ssdp_packet_t *pkt);
    bool _startsWith(const char* pre, const char* str);
    void _addToSendQueue(IPAddress addr, uint16_t port, UPNPDeviceType *d, const char *st, response_types_t responseType, uint8_t sec);
   
  public:
    SSDPClass();
    ~SSDPClass();
    UPNPDeviceType deviceTypes[SSDP_CHILD_DEVICES + 1];
    bool begin();
    void loop();
    void end();
    bool isStarted = false;
    unsigned long bootId = 0;
    int configId = 0;
    IPAddress localIP();
    UPNPDeviceType* getDeviceTypes(uint8_t ndx);
    UPNPDeviceType* getDeviceType(uint8_t ndx);
    UPNPDeviceType* findDeviceByType(char *devType);
    UPNPDeviceType* findDeviceByUUID(char *uuid);
    void setHTTPPort(uint16_t port);
    void setTTL(uint8_t ttl);
    void setInterval(uint32_t interval);
    void schema(WiFiClient client) { schema((Print&)std::ref(client)); }
    void schema(Print &print);

    void setDeviceType(uint8_t ndx, UPNPDeviceType *dt);
    void setDeviceType(uint8_t ndx, const String& type) { setDeviceType(ndx, type.c_str()); }
    void setDeviceType(uint8_t ndx, const char *type);
    void setUUID(uint8_t ndx, const char *uuid);
    void setUUID(uint8_t ndx, const char *prefix, uint32_t id);
    void setName(uint8_t ndx, const char *name);
    void setURL(uint8_t ndx, const char *url);
    void setSchemaURL(uint8_t ndx, const char *url);
    void setSerialNumber(uint8_t ndx, const char *serialNumber);
    void setModelName(uint8_t ndx, const char *name);
    void setModelNumber(uint8_t ndx, const char *num);
    void setModelURL(uint8_t ndx, const char *url);
    void setManufacturer(uint8_t ndx, const char *name);
    void setManufacturerURL(uint8_t ndx, const char *url);
    void setActive(uint8_t ndx, bool bActive);
    void setChipId(uint8_t ndx, uint32_t chipId);
};
#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_SSSDP)
extern SSDPClass SSDP;
#endif
#endif
