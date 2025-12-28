#include "WResp.h"
void JsonSockEvent::beginEvent(WebSocketsServer *server, const char *evt, char *buff, size_t buffSize) {
  this->server = server;
  this->buff = buff;
  this->buffSize = buffSize;
  this->_nocomma = true;
  this->_closed = false;
  snprintf(this->buff, buffSize, "42[%s,", evt);
}
void JsonSockEvent::closeEvent() {
  if(!this->_closed) {
    if(strlen(this->buff) < buffSize) strcat(this->buff, "]");
    else this->buff[buffSize - 1] = ']';
  }
  this->_nocomma = true;
  this->_closed = true;
}
void JsonSockEvent::endEvent(uint8_t num) {
  this->closeEvent();
  if(num == 255) this->server->broadcastTXT(this->buff);
  else this->server->sendTXT(num, this->buff);
}
void JsonSockEvent::_safecat(const char *val, bool escape) {
  size_t len = (escape ? this->calcEscapedLength(val) : strlen(val)) + strlen(this->buff);
  if(escape) len += 2;
  if(len >= this->buffSize) {
    Serial.printf("Socket exceeded buffer size %d - %d\n", this->buffSize, len);
    Serial.println(this->buff);
    return;
  }
  if(escape) strcat(this->buff, "\"");
  if(escape) this->escapeString(val, &this->buff[strlen(this->buff)]);
  else strcat(this->buff, val);
  if(escape) strcat(this->buff, "\"");
}
void JsonResponse::beginResponse(WebServer *server, char *buff, size_t buffSize) {
  this->server = server;
  this->buff = buff;
  this->buffSize = buffSize;
  this->buff[0] = 0x00;
  this->_nocomma = true;
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
}
void JsonResponse::endResponse() {
  if(strlen(buff)) this->send();
  server->sendContent("", 0);
}
void JsonResponse::send() {
    if(!this->_headersSent) server->send_P(200, "application/json", this->buff);
    else server->sendContent(this->buff);
    //Serial.printf("Sent %d bytes %d\n", strlen(this->buff), this->buffSize);
    this->buff[0] = 0x00;
    this->_headersSent = true;
}
void JsonResponse::_safecat(const char *val, bool escape) {
  size_t len = (escape ? this->calcEscapedLength(val) : strlen(val)) + strlen(this->buff);
  if(escape) len += 2;
  if(len >= this->buffSize) {
    this->send();
  }
  if(escape) strcat(this->buff, "\"");
  if(escape) this->escapeString(val, &this->buff[strlen(this->buff)]);
  else strcat(this->buff, val);
  if(escape) strcat(this->buff, "\"");
}

void JsonFormatter::beginObject(const char *name) {
  if(name && strlen(name) > 0) this->appendElem(name);
  else if(!this->_nocomma) this->_safecat(",");
  this->_safecat("{");
  this->_objects++;
  this->_nocomma = true;
}
void JsonFormatter::endObject() {
  //if(strlen(this->buff) + 1 > this->buffSize - 1) this->send();
  this->_safecat("}");
  this->_objects--;
  this->_nocomma = false;
}
void JsonFormatter::beginArray(const char *name) {
  if(name && strlen(name) > 0) this->appendElem(name);
  else if(!this->_nocomma) this->_safecat(",");
  this->_safecat("[");
  this->_arrays++;
  this->_nocomma = true;
}
void JsonFormatter::endArray() {
  //if(strlen(this->buff) + 1 > this->buffSize - 1) this->send();
  this->_safecat("]");
  this->_arrays--;
  this->_nocomma = false;
}

void JsonFormatter::appendElem(const char *name) {
  if(!this->_nocomma) this->_safecat(",");
  if(name && strlen(name) > 0) {
    this->_safecat(name, true);
    this->_safecat(":");
  }
  this->_nocomma = false;
}

void JsonFormatter::addElem(const char *name, const char *val) {
  if(!val) return;
  this->appendElem(name);
  this->_safecat(val, true);
}
void JsonFormatter::addElem(const char *val) { this->addElem(nullptr, val); }
void JsonFormatter::addElem(float fval) { sprintf(this->_numbuff, "%.4f", fval); this->_appendNumber(nullptr); }
void JsonFormatter::addElem(int8_t nval) { sprintf(this->_numbuff, "%d", nval); this->_appendNumber(nullptr); }
void JsonFormatter::addElem(uint8_t nval) { sprintf(this->_numbuff, "%u", nval); this->_appendNumber(nullptr); }
void JsonFormatter::addElem(int32_t nval) { sprintf(this->_numbuff, "%ld", (long)nval); this->_appendNumber(nullptr); }
void JsonFormatter::addElem(uint32_t nval) { sprintf(this->_numbuff, "%lu", (unsigned long)nval); this->_appendNumber(nullptr); }

