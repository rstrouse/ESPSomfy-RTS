#include <ESPAsyncWebServer.h>
#include "Somfy.h"
#ifndef wresp_h
#define wresp_h
#include "JsonFormatter.h"

class AsyncJsonResp : public JsonFormatter {
  protected:
    void _safecat(const char *val, bool escape = false) override;
    AsyncWebServerRequest *_request = nullptr;
    AsyncResponseStream *_stream = nullptr;
  public:
    void beginResponse(AsyncWebServerRequest *request, char *buff, size_t buffSize);
    void endResponse();
    void flush();
};
#endif
