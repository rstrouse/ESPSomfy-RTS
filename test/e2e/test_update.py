"""A firmware update through /updateFirmware, as the web UI sends it.

Pass --firmware with a firmware.bin built for the device's chip, e.g.
.pio/build/esp32s3/firmware.bin.  The device must have an OTA slot: the
release builds do, the C5, C6 and debug builds don't.

It can't tell the new firmware from the old when both have the same
version, and it doesn't make the update fail.  test/e2e/README.md has the
rollback check by hand.
"""
import time

import pytest

from proto_client import run_sync
from test_routes import auth, upload
from test_settings import boot_time, wait_for_restart

# The new firmware is marked good once it has run this long with the network
# up; a reset before then boots the old one (docs/adr/0003-pioarduino-idf55.md).
PROVEN_AFTER_S = 60


@pytest.fixture
def firmware(request):
    path = request.config.getoption("--firmware")
    if not path:
        pytest.skip("updates the device's firmware: pass --firmware <firmware.bin>")
    with open(path, "rb") as f:
        return f.read()


def test_an_update_restarts_on_the_new_firmware_and_stays(host, token, connect, schema, firmware):
    c = connect(schema=schema.hash)
    _, frames, _ = run_sync(c)
    booted = boot_time(schema, frames)
    c.close()

    status, _, body = upload(host, "/updateFirmware", auth(token), file=firmware)
    assert status == 200, body
    wait_for_restart(host, token, schema, booted)

    # Past the point where it is marked good it is still running, and hasn't
    # restarted on the old firmware.
    c = connect(schema=schema.hash)
    _, frames, _ = run_sync(c)
    restarted = boot_time(schema, frames)
    c.close()
    time.sleep(PROVEN_AFTER_S + 10)
    c = connect(schema=schema.hash)
    _, frames, _ = run_sync(c)
    assert abs(boot_time(schema, frames) - restarted) < 5, "the device restarted again"