/*
void JsonFormatter::addElem(int16_t nval) { sprintf(this->_numbuff, "%d", nval); this->_appendNumber(nullptr); }
void JsonFormatter::addElem(uint16_t nval) { sprintf(this->_numbuff, "%u", nval); this->_appendNumber(nullptr); }
void JsonFormatter::addElem(int64_t lval) { sprintf(this->_numbuff, "%lld", (long long)lval); this->_appendNumber(nullptr); }
void JsonFormatter::addElem(uint64_t lval) { sprintf(this->_numbuff, "%llu", (unsigned long long)lval); this->_appendNumber(nullptr); }
*/
void JsonFormatter::addElem(bool bval) { strcpy(this->_numbuff, bval ? "true" : "false"); this->_appendNumber(nullptr); }

void JsonFormatter::addElem(const char *name, float fval) { sprintf(this->_numbuff, "%.4f", fval); this->_appendNumber(name); }
void JsonFormatter::addElem(const char *name, int8_t nval) { sprintf(this->_numbuff, "%d", nval); this->_appendNumber(name); }
void JsonFormatter::addElem(const char *name, uint8_t nval) { sprintf(this->_numbuff, "%u", nval); this->_appendNumber(name); }
void JsonFormatter::addElem(const char *name, int32_t nval) { sprintf(this->_numbuff, "%ld", (long)nval); this->_appendNumber(name); }
void JsonFormatter::addElem(const char *name, uint32_t nval) { sprintf(this->_numbuff, "%lu", (unsigned long)nval); this->_appendNumber(name); }

/*
void JsonFormatter::addElem(const char *name, int16_t nval) { sprintf(this->_numbuff, "%d", nval); this->_appendNumber(name); }
void JsonFormatter::addElem(const char *name, uint16_t nval) { sprintf(this->_numbuff, "%u", nval); this->_appendNumber(name); }
void JsonFormatter::addElem(const char *name, int64_t lval) { sprintf(this->_numbuff, "%lld", (long long)lval); this->_appendNumber(name); }
void JsonFormatter::addElem(const char *name, uint64_t lval) { sprintf(this->_numbuff, "%llu", (unsigned long long)lval); this->_appendNumber(name); }
*/
void JsonFormatter::addElem(const char *name, bool bval) { strcpy(this->_numbuff, bval ? "true" : "false"); this->_appendNumber(name); }

void JsonFormatter::_safecat(const char *val, bool escape) {
  size_t len = (escape ? this->calcEscapedLength(val) : strlen(val)) + strlen(this->buff);
  if(escape) len += 2;
  if(len >= this->buffSize) {
    return;
  }
  if(escape) strcat(this->buff, "\"");
  if(escape) this->escapeString(val, &this->buff[strlen(this->buff)]);
  else strcat(this->buff, val);
  if(escape) strcat(this->buff, "\"");
}
void JsonFormatter::_appendNumber(const char *name) { this->appendElem(name); this->_safecat(this->_numbuff); } 
uint32_t JsonFormatter::calcEscapedLength(const char *raw) {
  uint32_t len = 0;
  for(size_t i = strlen(raw); i > 0; i--) {
    switch(raw[i]) {
      case '"':
      case '/':
      case '\b':
      case '\f':
      case '\n':
      case '\r':
      case '\t':
      case '\\':
        len += 2;
        break;
      default:
        len++;
        break;
    }
  }
  return len;
}
void JsonFormatter::escapeString(const char *raw, char *escaped) {
  for(uint32_t i = 0; i < strlen(raw); i++) {
    switch(raw[i]) {
      case '"':
        strcat(escaped, "\\\"");
        break;
      case '/':
        strcat(escaped, "\\/");
        break;
      case '\b':
        strcat(escaped, "\\b");
        break;
      case '\f':
        strcat(escaped, "\\f");
        break;
      case '\n':
        strcat(escaped, "\\n");
        break;
      case '\r':
        strcat(escaped, "\\r");
        break;
      case '\t':
        strcat(escaped, "\\t");
        break;
      case '\\':
        strcat(escaped, "\\\\");
        break;
      default:
        size_t len = strlen(escaped);
        escaped[len] = raw[i];
        escaped[len+1] = 0x00;
        break;
    }
  }
}
