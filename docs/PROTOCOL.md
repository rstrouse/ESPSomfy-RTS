# Socket protocol 2

Why the protocol looks like this: [ADR 0005](adr/0005-one-websocket-protocol.md). Terms: [GLOSSARY.md](../GLOSSARY.md).

**The table of types, fields, error codes and feeds is defined once, in [src/ProtoSchema.h](../src/ProtoSchema.h).** The device sends it to every client as the schema, and `test/fixtures/proto2/schema.json` is a copy recorded by the e2e suite (`pytest test/e2e --record`). This page describes how the protocol works, not every field.

## Connection

- `ws://host/ws` on port 80. Under Secured the upgrade needs `?apikey=<token>` from `/login`; without a valid one it is refused with 401 ([ADR 0006](adr/0006-two-security-modes-eleven-http-routes.md)).
- An upgrade whose `Origin` header doesn't name the `Host` it was sent to is refused with 401 too, in either mode, so another site's page can't open the socket. Clients that aren't browsers send no `Origin`.
- Optional `?schema=<hash>`: the client already has the schema with that hash and doesn't need it again.
- At most 5 clients. A sixth is closed as soon as it connects.
- Text messages of at most 4 KB. A message may be sent as several frames, and a frame larger than one TCP segment (about 1.4 KB) reaches the device in pieces anyway; the device puts it back together. It does that for one message at a time across all clients: while it is, another client's long message is answered with busy, and the client sends it again. A message that hasn't arrived in full within 10 s is given up, and its client is told with a bad request. These refusals have no `id`, since the message couldn't be read. A binary encoding, if one comes, would be asked for at the upgrade (for example `?enc=bin`) and use binary frames.

## Envelope

Every frame, in both directions, is one JSON object:

```json
{"v":2,"t":129,"id":17,"d":{"s":3,"p":40}}
```

| Key | Meaning |
|---|---|
| `v` | protocol version, 2 |
| `t` | message type, a number from the schema |
| `id` | only on a client message that wants a reply, and on that reply |
| `d` | the payload, with keys from the schema's field dictionary |

Within `d`:
- keys are one or two characters, from one dictionary shared by every type;
- booleans are `1` and `0`;
- a field at its default is left out: a client fills it in from the schema;
- lists and objects use the same dictionary for the keys inside them.

The keys of `hello` (`fw`, `sh`) and of the schema itself (`T`, `F`, `E`, `Q`) never change, since a client needs them before it has a schema.

## Message types

| Range | Sender | Types |
|---|---|---|
| 1–15 | control | `hello` 1, `reply` 2, `next` 3, `sync` 4, `syncEnd` 5, `subscribe` 6, `unsubscribe` 7 |
| 16–127 | client ops | commands: `shadeCommand` 16, `groupCommand` 17, `tiltCommand` 18, `repeatCommand` 19, `setPositions` 20, `setSensor` 21, `setMyPosition` 22, `unpairShade` 23, `setRollingCode` 24, `sendRemoteCommand` 25; edits: 26–44 (`addRoom` … `groupSortOrder`); reads: `getNextRoom` 45, `getNextShade` 46, `getNextGroup` 47, `groupOptions` 48; settings and the device: `getSettings` 49, `saveSettings` 50, `scanWifi` 51, `beginFrequencyScan` 52, `endFrequencyScan` 53, `reboot` 54 |
| 128–255 | device events | `shadeState`, `shade`, `shadeRemoved`, `group`, `groupState`, `groupRemoved`, `room`, `roomRemoved`, `roomOrder`, `shadeOrder`, `groupOrder`, `shadeCommanded`, `memory`, `wifi`, `ethernet`, `frame`, `frequencyScan`, `repeaters`, `settingsChanged`, `wifiNetworks` |

A number is never reused. A retired type stays reserved.

## Connect sequence

1. The device sends `hello`: `{"fw":"<firmware>","sh":"<schema hash>"}`.
2. The device sends the first `sync` frame straight away. Unless the client passed the matching `?schema=`, the sync starts with the schema:
   - `T`: types, as `[number, name]`
   - `F`: fields, as `[key, name, kind, default]`, kind being `b` bool, `i` integer, `n` number, `s` string, `l` list, `o` object
   - `E`: error codes, as `[number, name]`
   - `Q`: feeds, as `[number, name]`
