"""The command ops (docs/PROTOCOL.md).  Without --radio nothing goes on air:
the ops are refused before they reach the radio, or send nothing anyway."""
import time

import pytest

from proto_client import run_sync
from test_messages import error_name, reply_to

COMMAND_OPS = ("shadeCommand", "groupCommand", "tiltCommand", "repeatCommand", "setPositions", "setSensor",
               "setMyPosition", "unpairShade", "setRollingCode", "sendRemoteCommand")


def pack(schema, fields):
    """A payload by field names, in short keys."""
    return {schema.keys[name]: value for name, value in fields.items()}


def synced_model(connect, schema):
    """A synced client and the shades and groups it got, by name."""
    c = connect(schema=schema.hash)
    _, frames, _ = run_sync(c)
    shades, groups = [], []
    for f in frames:
        shades.extend(schema.expand(s) for s in f["d"].get(schema.keys["shades"], []))
        groups.extend(schema.expand(g) for g in f["d"].get(schema.keys["groups"], []))
    return c, shades, groups


def op(c, schema, name, fields):
    """Sends the op and returns its reply."""
    msg_id = c.send(schema.type_numbers[name], pack(schema, fields), with_id=True)
    return reply_to(c, msg_id)


def is_ok(schema, reply):
    return reply["d"].get(schema.keys["ok"]) == 1


def test_every_command_op_is_in_the_schema(schema):
    for name in COMMAND_OPS:
        assert 16 <= schema.type_numbers[name] <= 127, name


def test_an_op_without_an_id_is_a_bad_request(connect, schema):
    c, _, _ = synced_model(connect, schema)
    c.send(schema.type_numbers["shadeCommand"], pack(schema, {"shadeId": 1, "command": "My"}))
    reply = c.recv_until(lambda f: f["t"] == 2)[-1]
    assert "id" not in reply
    assert error_name(schema, reply) == "badRequest"


@pytest.mark.parametrize("name, fields, why", [
    ("shadeCommand", {}, "shadeId is missing"),
    ("shadeCommand", {"shadeId": 1, "command": "Upp"}, "Not a command"),
    ("shadeCommand", {"shadeId": 1}, "Give command or target"),
    ("shadeCommand", {"shadeId": 1, "target": 101}, "target must be 0-100"),
    ("tiltCommand", {"shadeId": 1, "command": "Up", "target": 5}, "Give command or target"),
    ("groupCommand", {"groupId": 1}, "command is missing"),
    ("repeatCommand", {"command": "Up"}, "Give shadeId or groupId"),
    ("setPositions", {"shadeId": 1}, "Give position, tiltPosition or both"),
    ("setSensor", {"shadeId": 1}, "Give sunny, windy or both"),
    ("setMyPosition", {"shadeId": 1, "myPos": 101}, "myPos must be 0-100"),
    ("unpairShade", {}, "shadeId is missing"),
    ("setRollingCode", {"shadeId": 1}, "lastRollingCode is missing"),
    ("sendRemoteCommand", {"command": "Up", "lastRollingCode": 1}, "remoteAddress is missing"),
])
def test_a_bad_payload_is_refused_with_why(connect, schema, name, fields, why):
    c, _, _ = synced_model(connect, schema)
    reply = op(c, schema, name, fields)
    assert error_name(schema, reply) == "badRequest"
    assert reply["d"][schema.keys["desc"]] == why


def test_a_shade_or_group_that_does_not_exist_is_not_found(connect, schema):
    c, shades, groups = synced_model(connect, schema)
    shade = next(i for i in range(254, 0, -1) if i not in {s["shadeId"] for s in shades})
    group = next(i for i in range(254, 0, -1) if i not in {g["groupId"] for g in groups})
    reply = op(c, schema, "shadeCommand", {"shadeId": shade, "command": "Up"})
    assert error_name(schema, reply) == "notFound"
    assert reply["d"][schema.keys["desc"]] == "No shade with that id"
    reply = op(c, schema, "groupCommand", {"groupId": group, "command": "Up"})
    assert error_name(schema, reply) == "notFound"
    assert reply["d"][schema.keys["desc"]] == "No group with that id"


def test_set_positions_where_the_shade_already_is(connect, schema):
    """setPositions sends nothing on air.  Setting the position the sync gave
    moves the shade's believed position by less than 1%, its fraction."""
    c, shades, _ = synced_model(connect, schema)
    if not shades:
        pytest.skip("the device has no shades")
    shade = shades[0]
    reply = op(c, schema, "setPositions", {"shadeId": shade["shadeId"], "position": shade.get("position", 0)})
    assert is_ok(schema, reply)


def test_a_command_moves_the_shade_and_replies_ok(connect, schema, radio_shade):
    """Moves the --shade a little and back to where it was."""
    c, shades, _ = synced_model(connect, schema)
    shade = next((s for s in shades if s["shadeId"] == radio_shade), None)
    assert shade, "no shade %s on the device" % radio_shade
    start = shade.get("position", 0)
    reply = op(c, schema, "shadeCommand", {"shadeId": radio_shade, "command": "Down" if start < 50 else "Up"})
    assert is_ok(schema, reply)
    state = schema.type_numbers["shadeState"]
    moving = c.recv_until(lambda f: f["t"] == state and f["d"].get(schema.keys["shadeId"]) == radio_shade
                          and f["d"].get(schema.keys["direction"], 0) != 0, timeout=10)[-1]
    assert moving
    time.sleep(2)
    reply = op(c, schema, "shadeCommand", {"shadeId": radio_shade, "target": start})
    assert is_ok(schema, reply)
