---
status: accepted
---

# The web UI and Home Assistant talk to the device over one websocket protocol

The device was aborting while a browser loaded the web UI. Every open HTTP response holds its own TCP send window, the WiFi driver's copies of frames still on air, and a send buffer, which comes to about 14 KB each. A page load opens about ten of them. Six files with three answered at a time took 41 KB, a websocket connect about 10 KB more, and a `/controller` read about 22 KB with its ModelSnapshot. The idle heap is 80–89 KB. Over a weak WiFi link (-73 dBm, about 6 KB/s) these overlap for seconds until an allocation inside the web server fails and aborts.

We decided that the web UI and the Home Assistant integration do everything except the file transfers over one websocket (protocol 2). One connection means one send window per client, however much the client asks for. HTTP keeps only what a socket can't do; [ADR 0006](0006-two-security-modes-eleven-http-routes.md) lists what stays. The wire format is in [PROTOCOL.md](../PROTOCOL.md).

## Considered Options

- **Cap concurrent HTTP responses (a request gate) and budget the heap per response.** We built it and measured it, then dropped it. Each response still costs its full send window, a page load still opens ten requests, and the socket and `/controller` costs come on top of the gate's budget.
- **Keep HTTP, but make `/controller` smaller.** Rejected: it removes one cost and leaves the parallel connections.
- **Push the whole sync without flow control.** Rejected: after a reboot or a WiFi drop, every client reconnects within seconds. Five clients each queuing a full sync (about 20 KB at the model's maximum size) is more than the free heap.

## Consequences

- **Ops run on `loop()`.** async_tcp only parses an op and hands it over through the LoopQueue. `loop()` reads and writes the live shade model and sends the reply, so [ADR 0002](0002-loop-task-is-the-only-writer-of-the-shade-model.md)'s single-writer rule holds and no ModelSnapshot copy is needed. Long work, such as a WiFi scan, starts at once and reports through events, so `loop()` never stalls.
- **Every op has exactly one reply,** to its sender only, including model edits. `requestResult` and the edit's separate "accepted" step go. A command's reply carries no state; the movement arrives as events.
- **Sync on connect.** The device sends its schema (unless the client has it cached), then the shade model and status, in messages of at most 4 KB. Items are never split across messages. The client sends `next` after each message, and the sync ends with `syncEnd`. Each message is formatted from the live model, and events keep flowing during the sync, so the client upserts items by id and the result is exact without a copy. Settings aren't part of sync; a client asks for them when it needs them.
- **The send queue stays at 32 messages per client.** A full queue closes the client, which reconnects and syncs again. Dropping messages would leave a shade showing the wrong position with no way to notice.
- **A JSON envelope that a binary encoding can reuse.** Every frame is `{"v":2,"t":<type>,"id":<n>,"d":{...}}`, with `id` only on ops and replies. Message types and error codes are numbers that fit one byte and are never reused. A later binary encoding would put the same four fields in a fixed header, be asked for at the upgrade, and use binary websocket frames. The `42[...]` socket.io framing goes.
- **Short keys, described by a schema the device sends.** Payload keys are one or two characters from one global dictionary. Booleans are 1/0, and fields at their default are left out. The schema lists every type, field (name, kind, default) and error code. The firmware's table is the only copy, and its writers can only emit declared fields. `hello` carries the schema's hash, and a client that has it cached reconnects with `?schema=<hash>` to skip it. Clients expand frames to full names, so client code stays readable.
- **The web UI loads two files of its own: `index.js` and `app.css`.** Each file a page load fetches costs the device an open response, so the minify step joins the CSS files into `app.css`, and the protocol 2 client (`data-src/js/`) into `index.js`. Each client module ends with a `module.exports` footer, so Node can load it for the tests, and in the browser it adds nothing global but its classes.
- **Feeds.** High-rate events (received frames, frequency scan) go only to clients that subscribed to that feed.
- **A clean break, released as 4.0.0.** The firmware speaks only protocol 2. The new HA integration falls back to the old HTTP API and events when a device doesn't send `hello`; old integrations stop working against 4.0.0. The web UI checks `v` and shows a banner when the firmware and the web application don't match.
- **Tests.** Native tests cover parsing, the op table, permissions, the sync stream and the largest item fitting in 4 KB. A Python e2e suite, run by hand against a device, checks the protocol and survival under a reconnect storm and a page-load replay, and records fixtures. The web UI's client layer is tested with `node --test`. The HA integration tests its protocol module with plain pytest and the integration with pytest-homeassistant-custom-component, against a fake device that replays the fixtures.
