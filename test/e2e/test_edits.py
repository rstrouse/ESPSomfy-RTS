"""The edit ops and the reads an edit form needs (docs/PROTOCOL.md).  Without
--edit nothing changes: edits are refused, or set what is already there."""
import pytest

from test_commands import is_ok, op, pack, synced_model
from test_messages import error_name, reply_to

EDIT_OPS = ("addRoom", "addShade", "addGroup", "saveRoom", "saveShade", "saveGroup", "deleteRoom", "deleteShade",
            "deleteGroup", "linkRemote", "unlinkRemote", "linkToGroup", "unlinkFromGroup", "linkRepeater",
            "unlinkRepeater", "setPaired", "roomSortOrder", "shadeSortOrder", "groupSortOrder")
READ_OPS = ("getNextRoom", "getNextShade", "getNextGroup", "groupOptions")


def field(schema, reply, name):
    return reply["d"].get(schema.keys[name], schema.default(name))


def unused(items, key):
    return next(i for i in range(250, 0, -1) if i not in {x[key] for x in items})


def test_every_edit_and_read_op_is_in_the_schema(schema):
    for name in EDIT_OPS + READ_OPS:
        assert 16 <= schema.type_numbers[name] <= 127, name


def test_the_next_shade_gets_a_free_id_and_address(connect, schema):
    c, shades, groups = synced_model(connect, schema)
    reply = op(c, schema, "getNextShade", {})
    assert is_ok(schema, reply)
    assert field(schema, reply, "shadeId") not in {s["shadeId"] for s in shades}
    address = field(schema, reply, "remoteAddress")
    assert address and address not in {s.get("remoteAddress") for s in shades + groups}


def test_the_next_room_and_group(connect, schema):
    c, _, groups = synced_model(connect, schema)
    assert is_ok(schema, op(c, schema, "getNextRoom", {}))
    reply = op(c, schema, "getNextGroup", {})
    assert is_ok(schema, reply)
    assert field(schema, reply, "groupId") not in {g["groupId"] for g in groups}


def test_group_options_are_the_shades_it_lacks(connect, schema):
    c, shades, groups = synced_model(connect, schema)
    if not groups:
        pytest.skip("the device has no groups")
    group = groups[0]
    reply = op(c, schema, "groupOptions", {"groupId": group["groupId"]})
    assert is_ok(schema, reply)
    expected = {s["shadeId"] for s in shades} - set(group.get("linkedShades", []))
    assert set(field(schema, reply, "ids")) == expected


def test_group_options_of_a_missing_group(connect, schema):
    c, _, groups = synced_model(connect, schema)
    reply = op(c, schema, "groupOptions", {"groupId": unused(groups, "groupId")})
    assert error_name(schema, reply) == "notFound"


@pytest.mark.parametrize("name, payload, why", [
    ("saveRoom", {"zz": 1}, "Unknown field zz"),
    ("setPaired", {"s": 1, "i": 2}, "paired is not a boolean (1 or 0)"),
    ("shadeSortOrder", {}, "ids is missing"),
    ("setPaired", {"s": 1}, "paired is missing"),
    ("saveGroup", {"g": 1, "ss": 1}, "sunSensor can't be set by this edit"),
])
def test_a_bad_payload_is_refused_before_it_is_queued(connect, schema, name, payload, why):
    c, _, _ = synced_model(connect, schema)
    msg_id = c.send(schema.type_numbers[name], payload, with_id=True)
    reply = reply_to(c, msg_id)
    assert error_name(schema, reply) == "badRequest"
    assert reply["d"][schema.keys["desc"]] == why


def test_an_edit_of_a_missing_shade_says_why(connect, schema):
    """Refused by the edit itself on loop(), as its route would be."""
    c, shades, _ = synced_model(connect, schema)
    reply = op(c, schema, "saveShade", {"shadeId": unused(shades, "shadeId"), "name": "x"})
    assert error_name(schema, reply) == "notFound"
    assert reply["d"][schema.keys["desc"]] == "Shade Id not found."


def test_a_field_an_edit_doesnt_take_is_refused(connect, schema):
    c, shades, _ = synced_model(connect, schema)
    reply = op(c, schema, "saveShade", {"shadeId": unused(shades, "shadeId"), "paired": 1})
    assert error_name(schema, reply) == "badRequest"
    assert reply["d"][schema.keys["desc"]] == "paired can't be set by this edit"


def test_a_sort_order_set_to_what_it_is(connect, schema):
    c, shades, _ = synced_model(connect, schema)
    if not shades:
        pytest.skip("the device has no shades")
    ordered = sorted(shades, key=lambda s: s.get("sortOrder", 0))
    # A sort order numbers the shades 0, 1, 2...: only a list already numbered
    # so stays as it is.
    if [s.get("sortOrder", 0) for s in ordered] != list(range(len(ordered))):
        pytest.skip("the shades' sort orders have gaps or repeats, which a sort order would renumber")
    ids = [s["shadeId"] for s in ordered]
    reply = op(c, schema, "shadeSortOrder", {"ids": ids})
    assert is_ok(schema, reply)
    order = c.recv_until(lambda f: f["t"] == schema.type_numbers["shadeOrder"])[-1]
    assert order["d"][schema.keys["ids"]] == ids


def test_a_long_edit_is_put_back_together(connect, schema):
    """3 KB arrives in pieces: the reply carries its id, so it was read whole."""
    c, shades, _ = synced_model(connect, schema)
    padding = " " * 3000
    msg_id = 4242
    c.send_text('{"v":2,"t":%d,"id":%d,"d":{"%s":%d%s}}' % (
        schema.type_numbers["saveShade"], msg_id, schema.keys["shadeId"], unused(shades, "shadeId"), padding))
    reply = reply_to(c, msg_id)
    assert reply["d"][schema.keys["desc"]] == "Shade Id not found."


def test_adding_and_deleting_a_room(connect, schema, editing):
    """With --edit: a room is added, then deleted again."""
    c, _, _ = synced_model(connect, schema)
    reply = op(c, schema, "addRoom", {"name": "e2e room"})
    assert is_ok(schema, reply), reply
    room_id = field(schema, reply, "roomId")
    assert room_id
    added = c.recv_until(lambda f: f["t"] == schema.type_numbers["room"])[-1]
    assert schema.expand(added["d"])["name"] == "e2e room"
    assert is_ok(schema, op(c, schema, "deleteRoom", {"roomId": room_id}))
    removed = c.recv_until(lambda f: f["t"] == schema.type_numbers["roomRemoved"])[-1]
    assert removed["d"][schema.keys["roomId"]] == room_id
