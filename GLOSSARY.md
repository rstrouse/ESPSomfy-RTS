# Glossary

Domain terms used in the code, commits and design discussions.

## Radio

**Radio job**
One transmission handed to the radio task: an encoded first frame plus a number of repeat frames. A job is either *our own* (a command or sensor frame from one of our remotes, with outcome events) or a *repeater frame* (a frame received from someone else's remote and sent again). A repeater frame reports no events, and since it carries none of our rolling codes it never holds back a stop.

**RadioScheduler**
The module that owns the radio jobs waiting for, or on, the air. It decides their order, when a delayed job is due, which jobs a flush or suspend drops, and the outcome of every job. Producers hand it jobs and get a ticket back. The radio task asks it for the next job and reports back how the job went. It knows nothing about FreeRTOS, the CC1101 or shades.

**Ticket**
What submitting a radio job returns: the job's sequence number, 0 when the radio refused it. Every event about the job carries it. A command that went out over GPIO, not the radio, has ticket 0.

**Back job / front job**
A job normally joins the back of the queue. A *priority* job (a stop) goes to the front, unless an earlier job from the same remote is still waiting. Overtaking that job would put the remote's rolling codes out of order, and the motor would reject the older frame.

**Delayed job**
A job that must wait a given time after it was queued and after the transmission ahead of it ended, so the motor has settled (for example the long press that stores a favorite). It holds up the jobs behind it, but front jobs may overtake it while it waits.

**Flush**
Drops every waiting job (each gets a *dropped* event) and stops the job on air after its current frame (it gets *aborted*).

**Suspend**
Stops the radio taking jobs for a reason (frequency scan, OTA). Suspending flushes; while suspended, new jobs are refused.

**Job outcome**
Every job we submit gets exactly one *started* event, unless it is dropped before going on air. It then ends with exactly one of *finished* (every frame went on air), *aborted* (cut short) or *dropped* (never went on air).

**Rolling code**
The per-remote counter in every Somfy frame. A motor ignores any frame whose code is not newer than the last one it accepted, so a remote's frames must go on air in the order their codes were assigned.

**Pulse decoder**
Turns the times between edges of a received transmission into a frame's bits: it finds the hardware and software sync, which also tell a 56-bit frame from an 80-bit one, then reads the Manchester-coded bits.

## Shades

**Shade**
Any motor the firmware drives: a roller, blind, awning, shutter, drapery, garage door, gate or dry contact.
_Avoid_: blind (one type of shade), cover (Home Assistant's word)

**Shade model**
Everything the user has set up: rooms, shades, groups and repeaters, with their state.
_Avoid_: installation, config

**Favorite**
The position a motor stores and goes to on My. A shade may have none.
_Avoid_: My position

**Step**
A short move in one direction, sized by the shade's step time and the number of steps the frame asks for. An integrated tilt turns its slats to the end first, then moves the lift.

**Sensor frame**
A frame from a sun and wind sensor reporting whether it is sunny and whether it is windy. It carries no command; the shade decides what to do.

**Sun flag**
The per-shade switch that lets the sun move a shade. While it is on, lasting sun extends the shade and lasting cloud retracts it; wind retracts the shade whether it is on or not.

**Extend / retract**
Where the weather sends a shade: extend to its favorite, or fully when it has none; retract fully. A tilt-only shade extends and retracts its tilt, every other shade its lift.

**ShadeSettings**
What the user sets up for a shade and its behaviour only reads: shade and tilt type, travel times, step size, flip position, simulated My and sun sensor.
_Avoid_: config (the whole shade model is saved in the config file)

**RemoteSettings**
What shades and groups share about how their frames go on air: flip commands, bit length and repeats.

**Flags**
The byte a shade is saved and sent with. Its sun sensor, light and simulated My bits are settings; its sun flag, sunny, windy and demo mode bits are state. A group's flags are its shades' flags together.

**ShadeBehaviour**
What a shade does with frames, commands, group commands and radio events, and where that leaves it: positions, targets, directions, the favorite and the sensor state.
_Avoid_: shade logic, motion (`ShadeMotion` is the position maths it uses)

**MotorLink**
The seam a shade's commands reach its motor through, radio or GPIO. The shade hears its own frame; every other shade hears it through the link.

**ShadeEvents**
The seam through which a shade's behaviour reports what happened: changes for the change fan-out, commands for clients, a save wanted, and sensor flags for the groups.

## Entry points and outputs

**Entry point**
Where a request from outside reaches the firmware: an HTTP route, an MQTT command topic or a frame received from a remote.
_Avoid_: controller (it already names the owner of the shade list)

**HttpRoutes**
The HTTP routes on port 80: the web UI's files, login, the file transfers and the three commands for scripts. Everything else is an op on the socket.
_Avoid_: WebUiRoutes, ApiRoutes, WebApiController

**MqttCommands**
The MQTT topics ending in `/set` that command a shade or group.
_Avoid_: MQTTController

**MqttPublisher**
What sends the state of rooms, shades and groups, and the Home Assistant discovery config, to the MQTT broker.
_Avoid_: MQTTController

**MqttConnection**
The link to the MQTT broker that MqttCommands and MqttPublisher share: connecting, the root topic and the subscriptions.
_Avoid_: MQTTClass

**Change fan-out**
What sends everything that changed about a shade or group in one pass of `loop()` once: its changed MQTT topics, then one state event.

## Socket protocol

**Op**
A request a client sends over the socket: a command, a read, a model edit or a settings change.
_Avoid_: action, call

**Reply**
The answer to one op, sent only to the client that sent it: done, or an error with a code. Every op gets exactly one.
_Avoid_: response, request result

**Event**
A message the device sends without being asked: a change to a shade, group, room or setting, or a status update. Events come from the change fan-out and go to every client allowed to see them.
_Avoid_: increment, push

**Sync**
How a client catches up with the device: the shade model and status, sent item by item at the client's pace, after which events keep it current. It describes no single moment.
_Avoid_: snapshot (an item snapshot is a copy of one moment), initial load

**Feed**
A stream of events a client subscribes to on its own, such as received remote frames or a frequency scan.
_Avoid_: topic (MQTT's word), room (a room is part of the shade model)

**Schema**
The device's description of the protocol it speaks: what each message type, field and error code is, and what a field is when left out.

## Security

**Security mode**
Whether the device asks for login: *None* (everything open) or *Secured* (a PIN or password).
_Avoid_: Secure Config Only (removed)

**Public route**
What anyone may reach under Secured without a token: the web UI's files, `/loginContext`, `/login`, `/upnp.xml` and preflight requests. `/ws` counts as one at the HTTP level, because the socket checks the token itself in the handshake. Static serving answers the web UI's files alone, so `shades.cfg` isn't one.

**Token**
What logging in returns. It stands for the PIN or password, on one client address, and every request under Secured carries it.
_Avoid_: session, API key (the name of the field it travels in)

**Token key**
The secret a token is made with: random bytes the device makes on first boot and never sends anywhere. Changing it refuses every token.
_Avoid_: API key, server key

**Login throttle**
How the device slows down guessing: a client address with too many failed logins is refused for a while, and each refusal is a *lockout*, longer than the one before.
_Avoid_: login backoff, rate limit

## Domain modules

**Domain module**
A module that holds the rules of one part of the domain, such as what a shade request does. Entry points call it; it never calls an output itself.
_Avoid_: service, business service

**Shade request**
A request from outside to command a shade or group, set or clear its favorite, unpair it, or correct its position or rolling code. It names the shade or group and gives positions as the client sees them.
_Avoid_: web command

**ShadeRequests**
The domain module that carries out shade requests, whichever entry point they came from.

**ModelEditor**
The domain module that adds, changes, deletes, links and orders the rooms, shades, groups and repeaters of the shade model.
_Avoid_: ShadeConfig, InstallationEditor

**Item snapshot**
A copy of one room, shade or group, which an event, an MQTT topic or a command reply is written from.
_Avoid_: ModelSnapshot (the whole-model copy the HTTP reads used, removed in 4.0.0)

**LoopQueue**
The single queue through which shade requests, model edits, socket reads and settings ops, and restores reach `loop()` from other tasks, in the order they arrived.
_Avoid_: web command queue
