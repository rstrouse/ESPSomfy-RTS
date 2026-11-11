#include "PulseDecoder.h"
#if defined(ESP32)
#include <esp_attr.h>
#define PULSE_DECODER_ATTR IRAM_ATTR
#else
#define PULSE_DECODER_ATTR
#endif

// Each timing is accepted within 30%.
#define TOLERANCE_MIN 0.7
#define TOLERANCE_MAX 1.3
static const uint32_t tempo_wakeup_pulse = 9415;
static const uint32_t tempo_wakeup_min = 9415 * TOLERANCE_MIN;
static const uint32_t tempo_wakeup_max = 9415 * TOLERANCE_MAX;
static const uint32_t tempo_wakeup_silence = 89565;
static const uint32_t tempo_wakeup_silence_min = 89565 * TOLERANCE_MIN;
static const uint32_t tempo_wakeup_silence_max = 89565 * TOLERANCE_MAX;
static const uint32_t tempo_synchro_hw_min = SYMBOL * 4 * TOLERANCE_MIN;
static const uint32_t tempo_synchro_hw_max = SYMBOL * 4 * TOLERANCE_MAX;
static const uint32_t tempo_synchro_sw_min = 4850 * TOLERANCE_MIN;
static const uint32_t tempo_synchro_sw_max = 4850 * TOLERANCE_MAX;
static const uint32_t tempo_half_symbol_min = SYMBOL * TOLERANCE_MIN;
static const uint32_t tempo_half_symbol_max = SYMBOL * TOLERANCE_MAX;
static const uint32_t tempo_symbol_min = SYMBOL * 2 * TOLERANCE_MIN;
static const uint32_t tempo_symbol_max = SYMBOL * 2 * TOLERANCE_MAX;
static const uint32_t tempo_if_gap = 30415;  // Gap between frames
static int16_t  bitMin = SYMBOL * TOLERANCE_MIN;
PULSE_DECODER_ATTR PulseResult decodePulse(somfy_rx_t &rx, uint32_t durationUs) {
    const unsigned int duration = durationUs;
    if (duration < bitMin) {
        // The incoming bit is < 448us so it is probably a glitch so blow it off.
        // We need to ignore this bit.
        // REMOVE THIS AFTER WE DETERMINE THAT THE out-of-bounds stuff isn't a problem.  If there are bits
        // from the previous frame then we will capture this data here.
        if(rx.pulseCount < MAX_TIMINGS && rx.cpt_synchro_hw > 0) rx.pulses[rx.pulseCount++] = duration;
        return PulseResult::glitch;
    }
    switch (rx.status) {
    case waiting_synchro:
        if(rx.pulseCount < MAX_TIMINGS) rx.pulses[rx.pulseCount++] = duration;
        if (duration > tempo_synchro_hw_min && duration < tempo_synchro_hw_max) {
            // We have found a hardware sync bit.  There should be at least 4 of these.
            ++rx.cpt_synchro_hw;
        }
        else if (duration > tempo_synchro_sw_min && duration < tempo_synchro_sw_max && rx.cpt_synchro_hw >= 4) {
            // If we have a full hardware sync then we should look for the software sync.  If we have a software sync
            // bit and enough hardware sync bits then we should start receiving data.  It turns out that a 56 bit packet
            // with give 4 or 14 bits of hardware sync.  An 80 bit packet gives 12, 13 or 24 bits of hw sync.  Early on
            // I had some shorter and longer hw syncs but I can no longer repeat this.
            memset(rx.payload, 0x00, sizeof(rx.payload));
            rx.previous_bit = 0x00;
            rx.waiting_half_symbol = false;
            rx.cpt_bits = 0;
            // Keep an eye on this as it is possible that we might get fewer or more synchro bits.
            if (rx.cpt_synchro_hw <= 7) rx.bit_length = 56;
            else if (rx.cpt_synchro_hw == 14) rx.bit_length = 56;
            else if (rx.cpt_synchro_hw == 13) rx.bit_length = 80; // The RS485 device sends this sync.
            else if (rx.cpt_synchro_hw == 12) rx.bit_length = 80;
            else if (rx.cpt_synchro_hw > 17) rx.bit_length = 80;
            else rx.bit_length = 56;
            //rx.bit_length = 80;
            rx.status = receiving_data;
        }
        else {
            // Reset and start looking for hardware sync again.
            rx.cpt_synchro_hw = 0;
            // Try to capture the wakeup pulse.
            if(duration > tempo_wakeup_min && duration < tempo_wakeup_max)
            {
                memset(&rx.payload, 0x00, sizeof(rx.payload));
                rx.previous_bit = 0x00;
                rx.waiting_half_symbol = false;
                rx.cpt_bits = 0;
                rx.bit_length = 56;
            }
            else if((rx.pulseCount > 20 && rx.cpt_synchro_hw == 0) || duration > 250000) {
              rx.pulseCount = 0;
            }
        }
        break;
    case receiving_data:
        if(rx.pulseCount < MAX_TIMINGS) rx.pulses[rx.pulseCount++] = duration;
        // We should be receiving data at this point.
        if (duration > tempo_symbol_min && duration < tempo_symbol_max && !rx.waiting_half_symbol) {
            rx.previous_bit = 1 - rx.previous_bit;
            // Bits come in high order bit first.
            rx.payload[rx.cpt_bits / 8] += rx.previous_bit << (7 - rx.cpt_bits % 8);
            ++rx.cpt_bits;
        }
        else if (duration > tempo_half_symbol_min && duration < tempo_half_symbol_max) {
            if (rx.waiting_half_symbol) {
                rx.waiting_half_symbol = false;
                rx.payload[rx.cpt_bits / 8] += rx.previous_bit << (7 - rx.cpt_bits % 8);
                ++rx.cpt_bits;
            }
            else {
                rx.waiting_half_symbol = true;
            }
        }
        else {
            //++rx.cpt_bits;
            // Start over we are not within our parameters for bit timing.
            memset(&rx.payload, 0x00, sizeof(rx.payload));
            rx.pulseCount = 1;
            rx.cpt_synchro_hw = 0;
            rx.previous_bit = 0x00;
            rx.waiting_half_symbol = false;
            rx.cpt_bits = 0;
            rx.bit_length = 56;
            rx.status = waiting_synchro;
            rx.pulses[0] = duration;
        }
        break;
    default:
        break;
    }
    return rx.status == receiving_data && rx.cpt_bits >= rx.bit_length ? PulseResult::frame : PulseResult::more;
}

