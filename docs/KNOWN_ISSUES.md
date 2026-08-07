# Known Issues

## Concurrent mutation during chunked `/controller` response

**Status:** open
**Introduced:** 2026-04-19, alongside the chunked-response conversion of `/controller` (see [`ControllerChunker` in src/Web.cpp](../src/Web.cpp) and the original crash report about `cbuf::resize` aborting `async_tcp`).

### Summary

The chunked streaming of `/controller` lazily reads `somfy.rooms`, `somfy.shades`, `somfy.groups`, and `somfy.repeaters` across many `async_tcp` callback invocations as the client's TCP send window opens. If another task mutates those arrays mid-stream, the rendered JSON can be internally inconsistent.

### Why this is strictly worse than the pre-chunked code

The old `handleController` ran the serializers synchronously inside a single request-handler invocation, so the full JSON was built in memory *before* any bytes were sent. Window of exposure: a few milliseconds.

The new `ControllerChunker` reads state as the response drains. On a slow link or under backpressure, that window is hundreds of ms to seconds.

### Concrete failure modes

1. A shade is deleted mid-stream (e.g. via `/deleteShade`) — it may appear in the `shades` array but be missing from a later group's `linkedShades`, or vice versa.
2. `lastRollingCode` / position fields change between the `shades` pass and a later group's `linkedShades` pass — the client sees two values for the same shade in one document.
3. A group's `linkedShades` list is mutated while the chunker iterates inside `S_GROUPS` — an entry is skipped or emitted twice.

### Fix options (pick one later)

- **(a) Document and accept.** In practice users rarely mutate shades while the config UI is loading. Zero code change.
- **(b) FreeRTOS mutex around `somfy` reads/writes.** Acquire for the full duration of the chunked response and in every mutating path (RF RX, MQTT, web handlers). Cleanest but wide-reaching.
- **(c) Up-front snapshot.** At handler start, copy the subset of `somfy` state the response will serialize into the `ControllerChunker`. Defeats part of the memory benefit — a full snapshot of shades + groups is close in size to the old growing cbuf. Could be reduced by snapshotting only minimal fields (IDs, names, rolling codes) and reading the rest live.

### Related

- Same exposure exists in any other endpoint converted to chunked responses next (`/discovery`, `/shades`). Resolve this issue before expanding the pattern.

## Silent truncation of large websocket events

**Status:** open
**Location:** [`JsonSockEvent` in src/WResp.cpp](../src/WResp.cpp), buffer defined at [src/Sockets.cpp:45-46](../src/Sockets.cpp#L45-L46) as `g_response[MAX_SOCK_RESPONSE]` = 2048 bytes.

### Summary

Socket events are built into a fixed 2 KB static buffer. On overflow, [`JsonSockEvent::_safecat`](../src/WResp.cpp) logs an error and returns without appending — the event is sent truncated, producing malformed socket.io text that the client drops silently.

Unlike the `/controller` HTTP crash, this path does **not** abort — there is no growing cbuf and no `new[]` on the send path. Per-client frame allocations inside `AsyncWebSocket` are bounded by the 2 KB buffer size and have their own overflow guard (queue drop / client disconnect).

### Concrete failure modes

1. A single event serializing a fully-populated shade (~1.3–1.5 KB for a shade with all `SOMFY_MAX_LINKED_REMOTES` = 7 populated) gets close to the 2 KB limit. Any additional fields or long names push it over and the JSON is silently cut mid-value.
2. Any event that loops over a collection (e.g. frequency-scan results, batch emits in `Somfy.cpp` around lines 1870–1975) can exceed 2 KB depending on size, with no indication to the client beyond the ESP-side `ESP_LOGE` line.

### Fix options (pick one later)

- **(a) Fail loud.** Keep truncation but emit a sentinel/error frame so the client knows the event was lost, instead of sending a malformed one.
- **(b) Split large events across frames.** Use the socket.io ack/chunk pattern to send an event in multiple frames when it wouldn't fit. Requires matching client-side reassembly.
- **(c) Raise `MAX_SOCK_RESPONSE`.** Cheapest, but just pushes the limit — doesn't eliminate the failure mode.

### Related

- Not the same code path as the `/controller` crash. Solve independently.
- Worth grepping for `JsonSockEvent` usages that iterate collections (see references in `Somfy.cpp`, `ESPNetwork.cpp`, `GitOTA.cpp`) to identify the most at-risk events.
