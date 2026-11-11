#ifndef TX_BUFFER_H
#define TX_BUFFER_H
#include <Arduino.h>
#include "SomfyFrame.h"

// Repeater frames waiting to be handed to the radio task, and the jobs and
// events passed to and from it.  Kept free of the radio and timers so it can be
// unit tested on the PC (pio test -e native).

#define MAX_TX_BUFFER 5
#define TX_JOB_QUEUE_SIZE 32
// Slots only priority jobs (stops) may use so a stop always fits in the queue.
#define TX_RESERVED_SLOTS 4
// The radio task bit-bangs a whole job on loop()'s core at a higher priority
// so loop() only runs between jobs.  A frame takes about 140ms so this keeps a
// job around 7s, well inside the 15s task watchdog loop() is subscribed to.
#define TX_MAX_REPEATS 50

struct somfy_tx_t {
  void clear() {
    this->hwsync = 0;
    this->bit_length = 0;
    memset(this->payload, 0x00, sizeof(this->payload));
  }
  uint8_t hwsync = 0;
  uint8_t bit_length = 0;
  uint8_t payload[10] = {};
};
struct somfy_tx_queue_t {
  somfy_tx_queue_t() { this->clear(); }
  void clear() {
    for (uint8_t i = 0; i < MAX_TX_BUFFER; i++) {
      this->index[i] = 255;
      this->items[i].clear();
    }
    this->length = 0;
  }
  unsigned long delay_time = 0;  // Set by the caller, not before this millis().
  uint8_t length = 0;
  uint8_t index[MAX_TX_BUFFER] = {255};
  somfy_tx_t items[MAX_TX_BUFFER];
  // The oldest frame without removing it, so it can stay queued until the radio
  // takes it.
  bool peek(somfy_tx_t *tx) const;
  bool pop(somfy_tx_t *tx);
  // When full the oldest frame is thrown away.
  void push(uint8_t hwsync, byte *payload, uint8_t bit_length);
  private:
    // Position in index of the oldest frame, -1 when empty.
    int8_t oldest() const;
};
// A transmission handed off to the radio task.  The first frame is encoded by
// the producer into tx.  The frame is a copy so the radio task never touches the
// remote's lastFrame and is only used to re-encode 80-bit repeat frames.
struct somfy_tx_job_t {
  somfy_tx_t tx;            // Encoded first frame with its hw sync count and bit length.
  somfy_frame_t frame;
  uint8_t repeats = 0;      // Number of repeat frames after the first frame.
  uint8_t firstRepeat = 1;  // 80-bit repeat counter used for the first repeat frame.
  bool repeater = false;    // Repeater frame.  Waits for the receiver to be idle.
  // Set by RadioScheduler::submit().
  bool priority = false;    // May use the reserved slots.
  bool front = false;       // Queued ahead of the back.  Only for priority jobs.
  uint32_t ticket = 0;      // Matches the job's events up with it.
  uint32_t queuedAt = 0;    // millis() when it was submitted.
  uint16_t delayMs = 0;     // How long it waits after it was queued and after the job ahead of it ended.
};
enum class tx_event_t : uint8_t {
  started,  // The job is going on air.
  finished, // Every frame of the job went on air.
  aborted,  // The job was cut short by a flush or a radio reconfiguration.
  dropped   // The job was flushed and will never go on air.
};
// Reported by the radio task for every queued non-repeater job.
struct somfy_tx_event_t {
  tx_event_t type = tx_event_t::started;
  somfy_commands cmd = somfy_commands::My;
  uint32_t remoteAddress = 0;
  uint32_t ticket = 0;
  uint32_t at = 0;          // millis() when the job started or was dropped.
};
// Encodes the first frame of a transmission into the job.  Encoding here keeps
// its side effects on the frame (valid flag, default step size) visible to the
// caller and the radio task only re-encodes the 80-bit repeat frames.  More
// than TX_MAX_REPEATS repeats are cut down to it.
void encodeTxJob(somfy_tx_job_t &job, somfy_frame_t &frame, uint8_t repeat);
// Encodes a repeat of the frame last sent.  The first frame carries the
// frame's current repeat counter and the radio task counts up from there.
void encodeRepeatJob(somfy_tx_job_t &job, somfy_frame_t &frame, uint8_t repeat);
// Advances the frame's repeat counter past a repeat job the radio took.
void noteRepeatQueued(somfy_frame_t &frame, const somfy_tx_job_t &job);
#endif