3. Then the device: `header` (limits, starting address, what a new shade gets, server id, hostname, model, chip, whether it has an OTA slot, firmware, connection type), `rooms`, `shades`, `groups`, `repeaters` and `status` (memory, and WiFi or Ethernet). A frame is at most 4 KB, an item is never split across frames, and several sections can share one.
4. The client sends `next` after each `sync` frame, and the device answers with the following one. With no `next` for 30 s the device closes the socket.
5. `syncEnd` follows the last `sync` frame.

The client can send `sync` at any time to get the shade model and status again (without the schema).

Events go out to every client from the moment it connects, so they can arrive during the sync, and even before the schema. A client keeps any frame it can't decode yet until it has the schema. Each sync frame is formatted from the live model, so an item in a later frame already holds every change before it. A client treats sync items and events alike: insert or update by id, and ignore removing an id it doesn't have.

## Events

| Event | When | Payload |
|---|---|---|
| `shadeState` | a shade moved or its state changed | id, position, target, direction, favorite, tilt, flags, rolling code |
| `shade` | a shade was added or a setting changed (edit, pairing, linked remote, group, its room deleted) | the whole shade, as in the sync |
| `group`, `groupState` | the same for a group: its settings or linked shades, or its flags and direction | |
| `room` | a room was added or saved | the whole room |
| `*Removed` | a shade, group or room was deleted | its id |
| `roomOrder`, `shadeOrder`, `groupOrder` | the sort order changed | `is`: the ids in order |
| `repeaters` | a repeater was linked or unlinked | the list |
| `shadeCommanded` | a remote or the device commanded a shade | id, address, command, source, rolling code, source address |
| `memory`, `wifi`, `ethernet` | every 30 s, WiFi only when the signal moved | |
| `settingsChanged` | another client saved settings | `section` |
| `wifiNetworks` | a WiFi scan this client asked for is done; to that client only | `networks`, each with `ssid`, `channel`, `strength`, `macAddress` |

## Commands

Every op carries an `id` and gets one reply. The ops do what the HTTP routes of the same name did before 4.0.0, with these arguments:

| Op | Payload | Does |
|---|---|---|
| `shadeCommand` | `shadeId`; `command` or `target`; optional `repeat`, `stepSize` | sends the command, or moves the shade to the position 0–100 |
| `tiltCommand` | `shadeId`; `command` or `target` | the same for the tilt |
| `groupCommand` | `groupId`, `command`; optional `repeat`, `stepSize` | sends the command as the group |
| `repeatCommand` | `shadeId` or `groupId`, `command`; optional `repeat`, `stepSize` | repeats the last frame when it was this command, or sends it |
| `setPositions` | `shadeId`; `position`, `tiltPosition` or both | sets where the shade is thought to be; nothing goes on air |
| `setSensor` | `shadeId` or `groupId`; `sunny`, `windy` or both; optional `repeat` | sends a sensor frame |
| `setMyPosition` | `shadeId`; `myPos`, `myTiltPos` or both | sets the favorite |
| `unpairShade` | `shadeId` | sends Prog and clears paired |
| `setRollingCode` | `shadeId`, `lastRollingCode` | sets the next rolling code; nothing goes on air |
| `sendRemoteCommand` | `remoteAddress`, `command`, `lastRollingCode`; optional `encKey`, `repeats` | sends one frame as a remote that isn't one of the shades |

- **An op reads only the fields that were sent.** Unlike what the device sends, a field left out of an op is not its schema default: it is not given, so `setPositions` with only `position` leaves the tilt as it is. A client sends every field it means, defaults included.
- `command` is a command's name, in any case: `My`, `Up`, `Down`, `MyUp`, `MyDown`, `UpDown`, `MyUpDown`, `Prog`, `SunFlag`, `Flag`, `StepUp`, `StepDown`, `Toggle`, `Sensor`, `Favorite`, `Stop`, or the spelling `shadeCommanded` reports (`Step Up`, `My+Up`).
- `repeat` left out means the shade's or the group's own repeats.
- Where they differ from the routes:
  - The keys come from the dictionary: `setMyPosition` takes `myPos` and `myTiltPos` (the route: `pos`, `tilt`); `setRollingCode` and `sendRemoteCommand` take `lastRollingCode` (the routes: `rollingCode`, `rcode`); `sendRemoteCommand` takes `remoteAddress` (the route: `address`).
  - `groupCommand`, `repeatCommand` and `sendRemoteCommand` need a `command`, where the routes send My without one.
  - `setMyPosition` takes a tilt on its own and keeps the lift favorite, where the route needs a lift position.
