#include "ProtoSlots.h"

proto_slot_t *claimSlot(proto_slot_t *slots, uint8_t count, uint32_t clientId, bool withSchema) {
  for(uint8_t i = 0; i < count; i++) {
    uint32_t expected = 0;
    if(!slots[i].clientId.compare_exchange_strong(expected, clientId)) continue;
    slots[i].withSchema = withSchema;
    slots[i].feeds = 0;
    return &slots[i];
  }
  return nullptr;
}
void startSync(proto_slot_t &slot) { slot.wants.fetch_or(WANTS_SYNC); }
proto_slot_t *slotOf(proto_slot_t *slots, uint8_t count, uint32_t clientId) {
  for(uint8_t i = 0; i < count; i++)
    if(slots[i].clientId.load() == clientId) return &slots[i];
  return nullptr;
}
void freeSlot(proto_slot_t &slot) {
  // Cleared before the slot is free, so the next client starts clean.
  slot.wants = 0;
  slot.feeds = 0;
  slot.clientId = 0;
}

uint32_t takeWants(proto_slot_t &slot, uint32_t seenId) {
  const uint32_t wants = slot.wants.exchange(0);
  if(slot.clientId.load() == seenId) return wants;
  // The client left and another took the slot after loop() read it: what was
  // taken may be the new client's, so it goes back for the next pass.  A bit
  // the old client left behind does no harm, as the new one wants a sync.
  if(wants) slot.wants.fetch_or(wants);
  return 0;
}
sync_step_t nextSyncStep(proto_slot_t &slot, uint32_t now) {
  const uint32_t id = slot.clientId.load();
  if(id != slot.loopId) {
    // A client left or another took the slot.
    slot.loopId = id;
    slot.cur = sync_cursor_t();
    slot.awaitingNext = false;
  }
  if(!id) return sync_step_t::none;
  const uint32_t wants = takeWants(slot, id);
  if(wants & (WANTS_SYNC | WANTS_SYNC_AGAIN)) {
    // A client that just connected may skip the schema; one that asks again
    // never gets it.
    slot.cur = beginSync((wants & WANTS_SYNC) && slot.withSchema.load());
    return sync_step_t::send;
  }
  if((wants & WANTS_NEXT) && slot.awaitingNext) return sync_step_t::send;
  if(slot.awaitingNext && now - slot.sentAt > PROTO_SYNC_TIMEOUT_MS) {
    slot.awaitingNext = false;
    slot.cur = sync_cursor_t();
    return sync_step_t::timedOut;
  }
  return sync_step_t::none;
}
void syncFrameSent(proto_slot_t &slot, uint32_t now) {
  slot.awaitingNext = slot.cur.active();
  slot.sentAt = now;
}
