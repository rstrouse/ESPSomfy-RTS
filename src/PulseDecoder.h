#ifndef PULSE_DECODER_H
#define PULSE_DECODER_H
#include <stdint.h>
#include <string.h>

// Turns the time between edges of a received RTS transmission into frame
// bits.  Runs in the receive interrupt, so it stays in IRAM on the ESP32, and
// is kept free of the radio so it can be unit tested on the PC
// (pio test -e native).

// Half an RTS bit in us.  A bit is two symbols with an edge in the middle.
#define SYMBOL 640

#define MAX_TIMINGS 300

typedef enum {
    waiting_synchro = 0,
    receiving_data = 1,
    complete = 2
} t_status;

struct somfy_rx_t {
    void clear() {
      this->status = t_status::waiting_synchro;
      this->bit_length = 56;
      this->cpt_synchro_hw = 0;
      this->cpt_bits = 0;
      this->previous_bit = 0;
      this->waiting_half_symbol = false;
      memset(this->payload, 0, sizeof(this->payload));
      memset(this->pulses, 0, sizeof(this->pulses));
      this->pulseCount = 0;
    }
    t_status status;
    uint8_t bit_length = 56;
    uint8_t cpt_synchro_hw = 0;
    uint8_t cpt_bits = 0;
    uint8_t previous_bit = 0;
    bool waiting_half_symbol;
    uint8_t payload[10];
    unsigned int pulses[MAX_TIMINGS];
    uint16_t pulseCount = 0;
};

enum class PulseResult : uint8_t {
  glitch,  // Too short to be a symbol.  The caller times the next edge from the last one.
  more,    // Taken, the frame isn't complete yet.
  frame    // rx holds a complete frame.
};
// Feeds the time since the previous edge to rx.
PulseResult decodePulse(somfy_rx_t &rx, uint32_t durationUs);
#endif

