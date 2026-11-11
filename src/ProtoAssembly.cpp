#include <stdlib.h>
#include <string.h>
#include "ProtoAssembly.h"

void MessageAssembler::release() {
  free(this->buf);
  this->buf = nullptr;
  this->len = 0;
  this->owner = 0;
}
void MessageAssembler::drop(uint32_t clientId) {
  if(this->owner == clientId) this->release();
}
uint32_t MessageAssembler::expire(uint32_t now) {
  if(!this->owner || now - this->startedAt < PROTO_ASSEMBLY_TIMEOUT_MS) return 0;
  const uint32_t stalled = this->owner;
  this->release();
  return stalled;
}
char *MessageAssembler::take(size_t &len) {
  char *message = this->buf;
  len = this->len;
  this->buf = nullptr;
  this->release();
  return message;
}
assembly_t MessageAssembler::add(const proto_piece_t &piece, uint32_t now) {
  if(piece.first) {
    // Refused without taking the buffer from anyone.
    if(!piece.text) return assembly_t::notText;
    if(this->owner && this->owner != piece.clientId) return assembly_t::busy;
    this->release();
    this->buf = static_cast<char *>(malloc(this->limit + 1));
    if(!this->buf) return assembly_t::noMemory;
    this->owner = piece.clientId;
    this->startedAt = now;
  }
  // The rest of a message that was already answered.
  else if(this->owner != piece.clientId) return assembly_t::ignored;
  if(this->len + piece.len > this->limit) {
    this->release();
    return assembly_t::tooLarge;
  }
  memcpy(this->buf + this->len, piece.data, piece.len);
  this->len += piece.len;
  if(!piece.last) return assembly_t::more;
  this->buf[this->len] = '\0';
  return assembly_t::complete;
}
