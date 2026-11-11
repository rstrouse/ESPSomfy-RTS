# Plan: authentication on every entry point, one HTTP server

**Status:** steps 1 to 3, PR B, PR C and 4a merged; 4b, the auth rules, is the last step.
**Written:** 2026-10-18, on top of PR #64 (stored secrets are write-only). **Revised:** 2026-10-22 for socket protocol 2: two security modes, eleven HTTP routes, the socket on port 80 ([ADR 0005](adr/0005-one-websocket-protocol.md), [ADR 0006](adr/0006-two-security-modes-eleven-http-routes.md)). Terms are defined in [GLOSSARY.md](../GLOSSARY.md).

## Why

- **Port 80 doesn't check login.** About 60 of its 70 routes call no auth check at all: settings, security, network, MQTT, radio, firmware, `/restore`, pairing, rolling codes. Only the ten that share a handler with port 8081 check it (`/controller`, `/rooms`, `/shades`, `/groups`, `/room`, `/shade`, `/group`, `/backup`, `/reboot`, `/downloadFirmware`). Logging in only hides parts of the UI.
- **"Secure Config Only" lets everything through.** No caller ever asks for the config level, so with that mode every check passes.
- **The token key is public.** API tokens are `HMAC(serverId, PIN-or-password + client IP)`. `serverId` is the last 3 bytes of the MAC address, which every device on the LAN can see, and it is also published in mDNS, in the retained MQTT `serverId` topic and in `/discovery`. Anyone who sees a token can recover a 4-character PIN offline. Tokens never expire.
- **`/login` has no rate limit.** A digits-only PIN (10,000 options) falls to an online attack within minutes.
- **CORS is wide open.** Every response carries `Access-Control-Allow-Origin: *` and `Allow-Headers: *`. Any website open in a browser on the LAN can call the device and read the replies.
- **The websocket has no auth.** With protocol 2 it carries every command and edit, and its frame feed carries received remote addresses and rolling codes.
- **`serveStatic("/")` serves all of LittleFS**, including `shades.cfg` and `shades.tmp`.

## Threat model

- **In scope:** another client on the same LAN that can send HTTP and websocket requests. That includes a browser on the LAN running a malicious page.
- **Out of scope:** someone sniffing traffic (everything is plain HTTP, and TLS on the ESP32 is out of reach), and a device exposed to the internet.

## Decisions

### Security modes
- **None:** everything is open.
- **Secured** (PIN or password): the socket upgrade and every HTTP route need a valid token, except the public ones below.
- "Secure Config Only" goes. A device set to it becomes Secured when it upgrades, so its main page asks for login. The 4.0.0 release notes say so.
- `serverId` stays public everywhere, including `/loginContext`. It is an identity, and HA uses it as the device's unique id. Only the token key changes (PR B).

### HTTP routes ([ADR 0006](adr/0006-two-security-modes-eleven-http-routes.md))
- **Public:** static web assets, `/loginContext`, `/login`, `/upnp.xml` (SSDP), OPTIONS preflight requests.
- **Token under Secured:** `/backup`, `/restore`, `/updateFirmware`, `/updateApplication`, `/shadeCommand`, `/groupCommand`, `/tiltCommand`.
- Which routes are public is one pure function with native tests (`isPublicRoute` in [src/HttpAccess.h](../src/HttpAccess.h)). It counts `/ws` as public because the socket checks its own token in the handshake.
- One middleware checks the token for every other route. It runs once the body is in, so the uploads check the token on their first chunk too and drop a refused one.
- Static serving covers the web assets only, so the rolling codes in `shades.cfg` are reachable only through the authenticated `/backup`.
- The web UI sends `apikey` on its backup and upload requests too.

### Websocket
- **Handshake:** the socket is `ws://host/ws` on port 80. The token goes in `?apikey=<token>`, because browsers can't set headers on a websocket. `handleHandshake` checks it with the same comparison the HTTP routes use. `request->url()` is the path only, so the token doesn't reach the slow-request log.
- **Rules:** under None anyone can connect. Under Secured the handshake needs a valid token, and a connection that has one may send every op and subscribe to every feed.
- **Logging in with a socket already open:** the UI reconnects the socket with the token. There is no in-band upgrade message.
- **Changing the security settings:** a `saveSettings` of the security section that leaves the device Secured, or changes its mode, closes every socket, since their tokens may no longer be good, and a socket opened under None had none. The client that saved is closed once its reply, which carries its new token, has gone, so no socket stays open on a check it never passed. The others log in when refused.
- **Origin:** a handshake whose `Origin` header doesn't match `Host` is refused, in either mode (`originMatchesHost` in [src/HttpAccess.h](../src/HttpAccess.h)). One without `Origin` comes from a client that isn't a browser and passes.

### CORS
- The wildcard `Access-Control-Allow-*` default headers go. The UI is served from the same origin, and HA and scripts aren't browsers, so CORS doesn't affect them.
- A build flag (`WEB_CORS` in `platformio.ini`) turns the headers back on for UI development from `file://`, which `baseUrl` in `index.js` supports. It also lets the socket take `Origin: null`, which such a page sends.

