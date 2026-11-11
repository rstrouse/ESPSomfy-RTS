"""The settings ops, the WiFi and frequency scans, and reboot (docs/PROTOCOL.md).
Without --reboot nothing changes: saves are refused, or save what is already
there, and nothing goes on air."""
import time

import pytest

from proto_client import ProtoClient, run_sync
from test_commands import is_ok, op, synced_model
from test_messages import error_name

SETTINGS_OPS = ("getSettings", "saveSettings", "scanWifi", "beginFrequencyScan", "endFrequencyScan", "reboot")
SECTIONS = ("general", "network", "mqtt", "security", "radio")
SECRETS = ("password", "passphrase", "pin")


def desc(schema, reply):
    return reply["d"].get(schema.keys["desc"])


def settings(c, schema, section):
    reply = op(c, schema, "getSettings", {"section": section})
    assert is_ok(schema, reply), desc(schema, reply)
    return schema.expand(reply["d"])


def test_every_settings_op_is_in_the_schema(schema):
    for name in SETTINGS_OPS:
        assert 16 <= schema.type_numbers[name] <= 127, name
    for name in ("settingsChanged", "wifiNetworks"):
        assert schema.type_numbers[name] >= 128, name


@pytest.mark.parametrize("section", SECTIONS)
def test_every_section_reads_without_its_secrets(connect, schema, section):
    c, _, _ = synced_model(connect, schema)
    got = settings(c, schema, section)
    for name in SECRETS:
        assert name not in got, name


def test_the_sections_hold_what_the_routes_send(connect, schema):
    c, _, _ = synced_model(connect, schema)
    assert settings(c, schema, "general").get("hostname")
    assert settings(c, schema, "radio").get("bitLength", schema.default("bitLength")) in (56, 80)
    assert settings(c, schema, "mqtt").get("port", 0) > 0


def test_an_unknown_section_is_a_bad_request(connect, schema):
    c, _, _ = synced_model(connect, schema)
    reply = op(c, schema, "getSettings", {"section": "wifi"})
    assert error_name(schema, reply) == "badRequest"
    assert desc(schema, reply) == "Unknown section"


@pytest.mark.parametrize("section, fields, why", [
    ("mqtt", {}, "Nothing to save"),
    ("mqtt", {"hasPassword": 1}, "hasPassword can't be saved in mqtt"),
    ("general", {"ssid": "x"}, "ssid can't be saved in general"),
    ("network", {"connMode": 9}, "connMode must be 1-3"),
    ("network", {"ip": "10.0.0"}, "ip is not an IP address"),
    ("radio", {"bitLength": 60}, "bitLength must be 56 or 80"),
    ("security", {"pin": "12345"}, "pin is longer than 4 characters"),
])
def test_a_save_is_checked_before_it_runs(connect, schema, section, fields, why):
    c, _, _ = synced_model(connect, schema)
    reply = op(c, schema, "saveSettings", dict(fields, section=section))
    assert error_name(schema, reply) == "badRequest"
    assert desc(schema, reply) == why


def test_a_save_tells_the_other_clients(connect, schema):
    c, _, _ = synced_model(connect, schema)
    other, _, _ = synced_model(connect, schema)
    general = settings(c, schema, "general")
    # What is there already, so nothing changes.
    same = {name: general.get(name, schema.default(name)) for name in ("ntpServer", "posixZone")}
    reply = op(c, schema, "saveSettings", dict(same, section="general"))
    assert is_ok(schema, reply), desc(schema, reply)
    assert not schema.expand(reply["d"]).get("rebooting")
    changed = other.recv_until(lambda f: f["t"] == schema.type_numbers["settingsChanged"])[-1]
    assert schema.expand(changed["d"])["section"] == "general"
    assert settings(c, schema, "general") == general


def test_a_wifi_scan_lists_the_networks(connect, schema):
    c = connect(schema=schema.hash)
    _, frames, _ = run_sync(c)
    # The connection in use: Ethernet with WiFi as the fallback may be on either.
    if header(schema, frames).get("connType") != "Wifi":
        pytest.skip("the device isn't on WiFi")
    assert is_ok(schema, op(c, schema, "scanWifi", {}))
    found = c.recv_until(lambda f: f["t"] == schema.type_numbers["wifiNetworks"], timeout=20)[-1]
    networks = schema.expand(found["d"]).get("networks", [])
    assert networks, "no networks, not even the one the device is on"
    for n in networks:
        assert n["ssid"] and n["macAddress"]


def test_a_frequency_scan_starts_and_ends(connect, schema):
    c, _, _ = synced_model(connect, schema)
    scan = schema.type_numbers["frequencyScan"]
    assert is_ok(schema, op(c, schema, "subscribe", {"feed": schema.feed_numbers["frequencyScan"]}))
    reply = op(c, schema, "beginFrequencyScan", {})
    if not is_ok(schema, reply):
        assert error_name(schema, reply) == "radioUnavailable"
        pytest.skip("the radio is disabled")
    try:
        started = c.recv_until(lambda f: f["t"] == scan, timeout=5)[-1]
        assert schema.expand(started["d"]).get("scanning")
    finally:
        assert is_ok(schema, op(c, schema, "endFrequencyScan", {}))
    ended = c.recv_until(lambda f: f["t"] == scan and not schema.expand(f["d"]).get("scanning"), timeout=5)[-1]
    assert ended


def test_reboot(request, host, token, connect, schema):
    if not request.config.getoption("--reboot"):
        pytest.skip("reboots the device: pass --reboot")
    c = connect(schema=schema.hash)
    _, frames, _ = run_sync(c)
    booted = boot_time(schema, frames)
    assert is_ok(schema, op(c, schema, "reboot", {}))
    assert c.closed.wait(15), "the socket stayed open"
    wait_for_restart(host, token, schema, booted)


def wait_for_restart(host, token, schema, booted):
    """Waits until the device has restarted since it booted at booted (boot_time)."""
    # The device closes its sockets just before it restarts, and may still
    # take a connection in between: that one never gets its hello.
    time.sleep(3)
    end = time.time() + 90
    while True:
        assert time.time() < end, "the device didn't restart"
        try:
            back = ProtoClient(host, params={"apikey": token} if token else None, timeout=3)
        except OSError:
            time.sleep(2)
            continue
        try:
            if back.status == 101:
                hello, frames, _ = run_sync(back, timeout=10)
                assert hello["d"]["sh"] == schema.hash
                # Compared as boot times: a device that restarted a moment
                # ago may have had less uptime than it has now.
                if boot_time(schema, frames) > booted + 2:
                    return
        except TimeoutError:
            pass  # not back yet
        finally:
            back.close()
        time.sleep(2)


def boot_time(schema, frames):
    """When the device booted, as a time.time(), from the status a sync ends with."""
    return time.time() - uptime(schema, frames) / 1000


def header(schema, frames):
    """The header a sync starts with."""
    key = schema.keys["header"]
    return next(schema.expand(f["d"][key]) for f in frames if key in f["d"])


def uptime(schema, frames):
    """The uptime, in ms, from the status a sync ends with."""
    key = schema.keys["status"]
    status = next(f["d"][key] for f in frames if key in f["d"])
    return schema.expand(status)["uptime"]
