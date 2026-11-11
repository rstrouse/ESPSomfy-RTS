#ifndef PROTO_SLOTS_H
#define PROTO_SLOTS_H
#include <stdint.h>
#include <atomic>
#include "ProtoSync.h"

// The protocol 2 clients and their syncs (docs/PROTOCOL.md).  async_tcp
// claims and frees a slot and records what its client asked for; loop() moves
// each client's sync on.  The two sides share only the atomics.  Kept free of
// the web server so it can be unit tested on the PC (pio test -e native).

#define PROTO_MAX_CLIENTS 5
// How long a client may take to ask for the next sync frame.
#define PROTO_SYNC_TIMEOUT_MS 30000

// What a client asked loop() for, as bits.
enum : uint32_t {
  WANTS_SYNC = 0x01,        // just connected: a sync, with the schema unless it has it
  WANTS_NEXT = 0x02,        // the next sync frame
  WANTS_SYNC_AGAIN = 0x04   // a sync without the schema
};
struct proto_slot_t {
  // Set and cleared on async_tcp; clientId is 0 when the slot is free.
  std::atomic<uint32_t> clientId{0};
  std::atomic<uint32_t> wants{0};
  std::atomic<uint8_t> feeds{0};     // a bit per feed number
  std::atomic<bool> withSchema{true};
  // loop() only.
  uint32_t loopId = 0;               // the client loop() is serving here
  sync_cursor_t cur;
  bool awaitingNext = false;
  uint32_t sentAt = 0;
};

// async_tcp: the slot a new client takes, or nullptr when every slot is
// taken.
proto_slot_t *claimSlot(proto_slot_t *slots, uint8_t count, uint32_t clientId, bool withSchema);
// async_tcp: hello has gone out, so loop() may start the client's sync.
void startSync(proto_slot_t &slot);
proto_slot_t *slotOf(proto_slot_t *slots, uint8_t count, uint32_t clientId);
// async_tcp: the client has gone.
void freeSlot(proto_slot_t &slot);

// What loop() does for a slot on this pass.
enum class sync_step_t : uint8_t {
  none,
  send,      // send the sync frame at slot.cur to slot.loopId
  timedOut   // close slot.loopId: it sent no next in time
};
// loop(): picks up a new client, takes what it asked for and moves its sync
// on.  When the step is send, slot.cur is where the frame starts.
sync_step_t nextSyncStep(proto_slot_t &slot, uint32_t now);
// loop(): a sync frame went out, and more follow while slot.cur is active.
void syncFrameSent(proto_slot_t &slot, uint32_t now);
// loop(): what the slot's client asked for, provided it is still the client
// seenId that loop() read.  When another client took the slot in between,
// what it asked for stays for the next pass and 0 is returned.
uint32_t takeWants(proto_slot_t &slot, uint32_t seenId);
#endif
