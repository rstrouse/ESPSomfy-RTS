# Each function opens NVS with its own Preferences handle, with no lock or wrapper

A single global `Preferences pref` was used from both `loop()` and the async web server task. A `begin()` from one task could fail while the other task had a namespace open, and its `clear()` or `remove()` then hit the wrong namespace (a radio settings save could erase every rolling code). We replaced the global with a local `Preferences` in each function that uses NVS. We did not put an app-level lock or a wrapper module around it, because ESP-IDF's NVS API already serializes access internally (`nvs::Lock`), each `Preferences` holds its own handle, and its destructor calls `end()`.

## Considered Options

- **Global handle plus a mutex-backed `NvsSession`.** Rejected: the lock would only protect the shared handle, which is the actual bug, and it would block `loop()`'s rolling-code writes while `ConfigFile` keeps `ShadeCodes` open across LittleFS reads. Remove the shared handle and nothing is left for the lock to do.
- **An `NvsStore` seam with typed accessors and an in-memory adapter for native tests.** Rejected: it would have exactly one real adapter, so the seam would be hypothetical, and deleting it would just give back `Preferences`. The only NVS logic worth testing (the next rolling code) is already tested natively through `RollingCode.h`, which takes its loader as a parameter.

## Consequences

Separate handles make each NVS operation safe on its own, but not a read-then-write sequence across tasks (rolling-code peek/commit in `loop()` against `/setRollingCode` or a restore on the web server task). Those races belong to the model, not to NVS, and are tracked in `docs/KNOWN_ISSUES.md`.
