#include "WResp.h"

void AsyncJsonResp::beginResponse(AsyncWebServerRequest *request, char *buff, size_t buffSize) {
  this->_request = request;
  this->buff = buff;
  this->buffSize = buffSize;
  this->buff[0] = 0x00;
  this->_len = 0;
  this->_nocomma = true;
  this->_headersSent = false;
  this->_overflow = false;
  this->_stream = request->beginResponseStream("application/json");
}
void AsyncJsonResp::endResponse() {
  if(this->_len) this->flush();
  if(this->_request && this->_stream) {
    this->_request->send(this->_stream);
  }
}
void AsyncJsonResp::flush() {
  if(this->_stream && this->_len > 0) {
    this->_stream->write((const uint8_t *)this->buff, this->_len);
    this->buff[0] = 0x00;
    this->_len = 0;
  }
}
void AsyncJsonResp::_safecat(const char *val, bool escape) {
  const size_t vlen = escape ? this->calcEscapedLength(val) + 2 : strlen(val);
  if(vlen + this->_len >= this->buffSize) {
    this->flush();
    if(vlen + this->_len >= this->buffSize) {
      // Bigger than the whole buffer so stream it straight out.
      if(!this->_stream) {
        this->_overflow = true;
        return;
      }
      if(!escape) {
        this->_stream->print(val);
        return;
      }
      char esc[7];
      this->_stream->print("\"");
      for(size_t i = 0; val[i]; i++) {
        escapeChar(val[i], esc);
        this->_stream->print(esc);
      }
      this->_stream->print("\"");
      return;
    }
  }
  this->_appendAt(val, escape, vlen);
}