- A field of the wrong kind, a value out of range, or both of two alternatives, is a bad request, and `desc` names the field.
- The reply comes once the command has gone to the radio, or was refused: not found (no shade or group with that id), radio unavailable (the radio is paused, as during a frequency scan, or didn't take it), busy (the device's command queue is full, or `setMyPosition` on a shade that is moving) or bad request (`setPositions` with only a tilt, for a shade without one). It carries no state: the shade's `shadeState` events follow as it moves.
- `sendRemoteCommand` is answered as soon as the radio has taken the frame, not after the ops queued before it, and with radio unavailable when the radio is full rather than waiting for room.

## Edits

Each edit op is the edit route of the same name: `addRoom`, `addShade`, `addGroup`, `saveRoom`, `saveShade`, `saveGroup`, `deleteRoom`, `deleteShade`, `deleteGroup`, `linkRemote`, `unlinkRemote`, `linkToGroup`, `unlinkFromGroup`, `linkRepeater`, `unlinkRepeater`, `setPaired`, `roomSortOrder`, `shadeSortOrder`, `groupSortOrder`.

- The payload holds what the route's request does, under the dictionary's keys: a room's, shade's or group's fields as in the sync, and its id to save, delete or link it. `linkRemote` takes `remoteAddress` and `lastRollingCode`; `linkRepeater` takes `remoteAddress`; `setPaired` needs `paired`. A sort order takes `ids`, the ids in their new order.
- As with the commands, only the fields that were sent are applied. A key that isn't in the dictionary, a field the edit doesn't set, or a value of the wrong kind, is a bad request before anything is queued. A shade's save doesn't set `paired`, `linkedRemotes` or `flags` (`light` is its own field, and `setSensor` sets the sun and wind), and a group's doesn't set `sunSensor`, which comes from its shades.
- One edit runs at a time, whichever client or route sent it: while one is waiting, another gets busy.
- The reply comes once the edit is applied and saved. An add's reply carries the new item's id (`roomId`, `shadeId` or `groupId`). A refused edit has the route's reason in `desc`: not found for an id that doesn't exist, otherwise a bad request, with the reason's number in `validation` for a shade whose settings don't add up.
- The edit's events follow: the whole item for an add or save, `*Removed` for a delete, the new order for a sort order, `repeaters` for a repeater.

## Reads

What an edit form needs before it is sent, worked out by the device as the edit would be:

| Op | Payload | Reply |
|---|---|---|
| `getNextRoom` | | the `roomId` a new room gets |
| `getNextShade`, `getNextGroup` | | the `shadeId` or `groupId` a new one gets, a `remoteAddress` no shade or group uses, and the radio's `bitLength` and `proto` |
| `groupOptions` | `groupId` | the group's id, and in `ids` the shades it doesn't have yet |

When every slot is taken the reply is a bad request; `groupOptions` for a group that doesn't exist is not found.

## Settings

The settings are in five sections. `getSettings` and `saveSettings` name one in `section`:

