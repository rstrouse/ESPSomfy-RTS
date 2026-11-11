#ifndef WEB_ROUTES_H
#define WEB_ROUTES_H
#include <ESPAsyncWebServer.h>
#include "LoopQueue.h"

// What HttpRoutes and the handlers it calls (Web.cpp) share.  Internal to the
// web server.

// Registers the HTTP routes on port 80, and the web UI's files.
void registerHttpRoutes(AsyncWebServer &server);

#define WEB_MAX_RESPONSE 4096
// Where replies are formatted before they are sent.  async_tcp only.
extern char g_async_content[WEB_MAX_RESPONSE];
extern const char _encoding_text[];
extern const char _encoding_json[];

// Hands an item to loop(), which owns the shades.  When it can't be the
// request is answered with the reason and false is returned.
bool queueLoopItem(AsyncWebServerRequest *request, const loop_item_t &item);
// A query parameter or form field, or an empty string when it is missing.
String asyncParam(AsyncWebServerRequest *request, const char *name);
bool asyncHasParam(AsyncWebServerRequest *request, const char *name);
#endif
