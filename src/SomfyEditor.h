#ifndef SOMFY_EDITOR_H
#define SOMFY_EDITOR_H
#include <ArduinoJson.h>
#include "ModelEditor.h"
#include "ProtoFrame.h"

// ModelEditor on the device: an edit of the shade model handed from the web
// server task to loop(), which applies it with the ModelEditor rules and replies
// to the client that sent it.

// One edit on its way to loop(): the protocol 2 edit op and a copy of its
// arguments.
struct model_edit_t {
  model_edit_type_t type = model_edit_type_t::add_room;
  proto_reply_to_t replyTo;
  JsonDocument doc;
};

// One edit at a time: false when one is already waiting or running.  Any task.
bool beginModelEdit();
// Gives up an edit that was begun but never queued.  Any task.
void cancelModelEdit(model_edit_t *edit);
// Begins an edit of that type: nullptr when one is already waiting or running
// (busy), or there was no memory for it.  Any task.
model_edit_t *newModelEdit(model_edit_type_t type, bool &busy);
// Hands a begun edit to loop(); false when the queue is full, and it is given
// up.  Any task.
bool submitModelEdit(model_edit_t *edit);
// Applies the edit, saves it, replies and frees it.  loop() only.
void runModelEdit(model_edit_t *edit);
// Applies the edit to the shade model and saves it.  loop() only.
edit_result_t applyModelEdit(model_edit_t &edit);
#endif
