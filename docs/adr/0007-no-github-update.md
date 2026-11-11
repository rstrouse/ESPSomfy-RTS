---
status: accepted
---

# The firmware no longer updates itself from GitHub

The device fetched GitHub's release list, downloaded firmware and filesystem images, and checked daily for a newer release, all over HTTPS on the device itself. A TLS session needs one contiguous block of about 35–40 KB. On IDF 5.5 the heap's largest free block is often smaller (issue #76: "SSL - Memory allocation failed" with a 34.8 KB largest block). The download checked no certificate (`setInsecure()`), so anyone who could intercept it could serve their own firmware. Parts of it ran blocking on the web server's task, and its socket events from there were dropped. The web UI's update buttons sent PUT to routes that accept only GET, so they had been answering 404.

We removed it: the release check and its setting, the release list, download and cancel, `/recoverFilesystem` and the `updateProgress` event. Updates are done by uploading `firmware.bin` and `littlefs.bin` (`/updateFirmware`, `/updateApplication`), or over USB.

## Consequences

- Users download releases from GitHub themselves. The web UI's firmware page shows the installed version and the upload form.
- Home Assistant's update entity can no longer install. From integration 3.0.4 Home Assistant checks GitHub for the latest release itself, and the entity shows when a newer one is out (cjkas/ESPSomfy-RTS-HA#22).
- A broken filesystem is recovered by uploading `littlefs.bin`, or over USB, rather than with `/recoverFilesystem`.
- Issue #76 and PR #72 (GitOTA's per-chip littlefs file name) become obsolete.
- The HTTPS client leaves the firmware, which frees flash in the 1.5 MB OTA slot.