| Section | Fields | The routes it replaces |
|---|---|---|
| `general` | `hostname`, `ssdpBroadcast`, `ntpServer`, `posixZone` | `/modulesettings`, `/setgeneral` |
| `network` | `connMode`; WiFi: `ssid`, `passphrase`, `roaming`, `hidden`; Ethernet: `boardType`, `phyType`, `CLKMode`, `phyAddress`, `PWRPin`, `MDCPin`, `MDIOPin`; the address: `dhcp`, `ip`, `gateway`, `subnet`, `dns1`, `dns2` | `/networksettings`, `/setNetwork`, `/setIP`, `/connectwifi` |
| `mqtt` | `enabled`, `pubDisco`, `protocol`, `hostname` (the broker's), `port`, `username`, `password`, `rootTopic`, `discoTopic` | `/mqttsettings`, `/connectmqtt` |
| `security` | `securityType`, `username`, `password`, `pin` | `/getSecurity`, `/saveSecurity` |
| `radio` | `bitLength`, `proto`, `enabled`, `TXPin`, `RXPin`, `SCKPin`, `MOSIPin`, `MISOPin`, `CSNPin`, `frequency`, `rxBandwidth`, `deviation`, `txPower`, `noiseDetection` | `/getRadio`, `/saveRadio` |

- **Reading.** `getSettings` replies with the section's fields. A secret is never sent: `hasPassphrase`, `hasPassword` and `hasPin` say whether one is set. `network` also has `hasEthernet`, and has the Ethernet fields only on a device with Ethernet. `radio` also has `radioInit`.
- `connMode` is the connection the settings ask for: 1 WiFi, 2 Ethernet, 3 Ethernet with WiFi as the fallback. The header's `connType` is the connection in use.
- **Saving.** `saveSettings` takes the section and the fields to change. As with every op, a field left out stays as it is. A secret sent empty keeps the stored one, except alongside a new `ssid` or MQTT `username`, as on the routes.
- Every field is checked before anything is saved:
  - its kind;
  - its range: `port` 1–65535, `txPower` -30–12, `bitLength` 56 or 80, `proto` 0, 1, 2, 8 or 9, a pin 0–48 (`PWRPin` and `phyAddress` may also be -1, for none), `frequency` 300–928, and so on;
  - a string's length, which is what the setting holds: `hostname` 31, `pin` 4, and so on;
  - an address: four numbers 0–255, or `""` for none.

  A bad request also comes for a field the section doesn't save (a `has*` field, `radioInit`, a field of another section, Ethernet on a device without it), and for a save with no fields.
- One save runs at a time, whichever client sent it: while one is waiting, another gets busy.
- The reply comes once the settings are saved and applied, as their route applies them. A saved time zone takes effect at once, where the route waited for a reboot.
  - `rebooting` 1 means the device restarts a second later for them. That happens for a new `connMode`, for a new SSID or passphrase while on WiFi with nobody on the soft AP, and for Ethernet settings while on Ethernet.
  - A security save's reply has the client's new token in `apiKey`, or none under None.
- The other clients get `settingsChanged` with the section.
- A security save that leaves the device Secured, or changes its mode, closes every client instead, the one that saved it too, once its reply has gone. Under Secured the tokens may no longer be good, and a client that connected under None had none. The saver connects again with the token from its reply; the others log in when theirs is refused. A save from None to None closes nobody.
- Where a section differs from its routes:
  - `/connectwifi` refused an SSID the device couldn't hear; the save doesn't check. The check stalled the device for a scan, and a hidden network can't be heard anyway. A client offers the networks from `scanWifi`.
  - `/modulesettings` and `/networksettings` also sent `fwVersion` and `chipModel`, which are in the header (`fw`, `chipModel`). Their `connType` is `connMode` in `network`, and `/networksettings`' hostname and `ssdpBroadcast` are in `general`.
  - `/scanaps` also sent the network the device is on: that is the `wifi` event, and `status` in the sync.

| Op | Payload | Reply |
|---|---|---|
| `scanWifi` | | ok at once. When the scan is done, `wifiNetworks` goes to each client that asked since the last one: the named networks that can be heard, at most 30. A failed scan lists none. |
| `beginFrequencyScan` | | ok, or radio unavailable when the radio is disabled. The radio sends nothing until the scan ends. Subscribe to the `frequencyScan` feed to follow it. |
| `endFrequencyScan` | | ok. A scan nobody ends stops after 5 minutes. |
| `reboot` | | ok, and the device restarts half a second later. |

These ops run on `loop()` like the rest. `endFrequencyScan` goes ahead of whatever is waiting in the loop queue: an unpair waits there for the radio to send again, which only the scan's end allows.

## Replies

A client message with an `id` gets exactly one `reply` with that `id`, to that client only, whether it is an op or a control message such as `next`: `ok` 1 on success, or an error `code` and a `desc` for people. Error codes are in the schema: bad request, unknown op, unauthorized, not found, busy, radio unavailable, internal.

A malformed message gets a bad request, with its `id` when it had one. A type a client may not send gets an unknown op.

## Feeds

`subscribe` and `unsubscribe` take a feed number in `fe`: `frames` (received remote frames, with their pulses) or `frequencyScan`. Their events go only to clients subscribed to them.

## Inside the device

async_tcp reads client frames and answers with fixed frames that hold nothing of the shade model: `hello`, errors and `ok`. Everything formatted from the shade model goes out from `loop()`, which is the only task that reads and writes it ([ADR 0002](adr/0002-loop-task-is-the-only-writer-of-the-shade-model.md)). Sync frames and events share one frame buffer, which belongs to `loop()`.
