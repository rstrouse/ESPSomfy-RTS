"""/restore and the options the web UI sends with the backup file.

The round trip restores a backup and reboots the device: pass --reboot.
"""
import json

import pytest

from proto_client import run_sync
from test_commands import is_ok, op, synced_model
from test_routes import auth, http, restore_without_a_file, upload
from test_settings import boot_time, desc, settings, wait_for_restart

# Saved for the round trip and put back by the restore.  .invalid never resolves.
CHANGED_NTP_SERVER = "restore.e2e.invalid"


def test_restore_reads_the_options_sent_as_a_form_field(host, token):
    """The options come as the form field "data", next to the file.

    They are read before the file is checked, so options that aren't JSON
    are refused, and this file, which isn't a backup, restores nothing
    either way.  /restore once looked up only query parameters, missed the
    field and restored the shades alone, whatever was ticked.
    """
    status, _, body = upload(host, "/restore", auth(token), fields={"data": "{"})
    assert status == 500, body
    assert json.loads(body)["desc"] == "JSON parse error"


def test_a_restore_with_no_part_chosen_is_refused(host, token):
    """Checked before the file, so this file, which isn't a backup, restores nothing either way."""
    options = json.dumps({"shades": False})
    status, _, body = upload(host, "/restore", auth(token), fields={"data": options})
    assert status == 400, body
    assert json.loads(body)["desc"] == "No part of the backup was chosen, so nothing was restored."


def test_a_refused_restore_leaves_no_upload_behind(host, token):
    """A later /restore without a file must not find the refused one's upload."""
    status, _, _ = upload(host, "/restore", auth(token), fields={"data": "{"})
    assert status == 500
    status, reason = restore_without_a_file(host, token)
    assert status == 400
    assert "didn't complete" in reason, reason


def test_a_restore_brings_back_the_parts_that_were_ticked(request, host, token, connect, schema):
    if not request.config.getoption("--reboot"):
        pytest.skip("restores a backup and reboots the device: pass --reboot")
    c = connect(schema=schema.hash)
    _, frames, _ = run_sync(c)
    booted = boot_time(schema, frames)
    general = settings(c, schema, "general")
    ntp = {name: general.get(name, schema.default(name)) for name in ("ntpServer", "posixZone")}
    assert ntp["ntpServer"] != CHANGED_NTP_SERVER
    status, _, backup = http(host, "/backup", headers=auth(token))
    assert status == 200

    reply = op(c, schema, "saveSettings", dict(ntp, ntpServer=CHANGED_NTP_SERVER, section="general"))
    assert is_ok(schema, reply), desc(schema, reply)
    try:
        options = json.dumps({"shades": False, "settings": True})
        status, _, body = upload(host, "/restore", auth(token), file=backup, fields={"data": options})
        assert status == 200, body
        wait_for_restart(host, token, schema, booted)
    finally:
        c, _, _ = synced_model(connect, schema)
        restored = settings(c, schema, "general").get("ntpServer", schema.default("ntpServer"))
        if restored != ntp["ntpServer"]:
            op(c, schema, "saveSettings", dict(ntp, section="general"))
    assert restored == ntp["ntpServer"]


def test_a_restore_of_the_radio_alone_brings_it_back_as_it_was(request, host, token, connect, schema):
    """4.0 boards without Ethernet declared their network record 28 bytes
    longer than they wrote, so a restore that skipped it read the radio from
    the middle of its record."""
    if not request.config.getoption("--reboot"):
        pytest.skip("restores a backup and reboots the device: pass --reboot")
    c = connect(schema=schema.hash)
    _, frames, _ = run_sync(c)
    booted = boot_time(schema, frames)
    radio = settings(c, schema, "radio")
    status, _, backup = http(host, "/backup", headers=auth(token))
    assert status == 200

    options = json.dumps({"shades": False, "transceiver": True})
    status, _, body = upload(host, "/restore", auth(token), file=backup, fields={"data": options})
    assert status == 200, body
    wait_for_restart(host, token, schema, booted)
    c, _, _ = synced_model(connect, schema)
    assert settings(c, schema, "radio") == radio


def test_a_restore_of_the_shades_brings_them_back_as_they_were(request, host, token, connect, schema):
    """Every shade and group, their rolling codes included, from a backup taken a moment before."""
    if not request.config.getoption("--reboot"):
        pytest.skip("restores a backup and reboots the device: pass --reboot")
    _, shades, groups = synced_model(connect, schema)
    _, frames, _ = run_sync(connect(schema=schema.hash))
    booted = boot_time(schema, frames)
    status, _, backup = http(host, "/backup", headers=auth(token))
    assert status == 200

    options = json.dumps({"shades": True})
    status, _, body = upload(host, "/restore", auth(token), file=backup, fields={"data": options})
    assert status == 200, body
    wait_for_restart(host, token, schema, booted)
    _, shades_after, groups_after = synced_model(connect, schema)
    assert shades_after == shades
    assert groups_after == groups
