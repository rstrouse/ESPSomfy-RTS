---
status: accepted
---

# Two security modes, and eleven HTTP routes on one server

With [ADR 0005](0005-one-websocket-protocol.md) moving everything else to the websocket, HTTP keeps only what has to happen before a socket exists, or moves a file. There is one server, on port 80, and the socket is served from it at `ws://host/ws`. Ports 8081 and 8080 are removed in 4.0.0 with no alias period, because that release breaks the HA integration contract anyway.

| Route | Why it stays HTTP |
|---|---|
| static web assets | the page has to load before it can open a socket |
| `/loginContext` | the security mode and device info, needed before login |
| `/login` | gets the token the socket needs |
| `/upnp.xml` | SSDP description (Windows' Network view, routers) |
| `/backup` | a file the browser saves |
| `/restore`, `/updateFirmware`, `/updateApplication` | file uploads |
| `/shadeCommand`, `/groupCommand`, `/tiltCommand` | one-line commands for scripts and Node-RED |

Static serving covers the web assets only, so `shades.cfg` and the other config files are never served. The script commands keep today's full field names, since people write those requests by hand. `/discovery` goes: Home Assistant's setup reads the device info from `/loginContext`, and mDNS still advertises `serverId`.

There are two security modes. **None** leaves everything open, except that a socket upgrade from another site's page is refused in either mode (the Origin check). **Secured** (PIN or password) needs the token on the socket upgrade (`?apikey=`) and on every route except the static files, `/loginContext`, `/login` and `/upnp.xml`. Devices set to "Secure Config Only" become Secured when they upgrade. The rest of the security work (the token key, the login throttle, CORS, the Origin check) is in [SECURITY_PLAN.md](../SECURITY_PLAN.md).

## Considered Options

- **Keep control and config access levels ("Secure Config Only").** Rejected: every op would need a level, and the mode was bypassed in practice, since nothing checked config routes for the config level.
- **Keep port 8081 for scripts.** Rejected: one server costs less memory, and scripts only change a port number.
- **Keep `/discovery` for scripts.** Rejected: it was the only HTTP read left of the shade model, and it alone kept the ModelSnapshot copy alive. A script that needs live state should use the socket.

## Consequences

- The release notes for 4.0.0 must cover: scripts move from port 8081 to 80, the HA integration must be updated, and "Secure Config Only" devices now ask for login on the main page.
- The `_espsomfy_rts._tcp` mDNS record, which Home Assistant's discovery matches, moves from port 8080 to 80 and gains the socket path as a TXT entry.
