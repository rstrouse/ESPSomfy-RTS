---
status: accepted, amended by ADR-0005
---

# The loop task is the only writer of the shade model

The shade model (rooms, shades, groups and repeaters) is changed only from `loop()` (the Arduino `loopTask`). Code on any other task, mainly the HTTP handlers on the async_tcp task, hands its change over as a request through the LoopQueue and reads the shade model only from a ModelSnapshot that `loop()` fills. On a dual-core ESP32 the two tasks run at the same instant, and on a single-core one async_tcp preempts `loop()` mid-step, so a second writer races `loop()`. Examples: `/linkRemote` setting a rolling code that a received frame overwrites a moment later, `/deleteShade` clearing a shade halfway through `checkMovement`, both tasks writing `/shades.cfg` and the plain `isDirty` flag, and `/controller` streaming a document in which one shade has two positions.

## Considered Options

- **A mutex around the shade model.** Rejected: every reader and writer has to remember to take it, and a missed lock is a silent bug. A chunked `/controller` response would hold it for seconds and stop `loop()` from receiving frames and moving shades in the meantime.
- **Keep the mix.** Shade commands were already queued while model edits and reads ran on async_tcp. Rejected because the races above stay.

## Consequences

> **Amended by [ADR 0005](0005-one-websocket-protocol.md) (socket protocol 2).** The rule above stands. These consequences are replaced: model edits no longer answer "accepted" and report through `requestResult`, because every socket op gets one reply from `loop()`. Reads no longer go through a ModelSnapshot, because `loop()` formats a sync from the live model. The caps on reads and the UI's 5 s `requestResult` wait go with them, and 4.0.0 removes ModelSnapshot.

- async_tcp must still never wait for `loop()`, so a model edit's HTTP reply can't block until the change is made. The reply only says the request was accepted; the result reaches clients as a WebSocket event once `loop()` has made the change. We chose that over a chunked HTTP response that `loop()` completes, because pushing results to clients is what the WebSocket is for and it needs no pool of pending responses.
- A read has to reply with data, so it does wait, without blocking: its chunked response sends nothing until `loop()` has filled the read's ModelSnapshot.
- MQTT commands already run on `loop()` (`mqtt.loop()` is called from it), but they go through the LoopQueue too. That gives them the radio back-pressure web commands have (`holdWebCommand`): while the radio is full they wait instead of being refused, and they run in the order they arrived with web requests.
- The LoopQueue is one FIFO, and no item jumps it, model edits included.
- Requests that hold heap are capped, so a burst can't exhaust it: at most one model edit (a copy of its body, up to 4 KB; a larger one gets 413) and two reads (a ModelSnapshot of about 5.6 KB each) at a time. One more gets 503 "Busy", and the web UI retries it.
- The web UI waits about 5 s for an edit's `requestResult`. If none comes, it says the change wasn't confirmed and offers to reload.
- The UI and the firmware change together, because the UI is served from LittleFS. An older UI against firmware with this rule waits for edit reply bodies that no longer come.
- With one writer, the domain modules are plain single-threaded code and can be tested natively without threads.
