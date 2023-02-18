#ifndef webserver_h
#define webserver_h
class Web {
  public:
    void sendCORSHeaders();
    void sendCacheHeaders(uint32_t seconds=604800);
    void startup();
    void begin();
    void loop();
    void end();
};
#endif
