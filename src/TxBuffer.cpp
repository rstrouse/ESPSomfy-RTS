#include "TxBuffer.h"
#include "esp_log.h"

// Logged as part of the radio code it came from.
static const char *TAG = "Somfy";

int8_t somfy_tx_queue_t::oldest() const {
  // The newest index is at position 0 so the oldest is the last one in use.
  for(int8_t i = MAX_TX_BUFFER - 1; i >= 0; i--) {
    if(this->index[i] < MAX_TX_BUFFER) return i;
  }
  return -1;
}
bool somfy_tx_queue_t::peek(somfy_tx_t *tx) const {
  const int8_t i = this->oldest();
  if(i < 0) return false;
  memcpy(tx, &this->items[this->index[i]], sizeof(somfy_tx_t));
  return true;
}
bool somfy_tx_queue_t::pop(somfy_tx_t *tx) {
  const int8_t i = this->oldest();
  if(i < 0) return false;
  uint8_t ndx = this->index[i];
  memcpy(tx, &this->items[ndx], sizeof(somfy_tx_t));
  this->items[ndx].clear();
  if(this->length > 0) this->length--;
  this->index[i] = 255;
  return true;
}
void somfy_tx_queue_t::push(uint8_t hwsync, uint8_t *payload, uint8_t bit_length) {
  if(this->length >= MAX_TX_BUFFER) {
    // We have overflowed the buffer simply empty the last item
    // in this instance we will simply throw it away.
    uint8_t ndx = this->index[MAX_TX_BUFFER - 1];
    if(ndx < MAX_TX_BUFFER) this->items[ndx].clear();
    this->index[MAX_TX_BUFFER - 1] = 255;
    this->length--;
  }
  uint8_t first = 0;
  // Place this record in the first empty slot.  There will
  // be one since we cleared a space above should there
  // be an overflow.
  for(uint8_t i = 0; i < MAX_TX_BUFFER; i++) {
    if(this->items[i].bit_length == 0) {
      first = i;
      this->items[i].bit_length = bit_length;
      this->items[i].hwsync = hwsync;
      memcpy(&this->items[i].payload, payload, sizeof(this->items[i].payload));
      break;
    }
  }
  // Move the index so that it is the at position 0.  The oldest item will fall off.
  for(uint8_t i = MAX_TX_BUFFER - 1; i > 0; i--) {
    this->index[i] = this->index[i - 1];
  }
  this->length++;
  // When popping from the queue we always pull from the end
  this->index[0] = first;
}
void encodeTxJob(somfy_tx_job_t &job, somfy_frame_t &frame, uint8_t repeat) {
  frame.encodeFrame(job.tx.payload);
  job.tx.hwsync = frame.bitLength == 56 ? 2 : 12;
  job.tx.bit_length = frame.bitLength;
  job.frame = frame;
  if(repeat > TX_MAX_REPEATS) {
    ESP_LOGW(TAG, "Limiting %u repeats to %u", repeat, TX_MAX_REPEATS);
    repeat = TX_MAX_REPEATS;
  }
  job.repeats = repeat;
}
void encodeRepeatJob(somfy_tx_job_t &job, somfy_frame_t &frame, uint8_t repeat) {
  encodeTxJob(job, frame, repeat);
  job.firstRepeat = frame.repeats + 2;
}
void noteRepeatQueued(somfy_frame_t &frame, const somfy_tx_job_t &job) {
  // Only advance the counter for frames that will go on air.
  frame.repeats += job.repeats + 1;
}
