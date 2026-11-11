[![Release](https://img.shields.io/github/v/release/cjkas/ESPSomfy-RTS)](https://github.com/cjkas/ESPSomfy-RTS/releases/latest) [![Build](https://github.com/cjkas/ESPSomfy-RTS/actions/workflows/build.yaml/badge.svg?branch=main)](https://github.com/cjkas/ESPSomfy-RTS/actions/workflows/build.yaml) [![CodeQL](https://github.com/cjkas/ESPSomfy-RTS/actions/workflows/codeql.yaml/badge.svg?branch=main)](https://github.com/cjkas/ESPSomfy-RTS/actions/workflows/codeql.yaml) [![Coverage](https://img.shields.io/badge/coverage-report-blue)](https://cjkas.github.io/ESPSomfy-RTS/)

# ESPSomfy-RTS <image src="https://user-images.githubusercontent.com/47839015/218898940-3541b360-5c49-4e38-a918-392cd0408b76.png" align="right" style="width:177px;display:inline-block;float:right"></image>

Control Somfy RTS shades, blinds, awnings and shutters from an ESP32 and a CC1101 radio, about $12 in parts. It runs up to 32 shades and 16 groups, keeps track of their positions even when you use your old Somfy remote, and talks to Home Assistant, MQTT, Node-RED and your own scripts.

This is the maintained continuation of [rstrouse/ESPSomfy-RTS](https://github.com/rstrouse/ESPSomfy-RTS), which has had no commits since August 2024.

## What's new compared to rstrouse's version

* **No more random restarts.** The web server and sockets are asynchronous, which fixed the watchdog resets.
* **More boards.** ESP32, ESP32-S3, ESP32-C3, ESP32-C5, ESP32-C6 and ESP32-P4, on Arduino 3 and ESP-IDF 5.5.
* **One port, one socket.** Since 4.0.0 the web UI, the API and the events all run on port 80, see [API and ports](#api-and-ports).
* **Secured means secured.** With a PIN or password set, every route and the socket need a login, see [SECURITY.md](SECURITY.md).
* **Backups don't wear out the flash.** They are built in RAM, not written to LittleFS.
* **Verifiable releases.** Every release file comes with a checksum and a signed build attestation.
* **A maintained Home Assistant integration**: [cjkas/ESPSomfy-RTS-HA](https://github.com/cjkas/ESPSomfy-RTS-HA), installed through HACS.

## Install

1. Build the radio device: an ESP32 and a CC1101 connected with Dupont wires, no soldering needed. See the [Simple Hardware Guide](https://github.com/cjkas/ESPSomfy-RTS/wiki/Simple-ESPSomfy-RTS-device).
2. Download `SomfyController.onboard.<chip>.bin.zip` for your chip from the [latest release](https://github.com/cjkas/ESPSomfy-RTS/releases/latest). It holds the whole image: bootloader, partitions, firmware and web UI.
3. Flash it over USB with the [Firmware Guide](https://github.com/cjkas/ESPSomfy-RTS/wiki/Installing-the-Firmware), connect the device to your network and pair your shades with [Configuring ESPSomfy-RTS](https://github.com/cjkas/ESPSomfy-RTS/wiki/Configuring-the-Software).

Later updates go through the device's web UI with the `SomfyController.ino.<chip>.bin` and `SomfyController.littlefs.<chip>.bin` files, see [Updating ESPSomfy RTS](https://github.com/cjkas/ESPSomfy-RTS/wiki/Updating-ESPSomfy-RTS). ESP32-C5 and ESP32-C6 have no room for a second firmware, so they are always updated over USB.

**Coming from rstrouse's firmware or 3.x?** An update from the old web UI to 4.x fails. Take a backup, flash the onboard image over USB and restore the backup: see [From 3.x to 4.0.0](https://github.com/cjkas/ESPSomfy-RTS/wiki/Installing-the-Firmware#from-3x-to-400-flash-over-usb). Update the Home Assistant integration to 3.0.1 or newer first.

If you have IO Home Control motors this project can't talk to them over the air, but you can wire the device to a dissected IO remote. See [Controlling Motors with GPIO](https://github.com/cjkas/ESPSomfy-RTS/wiki/Controlling-Motors-with-GPIO) to check whether that works for you.

## Supported motors

Most 433 MHz Somfy protocols work: RTS, RTW, RTV/L and 433.92 MHz transceivers. You can mix them on one device as long as they share a base frequency; 433.92 MHz and 433.42 MHz motors can't be on the same device.

Bare motors without a radio module work through a relay module, and you can then pair any RTS, RTW or RTV remote with them.

## Functionality

The device has a built-in web interface, a socket interface and MQTT.

![image](https://user-images.githubusercontent.com/47839015/224559426-c81422c0-cdfe-45f9-a9c2-0c727619cdf9.png)

![image](https://github.com/rstrouse/ESPSomfy-RTS/assets/47839015/2a605e54-4487-49ba-9a7a-07e153db1c02)

* Identify each device by motor type (Awning, Shade, Shutter, Blind, or Drapery)
* Control movement using up, down, and my buttons
* Set and remove my button favorite
* Interactive positioning
* Tilt blinds
* Set blind tilt by percentage
* Control Awning Sun/Wind Sensors

### Moving a shade
Click the up button to move the shade to the full up position and the down button for full down. Press the my button during travel to stop the shade.

Press the my button while the shade is at rest and it moves to its favorite position. The favorite is shown under the shade name.

To move a shade to a percentage, click or tap its icon and drag the slider. The shade starts moving when you release the slider, and you can change the target while it moves.

![image](https://user-images.githubusercontent.com/47839015/224559596-aa98d015-ee74-41f1-a852-3018f861e354.png)

### Setting a favorite
You can set the my position from ESPSomfy RTS or from your Somfy remote: ESPSomfy RTS listens for the long press of the my button. If a favorite was set before you installed ESPSomfy RTS, reset it from ESPSomfy RTS, because Somfy uses the same command to set and unset it.

Long press the my button. After a few seconds the screen below opens. Drag the slider to, say, 37% and press `SET MY POSITION`. The shade moves there and jogs briefly to show the favorite is saved.

![image](https://user-images.githubusercontent.com/47839015/224559730-859d3f9c-177c-46c3-9fb4-1a7df2cac505.png)

To clear it, long press the my button again and move the slider to the current my position. The button turns red and reads `CLEAR MY POSITION`. Press it and the shade jogs to show the favorite is cleared.

![image](https://user-images.githubusercontent.com/47839015/224559837-ad28b843-49ab-468c-8b8c-1ad470775750.png)

## Integrations
* **Home Assistant**: [cjkas/ESPSomfy-RTS-HA](https://github.com/cjkas/ESPSomfy-RTS-HA), 3.0.1 or newer for firmware 4.x. Or use MQTT discovery.
* **MQTT, Node-RED and scripts**: see the [Integrations](https://github.com/cjkas/ESPSomfy-RTS/wiki/Integrations) wiki page.

### API and ports
Since 4.0.0 everything is on port 80. Ports 8081 (HTTP API) and 8080 (events socket) are gone, and so are the HTTP routes the old web UI called.

| What | Where |
|---|---|
| Web UI | `http://<device>/` |
| Commands for scripts and Node-RED | `/shadeCommand`, `/groupCommand` and `/tiltCommand`, with the same fields as before, as query parameters or a JSON body |
| Login | `/loginContext` (the security mode), then `/login`, which returns the token in `apiKey` |
| Backup and restore | `/backup`, `/restore` |
| Firmware and web UI updates | `/updateFirmware`, `/updateApplication` |
| Everything else: state, events, edits, settings | the socket at `ws://<device>/ws`, see [docs/PROTOCOL.md](docs/PROTOCOL.md) |

A script that called port 8081 only changes the port, if it used one of the three commands. Reads such as `/controller`, `/shades` and `/discovery` are gone: use the socket, or MQTT. The Home Assistant integration needs version 3.0.1 or newer.

When the device is Secured (a PIN or password), every route above except the web UI and the login routes needs the token, in an `apikey` header; the socket takes it as `?apikey=<token>`. Without it the reply is 401. "Secure Config Only" is gone: a device set to it is Secured from 4.0.0, so scripts that sent commands without logging in now need the token.

In either mode the socket refuses an upgrade whose `Origin` header doesn't name the host it was sent to, so another site's page can't open it. Behind a reverse proxy, pass the `Host` header through unchanged.

## Building
The firmware builds on [pioarduino](https://github.com/pioarduino/platform-espressif32) (Arduino 3.x on ESP-IDF 5.5), not the stock PlatformIO platform. In VS Code use the **pioarduino IDE** extension instead of PlatformIO IDE. On the command line, `pip install pioarduino` gives you `pio`. Run the unit tests with `pio test -e native`. [ADR 0003](docs/adr/0003-pioarduino-idf55.md) explains why.

Bugs and ideas go to the [issues](https://github.com/cjkas/ESPSomfy-RTS/issues); security problems go through [SECURITY.md](SECURITY.md).

## Credits
* [rstrouse](https://github.com/rstrouse) wrote ESPSomfy RTS, its web UI, the Home Assistant integration and most of the wiki. This repo builds on that work.
* The Somfy RTS protocol timing comes from [pushstack](https://pushstack.wordpress.com/somfy-rts-protocol/).
* The CC1101 is driven by the [SmartRC-CC1101-Driver-Lib](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib).
