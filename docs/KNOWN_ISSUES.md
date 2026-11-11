# Known Issues

## Tracked as GitHub issues

- [#77](https://github.com/cjkas/ESPSomfy-RTS/issues/77) Port 80 routes don't check login
- [#78](https://github.com/cjkas/ESPSomfy-RTS/issues/78) CORS allows any origin
- [#79](https://github.com/cjkas/ESPSomfy-RTS/issues/79) Websocket has no auth
- [#82](https://github.com/cjkas/ESPSomfy-RTS/issues/82) A saved WiFi passphrase or MQTT password can't be cleared from the UI
- [#83](https://github.com/cjkas/ESPSomfy-RTS/issues/83) Installing "Main" from the update dialog always fails
- [#84](https://github.com/cjkas/ESPSomfy-RTS/issues/84) Transmitting a frame stops `loop()` for about 365 ms
- [#85](https://github.com/cjkas/ESPSomfy-RTS/issues/85) Integrated tilt turns its slats back with an Up after a lift-only target
- [#86](https://github.com/cjkas/ESPSomfy-RTS/issues/86) ShadeBehaviour: self-echo, group commands and `lastFrame` (structural follow-ups)

## Command replies read the live shade model

**Status:** open. What's left of the concurrent-mutation problem that [ADR-0002](adr/0002-loop-task-is-the-only-writer-of-the-shade-model.md) fixed for reads and edits.
**Location:** the command handlers in [src/entryPoints/Web.cpp](../src/entryPoints/Web.cpp).

### Summary

The three HTTP commands for scripts (`/shadeCommand`, `/groupCommand`, `/tiltCommand`) copy the shade or group they report from the live shade model on async_tcp, so a reply can show a value from the middle of a change. The socket's ops reply from `loop()` and don't have this problem.

## `.gz does not exist` errors are log noise

**Status:** open. Cosmetic.
**Location:** `serveStatic` in [src/entryPoints/HttpRoutes.cpp](../src/entryPoints/HttpRoutes.cpp); `MINIFY_AND_GZIP` in [minify.py](../minify.py).

`serveStatic` tries `<file>.gz` first. For files stored without one (`appversion`, `icon.png`), LittleFS logs an `E`-level `does not exist, no permits for creation` line on every request. The fix would be for `minify.py` to gzip the plain files too; `appversion` must stay plain as well, because `ConfigSettings` reads it from LittleFS. `setTryGzipFirst(false)` doesn't help: the error comes from `exists()` probing the missing variant, so it would only move the noise to the gzip-only files.

