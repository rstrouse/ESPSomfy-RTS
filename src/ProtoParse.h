#ifndef PROTO_PARSE_H
#define PROTO_PARSE_H
#include <stddef.h>
#include <stdint.h>
#include <ArduinoJson.h>
#include "ProtoSchema.h"

// Reading the frames a socket protocol 2 client sends (docs/PROTOCOL.md).
// Kept free of the web server so it can be unit tested on the PC
// (pio test -e native).

// The largest frame a client may send; a longer one is refused unread.
#define PROTO_MAX_CLIENT_FRAME 4096

struct proto_message_t {
  uint8_t type = 0;
  uint32_t id = 0;  // 0 when the frame had none
};
// Reads one frame into doc and out.  none when it is a message a client may
// send; otherwise the error to reply with.  out.id is set whenever the frame
// had one, so the reply can name it.  The payload is doc["d"].
proto_error_t parseClientFrame(const char *text, size_t len, JsonDocument &doc, proto_message_t &out);
// An op (16-127) as opposed to a control message.
inline bool protoIsOp(uint8_t type) { return type >= 16 && type <= 127; }

// A field of a payload, or its default when it was left out.
int32_t protoInt(JsonObjectConst d, proto_field_t field);
bool protoBool(JsonObjectConst d, proto_field_t field);
// nullptr when it was left out, as a string's default is "".
const char *protoString(JsonObjectConst d, proto_field_t field);
bool protoHas(JsonObjectConst d, proto_field_t field);

// A value sent for field f, put into out as the HTTP route reads it: booleans
// as true and false.  False when it isn't of the field's kind.
bool protoRouteValue(const proto_field_info_t &f, JsonVariantConst v, JsonVariant out);
// A kind as a bad request names it: "whole number", "string" ...
const char *protoKindName(char kind);
#endif
