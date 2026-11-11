#ifndef TRANSCEIVER_H
#define TRANSCEIVER_H
#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "SomfyFrame.h"
#include "TxBuffer.h"
#include "RadioScheduler.h"
#include "PulseDecoder.h"

// The CC1101 radio: receive decoding, the radio task that transmits radio
// jobs, the frequency scan and the radio config.

#define MAX_RX_BUFFER 3
// A simple FIFO queue to hold rx buffers.  We are using
// a byte index to make it so we don't have to reorganize
// the storage each time we push or pop.
struct somfy_rx_queue_t {
  void init();
  uint8_t length = 0;
  uint8_t index[MAX_RX_BUFFER];
  somfy_rx_t items[MAX_RX_BUFFER];
  void push(somfy_rx_t *rx);
  bool pop(somfy_rx_t *rx);
};
struct transceiver_config_t {
    bool printBuffer = false;
    bool enabled = false;
    uint8_t type = 56;                // 56 or 80 bit protocol.
    radio_proto proto = radio_proto::RTS;
    uint8_t SCKPin = 18;
    uint8_t TXPin = 13;
    uint8_t RXPin = 35;
    uint8_t MOSIPin = 23;
    uint8_t MISOPin = 19;
    uint8_t CSNPin = 5;
    bool radioInit = false;
    float frequency = 433.42;         // Basic frequency
    float deviation = 47.60;          // Set the Frequency deviation in kHz. Value from 1.58 to 380.85. Default is 47.60 kHz.
    float rxBandwidth = 99.97;        // Receive bandwidth in kHz.  Value from 58.03 to 812.50.  Default is 99.97kHz.
    int8_t txPower = 10;              // Transmission power {-30, -20, -15, -10, -6, 0, 5, 7, 10, 11, 12}.  Default is 12.
    bool noiseDetection = false;      // Disable RX interrupt when RF noise is detected (>100 pulses/10s)
    void fromJSON(JsonObject& obj);
    void save();
    void load();
    void apply();
    // Removes a key no longer used from the open CC1101 namespace.
    void removeNVSKey(Preferences &pref, const char *key);
};
class Transceiver {
  private:
    static void handleReceive();
    static void txTask(void *arg);
    bool _received = false;
    somfy_frame_t frame;
    // Only touched under the TX queue lock.
    RadioScheduler sched;
    void startTxTask();
    bool transmitJob(somfy_tx_job_t &job);
    // Sends a job taken off the queue and leaves a frame beat after it.
    void runJob(somfy_tx_job_t &job);
  public:
    transceiver_config_t config;
    bool printBuffer = false;
    bool fromJSON(JsonObject& obj);
    // Swaps in, saves and applies the config.
    void setConfig(const transceiver_config_t &cfg);
    bool begin();
    void loop();
    bool end();
    bool receive(somfy_rx_t *rx);
    void clearReceived();
    void enableReceive();
    void disableReceive();
    somfy_frame_t& lastFrame();
    void sendFrame(byte *frame, uint8_t sync, uint8_t bitLength = 56);
    // Hands a job to the radio task, see RadioScheduler::submit().  Returns its
    // ticket, 0 when the radio can't take it.  Safe from any task.
    uint32_t submitTx(somfy_tx_job_t &job, bool priority = false, uint16_t delayMs = 0);
    void suspendTx(uint8_t reason);
    void resumeTx(uint8_t reason);
    bool txSuspended();
    bool txHasRoom();
    void beginTransmit();
    void endTransmit();
    void emitFrame(somfy_frame_t *frame, somfy_rx_t *rx = nullptr);
    void beginFrequencyScan();
    void endFrequencyScan();
    void processFrequencyScan(bool received = false);
    void emitFrequencyScan();
    bool usesPin(uint8_t pin);
};
// Bit length the radio was last set up for.  A frame sent without one
// gets it.
extern uint8_t radioBitLength;
#endif
