#ifndef webserver_h
#define webserver_h
class Web {
  public:
    void sendCORSHeaders();
    void startup();
    void begin();
    void loop();
    void end();
};
#endif