### UI
- The token is kept in `sessionStorage`: it survives a reload, is dropped when the tab closes, and is cleared when the socket upgrade is refused.
- On a 401, or a refused upgrade, the UI opens the login panel (`security.authUser()`) and drops that request, with no automatic retry. The user repeats the action.

### Token key (PR B)
- The token keeps its shape, `HMAC(key, PIN-or-password + client IP)`: stateless, and it survives reboots. Only the key changes.
- **Key:** 32 random bytes from `esp_fill_random`, generated on first boot in their own NVS namespace (`TOKEN`). Never sent anywhere, never in a backup, replaced only when NVS is erased. There is no rotate button: changing the PIN or password already changes every token.
  - The key is made in `Web::begin`, before WiFi starts, so the RNG takes its entropy from the bootloader's source (`bootloader_random_enable`) rather than the radio.
- **Comparison:** constant-time (`tokenMatches` in [src/ApiToken.h](../src/ApiToken.h)), used for HTTP, the websocket handshake and `/login` alike.
- **Migration:** none needed. The browser logs in again when its token is refused, and HA logs in again at runtime.

### Login throttle (PR C)
- Per client IP: after 5 failed `/login` attempts, refuse that IP for 30 s, doubling each time up to 15 min. A success resets it.
  - After the first lockout, each failure starts the next one, so an IP gets one try per lockout rather than five.
  - An hour without a failure forgets the IP.
  - A refused IP gets 429 with `Retry-After`, before its credentials are checked. Home Assistant tries again later instead of asking for new credentials.
  - The device keeps 8 IPs ([src/LoginThrottle.h](../src/LoginThrottle.h)). While all of them hold recent failures, every other IP shares one more slot. That way many addresses don't each get five free tries, but an unknown IP may have to wait during such an attack.
- Only `/login` is throttled. After PR B a token is a 256-bit HMAC with a secret key, so guessing one through `apikey` is hopeless.

### One server on port 80 ([ADR 0006](adr/0006-two-security-modes-eleven-http-routes.md))
- Port 80 serves the UI, the eleven HTTP routes and the socket at `/ws`. Ports 8081 and 8080 are removed in 4.0.0 with no alias period.
- The `_espsomfy_rts._tcp` mDNS record moves from port 8080 to 80, with the socket path as a TXT entry.
- The README gets an "API and ports" section for script and Node-RED users.

### Home Assistant ([ESPSomfy-RTS-HA](https://github.com/cjkas/ESPSomfy-RTS-HA))
- Today HA logs in only during the config flow, on a temporary API object, and sends `apikey: ""` at runtime. It only works with security None or Secure Config Only.
- **Setup:** the config flow reads the device info and security mode from `/loginContext`, then logs in with `/login`.
- **Login:** at `async_setup_entry` it logs in with the PIN or password stored in `entry.data`. On a refusal it logs in again once and retries; if that fails, it raises HA's reauth flow.
- **Socket:** it connects to `ws://host/ws?apikey=<token>` and speaks protocol 2. Against firmware whose `/ws` answers 404 or serves a web page, it falls back to the old HTTP API on 8081 and events on 8080.

## Order

Protocol 2 lands as one PR per step, and `main` keeps working throughout because the HTTP routes are removed last:

| # | Repo | Scope |
|---|---|---|
| 1a | firmware | protocol core next to the existing HTTP: `hello`, socket auth, sync, events, feeds, schema; the e2e suite starts |
| 1b | firmware | command ops through the LoopQueue, with replies from `loop()` |
| 1c | firmware | model edit ops, replacing `requestResult`, and the reads their forms need; messages reassembled from their pieces |
| 1d | firmware | settings ops (`getSettings`/`saveSettings` by section), the WiFi and frequency scans, reboot |
| 2 | firmware | web UI on ops; the CSS files bundled; token in `sessionStorage`; banner on a version mismatch |
| 3 | HA | protocol 2 with the old-firmware fallback; `/loginContext` setup; runtime login and reauth |
| 4a | firmware | removals: the old routes, ports 8081 and 8080, `/discovery`, ModelSnapshot; the mDNS record on port 80 |
| 4b | firmware | "Secure Config Only" goes; the token on every non-public route; static serving limited to web assets; CORS headers; Origin check |

PR B (token key) and PR C (login throttle) are separate firmware PRs that can land at any point. The GitHub update was removed ahead of the rest ([ADR 0007](adr/0007-no-github-update.md)), as protocol 2 didn't fit beside it.

## Tests

The rules go into small pure helpers with native tests, following [src/Secrets.h](../src/Secrets.h):
- which routes are public
- the token comparison
- the Origin/Host match
- the login throttle's schedule

The e2e suite (`test/e2e`) checks, when given credentials, that a socket without a token is refused, that the HTTP routes answer 401, that a token keyed with `serverId` is refused, and that a token works. It never changes the device's security settings itself.

## Not in this plan

- TLS.
- Session tokens with expiry.
- Contents of the binary backup file (it holds no passwords, but it does hold rolling codes; `/backup` needs the token under Secured).
