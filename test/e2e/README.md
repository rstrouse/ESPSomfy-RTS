# e2e suite

Runs against a real device on your network, by hand, never in CI. It checks socket protocol 2 ([PROTOCOL.md](../../docs/PROTOCOL.md)) and that the device survives the loads that used to run its heap out.

```sh
~/.platformio/penv/Scripts/uv run --no-project --with pytest -- pytest test/e2e --device 192.168.1.120
```

| Option | Use |
|---|---|
| `--device` | the device's address (required) |
| `--password`, `--username` / `--pin` | the credentials of a Secured device |
| `--radio --shade <id>` | also send real commands: that shade moves a little and back |
| `--edit` | also add a room and delete it again |
| `--reboot` | also reboot the device and wait for it to come back, and restore the settings from a backup taken a moment before (it changes the NTP server, which the restore puts back), and restore the radio settings alone |
| `--firmware <firmware.bin>` | also update the device to that firmware, built for its chip, and check it is still running a minute later, after it marks the update good. Needs a build with an OTA slot |
| `--lockout` | on a Secured device, also fail `/login` until this PC is refused, then wait out the 30 s lockout |
| `--record` | save hello, the sync and the schema to `test/fixtures/proto2/`, for the web UI and Home Assistant tests |

A plain run sends nothing on air, changes no setting (it saves only values the device already has) and doesn't reboot the device. The command tests send ops that are refused before they reach the radio, and one `setPositions` that sets a shade to the position the sync gave: that drops the fraction of a percent the shade's believed position may have had. The edit tests send edits that are refused, and one shade sort order set to the order it has, which saves the shades again unchanged. The settings tests send saves that are refused, and one that saves the NTP server and time zone the device already has. The restore test sends options that aren't JSON with a file that isn't a backup, so nothing is restored. A firmware upload goes only to a device without an OTA slot, which refuses it. The frequency scan test pauses the radio's sending for a second or two, as a scan does. It does fill every socket slot for a few seconds, and replays a page load.

The web UI holds one of the device's 5 socket slots for each open tab, and Home Assistant holds one. The tests that fill every slot fill the ones left, so close the UI's tabs for the full load.

A rollback is checked by hand, on a release build with an OTA slot:

1. Note the version the web UI shows.
2. Build firmware that restarts before it is marked good: add `abort();` at the end of `setup()` in `src/SomfyController.cpp`, and change `data-src/appversion` so the two versions differ.
3. Update to it through the web UI, with the serial monitor open.
4. The crash restarts the device, and the bootloader boots the old firmware ([ADR 0003](../../docs/adr/0003-pioarduino-idf55.md)). The web UI shows the version from step 1 again.

The tests run in whatever security mode the device is in; the suite never changes it. To test both modes, run the suite once in each.
