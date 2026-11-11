#ifndef PROTO_OPS_H
#define PROTO_OPS_H
#include <stdint.h>
#include <ArduinoJson.h>
#include "ProtoSchema.h"
#include "ShadeRequests.h"
#include "SomfyFrame.h"

// Socket protocol 2's command ops (docs/PROTOCOL.md): what each one asks for,
// read from its payload.  Kept free of the shade model and the web server so
// it can be unit tested on the PC (pio test -e native).
//
// The readers are strict where the HTTP routes guess: a field of the wrong
// kind, a value out of range or a command name that isn't one is a bad
// request, not a default.

// Every command op except sendRemoteCommand becomes a shade request that
// loop() runs.
// nullptr when the payload is good and req is filled in; otherwise what is
// wrong with it, for a bad request reply.
const char *parseShadeOp(uint8_t type, JsonObjectConst d, shade_request_t &req);
// sendRemoteCommand: a frame as a remote that isn't one of the shades, so it
// touches nothing of the shade model.  The same as parseShadeOp otherwise.
const char *parseRemoteCommandOp(JsonObjectConst d, somfy_frame_t &frame, uint8_t &repeats);
// A command by its name, as shadeCommanded reports it ("Up", "StepDown"),
// in any case.  False for a name that isn't a command.
bool protoCommand(const char *name, somfy_commands &cmd);

// What a reply says when the radio didn't take a command.
#define PROTO_RADIO_REFUSED "The radio didn't take the command"

// The reply to a shade request that loop() has run: none when it was done.
proto_error_t protoRequestError(shade_request_result_t result);
// What that reply says.
const char *protoRequestErrorDesc(const shade_request_t &req, shade_request_result_t result);
#endif
