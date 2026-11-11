#include "JsonFormatter.h"
#include "esp_log.h"

static const char *TAG = "JSON";

void BufferedJsonFormatter::setBuffer(char *b, size_t sz) {
  this->buff = b;
  this->buffSize = sz;
  this->buff[0] = 0;
  this->_len = 0;
  this->_nocomma = true;
  this->_overflow = false;
  this->_objects = 0;
  this->_arrays = 0;
}
size_t BufferedJsonFormatter::length() const {
  return this->_len;
}
void JsonFormatter::discard() {
  // With no room every _safecat() takes the overflow path, and as it has
  // already overflowed that doesn't log either.
  this->buff = nullptr;
  this->buffSize = 0;
  this->_len = 0;
  this->_nocomma = true;
  this->_overflow = true;
  this->_objects = 0;
  this->_arrays = 0;
}
void JsonFormatter::rollback(const json_mark_t &m) {
  this->_len = m.len;
  if(this->buff) this->buff[this->_len] = 0;
  this->_objects = m.objects;
  this->_arrays = m.arrays;
  this->_nocomma = m.nocomma;
  this->_overflow = false;
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
*/
void JsonFormatter::addElem(const char *name, uint64_t lval) { sprintf(this->_numbuff, "%llu", (unsigned long long)lval); this->_appendNumber(name); }

void JsonFormatter::addElem(const char *name, bool bval) { strcpy(this->_numbuff, bval ? "true" : "false"); this->_appendNumber(name); }

void JsonFormatter::_safecat(const char *val, bool escape) {
  const size_t vlen = escape ? this->calcEscapedLength(val) + 2 : strlen(val);
  if(this->_len + vlen >= this->buffSize) {
    if(!this->_overflow && this->_logOverflow) ESP_LOGE(TAG, "JSON exceeded buffer size %u", (unsigned)this->buffSize);
    this->_overflow = true;
    return;
  }
  this->_appendAt(val, escape, vlen);
}
void JsonFormatter::_appendAt(const char *val, bool escape, size_t vlen) {
  char *p = this->buff + this->_len;
  if(escape) {
    *p++ = '"';
    *p = 0;
    this->escapeString(val, p);
    p += vlen - 2;
    *p++ = '"';
  }
  else {
    memcpy(p, val, vlen);
    p += vlen;
  }
  *p = 0;
  this->_len += vlen;
}
void JsonFormatter::_appendNumber(const char *name) { this->appendElem(name); this->_safecat(this->_numbuff); } 
size_t JsonFormatter::escapeChar(char c, char *out) {
  const char *esc = nullptr;
  switch(c) {
    case '"': esc = "\\\""; break;
    case '/': esc = "\\/"; break;
    case '\b': esc = "\\b"; break;
    case '\f': esc = "\\f"; break;
    case '\n': esc = "\\n"; break;
    case '\r': esc = "\\r"; break;
    case '\t': esc = "\\t"; break;
    case '\\': esc = "\\\\"; break;
    default: break;
  }
  if(esc) {
    strcpy(out, esc);
    return 2;
  }
  // Any other control character is not allowed raw in a JSON string.
  if((unsigned char)c < 0x20) return snprintf(out, 7, "\\u%04x", (unsigned char)c);
  out[0] = c;
  out[1] = 0x00;
  return 1;
}
uint32_t JsonFormatter::calcEscapedLength(const char *raw) {
  char tmp[7];
  uint32_t len = 0;
  for(size_t i = 0; raw[i]; i++) len += escapeChar(raw[i], tmp);
  return len;
}
void JsonFormatter::escapeString(const char *raw, char *escaped) {
  char *out = escaped + strlen(escaped);
  for(size_t i = 0; raw[i]; i++) out += escapeChar(raw[i], out);
}
