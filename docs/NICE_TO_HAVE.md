 stop busy-waiting the CPU while transmitting
Today the CPU generates the radio waveform itself. sendFrame flips the TX pin and spins in delayMicroseconds() between flips. While it spins, the calling task can do nothing else. There are two ways out.

Option A: let the RMT peripheral generate the waveform (best)
The ESP32 has a hardware block called RMT (Remote Control Transceiver), built for IR and RF pulse trains:

You give it a list of {level, duration} items, and it drives the pin with exact timing.
During that time the CPU is free.
With a 1 µs tick, one item can last up to about 32 ms. That covers the longest pieces: the 10.9 ms wake-up pulse and the 27.4 ms gap between frames.
The encoding maps directly onto the existing sendFrame logic:

Current code	RMT items
Wake-up: high 10920 µs, low 7357 µs	1 item
Each hardware sync pulse: high 2560, low 2560	1 item each
Software sync: high 4850, low 640	1 item
Bit 1: low 640, high 640	1 item
Bit 0: high 640, low 640	1 item
Gap between frames: low 27434	1 item
A 56-bit frame comes to about 75 items, and a frame with all its repeats is a few hundred. The legacy driver in the Arduino 2.x core you use (driver/rmt.h) handles buffers larger than the channel's 64-item memory by refilling it from an interrupt. You call rmt_write_items(ch, items, n, true), and the calling task sleeps on a semaphore until the transmission is done instead of spinning. The C6 env is on core 3.x, which uses the newer rmt_tx API, so it needs its own #if branch.

What you gain:

No CPU time is spent on timing.
The timing is exact. Today, async_tcp (priority 10) and interrupts can preempt loopTask (priority 1) in the middle of a frame and stretch a pulse. You probably get away with it because Somfy receivers are tolerant.
You can still call it synchronously from loop() if you want, because the wait doesn't hold up other tasks.
The cost:

sendFrame has to be rewritten.
The waveforms should be checked on a logic analyser, especially the 80-bit frames and their repeat encoding.

The chips differ in channel count and buffer size:

Chip	RMT channels	Memory per channel (items)	DMA	In this project
ESP32 (classic)	8, each can be TX or RX	64	No	esp32dev, esp32devdbg
ESP32-S2	4, each can be TX or RX	64	No	–
ESP32-S3	4 TX + 4 RX	48	Yes (one TX channel)	esp32s3
ESP32-C3	2 TX + 2 RX	48	No	esp32c3
ESP32-C6	2 TX + 2 RX	48	No	esp32c6
ESP32-H2	2 TX + 2 RX	48	No	–
ESP32-P4	4 TX + 4 RX	48	Yes	–
ESP32-C2 (ESP8684)	None	–	–	–
What matters for the Somfy transmitter
One TX channel is enough. The radio has a single TX pin, so even the C3/C6 with 2 TX channels have one to spare.
Memory size doesn't limit you. A 56-bit frame is about 75 items, more than one 48- or 64-item block. The driver refills the buffer from an interrupt while it transmits, or the S3/P4 can use DMA. Either way you hand it the whole frame in one call.
The API depends on the core version, not the chip. This is where the real porting work is:
esp32dev, esp32c3, esp32s3 use platform espressif32 @ 6.13.0, which is Arduino core 2.x / IDF 4.4. That has the legacy driver/rmt.h API (rmt_config, rmt_write_items).
esp32c6 uses pioarduino, which is Arduino core 3.x / IDF 5. The legacy driver is deprecated there, and you use the new driver/rmt_tx.h API with an encoder, or Arduino's rmtInit() / rmtWrite().