## Testing instructions
- Fix any test or type errors until the whole suite is green.
- Add or update tests for the code you change, even if nobody asked.
- to test run `pio test -e native`
- the web UI's protocol 2 client (`data-src/js/`): `node --test "test/js/*.test.js"`. The minify step joins those files into `index.js`, so each one ends with a `module.exports` footer for Node and adds nothing global but its classes.
- the release scripts in `.github/` (`set_version.sh`): `bash test/scripts/test_set_version.sh`, which CI runs with the native tests.
- coverage of the native tests: `pio test -e coverage`, then `gcovr` (`pip install gcovr`), which reads `gcovr.cfg` and writes `coverage/coverage_details.html`. Files the native env doesn't build aren't in it. CI publishes the coverage of `main` to the repo's GitHub Pages.
- coverage of the web UI tests: add `--experimental-test-coverage --test-coverage-include="data-src/js/**"` to the `node --test` command.

## Building
- Firmware builds need pioarduino's Core (`pip install pioarduino`, or the pioarduino IDE extension). Stock PlatformIO Core refuses the pinned platform, see [ADR 0003](docs/adr/0003-pioarduino-idf55.md).
- The platform installs its Python packages into `~/.platformio/penv` and fails on Windows while `pio.exe` from that env is running. Run builds as `python -m platformio` from that env's Python.
