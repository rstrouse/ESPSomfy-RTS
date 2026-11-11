#ifndef LOOP_QUEUE_H
#define LOOP_QUEUE_H
#include <stdint.h>
#include "ShadeRequests.h"
#include "ProtoFrame.h"

// The one queue through which other tasks hand work to loop(), which alone
// changes the shade model (docs/adr/0002-loop-task-is-the-only-writer-of-the-shade-model.md).
// The items and the hold rule are kept free of FreeRTOS so they can be unit
// tested on the PC (pio test -e native).

// Big enough for Home Assistant firing a command at every shade at once.
#define LOOP_QUEUE_SIZE 32

// What a restore_shades item restores, as bits of restoreParts.
#define RESTORE_SETTINGS    0x01
#define RESTORE_SHADES      0x02
#define RESTORE_NETWORK     0x04
#define RESTORE_TRANSCEIVER 0x08
#define RESTORE_REPEATERS   0x10
#define RESTORE_MQTT        0x20

enum class loop_item_type_t : uint8_t {
  shade_request,   // run by ShadeRequests
  restore_shades,  // restore from /shades.tmp, then reboot
  model_edit,      // apply an edit with ModelEditor
  proto_read,      // answer a protocol 2 read op from the shade model
  proto_device     // run a protocol 2 settings op or the WiFi scan (DeviceOps)
};
struct model_edit_t;
struct settings_save_t;
struct loop_item_t {
  loop_item_type_t type = loop_item_type_t::shade_request;
  shade_request_t request;        // shade_request
  uint8_t restoreParts = 0;       // restore_shades: RESTORE_* bits
  model_edit_t *edit = nullptr;   // model_edit: loop() runs and frees it
  // shade_request, proto_read and proto_device from a protocol 2 client:
  // loop() replies once it has run.
  proto_reply_to_t replyTo;
  uint8_t op = 0;                 // proto_read and proto_device: the op's type
  uint8_t groupId = 0;            // proto_read: groupOptions' group
  uint8_t section = 0;            // proto_device: getSettings' settings_section_t
  settings_save_t *save = nullptr; // proto_device: saveSettings' save, which loop() frees
};

// Whether the item sends anything on air.
inline bool loopItemUsesRadio(const loop_item_t &item) {
  return item.type == loop_item_type_t::shade_request && shadeRequestUsesRadio(item.request.type);
}
// Whether the item at the head of the queue waits for the radio.  An HTTP or
// MQTT client was already told it was accepted, so it stays queued until the
// radio has room rather than have the radio drop it; a protocol 2 client gets
// its reply once it has run.  While TX is suspended nothing gets through
// anyway so nothing is held back then, except an unpair, which waits until TX
// resumes rather than leave the shade paired.  Everything queued
// behind it waits too, protocol 2 replies included, so a frequency scan
// started after an unpair delays them until it ends.
inline bool holdLoopItem(const loop_item_t &item, bool txSuspended, bool txHasRoom) {
  if(txSuspended) return item.type == loop_item_type_t::shade_request && item.request.type == shade_request_type_t::unpair;
  return loopItemUsesRadio(item) && !txHasRoom;
}

class LoopQueue {
  public:
    void begin();
    // Hands the item to loop() without waiting, so it is safe from any task,
    // including async_tcp.  False when the queue is full.  first puts it
    // ahead of everything waiting: an item that ends a frequency scan, which
    // an unpair at the head waits for (holdLoopItem).
    bool submit(const loop_item_t &item, bool first = false);
    // Runs the waiting items in order.  Only from loop().
    void drain();
  private:
    void *queue = nullptr;
};
#endif
