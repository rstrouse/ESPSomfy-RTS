"""hello, the schema and the sync (docs/PROTOCOL.md)."""
import json
import os

from proto_client import Schema, run_sync

FIXTURES = os.path.join(os.path.dirname(__file__), "..", "fixtures", "proto2")


def items(frames, schema, section):
    key = schema.keys[section]
    out = []
    for f in frames:
        v = f["d"].get(key)
        if isinstance(v, list):
            out.extend(v)
        elif v is not None:
            out.append(v)
    return out


def test_hello_names_the_firmware_and_schema(connect):
    c = connect()
    assert c.status == 101
    hello = c.recv()
    assert hello["v"] == 2
    assert hello["t"] == 1
    assert hello["d"]["fw"]
    assert len(hello["d"]["sh"]) == 8


def test_first_sync_carries_the_schema(connect):
    c = connect()
    schema = Schema()
    hello, frames, _ = run_sync(c, schema)
    assert schema.complete()
    assert schema.type_numbers["hello"] == 1
    assert schema.keys["shadeId"] == "s"
    for f in frames:
        assert len(json.dumps(f, separators=(",", ":"))) < 4096


def test_a_client_with_the_schema_gets_none(connect, schema):
    c = connect(schema=schema.hash)
    _, frames, _ = run_sync(c)
    for f in frames:
        for k in ("T", "F", "E", "Q"):
            assert k not in f["d"]


def test_an_old_schema_hash_gets_the_schema_again(connect):
    c = connect(schema="00000000")
    schema = Schema()
    run_sync(c, schema)
    assert schema.complete()


def test_sync_is_whole(connect, schema):
    """Every group links shades the sync has, every item's room is in it, and
    there is one header and one status."""
    c = connect(schema=schema.hash)
    _, frames, _ = run_sync(c)
    shades = [schema.expand(s) for s in items(frames, schema, "shades")]
    groups = [schema.expand(g) for g in items(frames, schema, "groups")]
    rooms = [schema.expand(r) for r in items(frames, schema, "rooms")]
    shade_ids = [s["shadeId"] for s in shades]
    room_ids = {r["roomId"] for r in rooms} | {0}
    assert len(shade_ids) == len(set(shade_ids))
    for g in groups:
        assert set(g.get("linkedShades", [])) <= set(shade_ids), g["groupId"]
    for item in shades + groups:
        assert item.get("roomId", 0) in room_ids
    assert len(items(frames, schema, "header")) == 1
    assert len(items(frames, schema, "status")) == 1


def test_sync_waits_for_next(connect):
    c = connect()
    c.recv()  # hello
    first = c.recv()
    assert first["t"] == 4
    # Without next no more sync comes, unless the whole sync fit one frame.
    # Events may come meanwhile, such as the 30 s memory status.
    while True:
        try:
            more = c.recv(timeout=2)
        except TimeoutError:
            return
        if more["t"] >= 128:
            continue
        assert more["t"] == 5, "a second sync frame came without next"
        return


def test_resync_sends_it_all_again(connect, schema):
    c = connect(schema=schema.hash)
    _, first, _ = run_sync(c)
    c.send(schema.type_numbers["sync"])
    _, again, _ = run_sync(c, hello=False)
    assert len(items(again, schema, "shades")) == len(items(first, schema, "shades"))
    for f in again:
        assert "T" not in f["d"]


# What a recording must not keep from a real installation: the remote
# addresses and rolling codes would let anyone forge frames for its shades.
SCRUBBED = {"remoteAddress", "lastRollingCode", "sourceAddress", "startingAddress", "repeaters",
            "name", "serverId", "hostname", "ssid"}


def scrub(value, schema, counter):
    """Fake values of the same kind in place of anything identifying."""
    if isinstance(value, list):
        return [scrub(v, schema, counter) for v in value]
    if not isinstance(value, dict):
        return value
    out = {}
    for key, v in value.items():
        name = schema.fields.get(key, (key,))[0]
        if name in SCRUBBED:
            counter[0] += 1
            if isinstance(v, list):
                v = [0x100000 + counter[0] * 16 + i for i in range(len(v))]
            elif isinstance(v, str):
                v = "%s %u" % (name, counter[0])
            else:
                v = 0x100000 + counter[0]
        out[key] = scrub(v, schema, counter)
    return out


def test_record_fixtures(connect, recording):
    """With --record, saves hello and a whole first sync for the other suites,
    with the installation's identifying values replaced."""
    if not recording:
        return
    c = connect()
    schema = Schema()
    run_sync(c, schema)
    os.makedirs(FIXTURES, exist_ok=True)
    hello = json.loads(c.raw[0])
    with open(os.path.join(FIXTURES, "hello.json"), "w", encoding="utf-8") as f:
        json.dump(hello, f, indent=1)
    counter = [0]
    frames = [json.loads(r) for r in c.raw[1:]]
    for frame in frames:
        frame["d"] = scrub(frame["d"], schema, counter)
    with open(os.path.join(FIXTURES, "sync.json"), "w", encoding="utf-8") as f:
        json.dump(frames, f, indent=1)
    expanded = {
        "hash": hello["d"]["sh"],
        "types": {str(k): v for k, v in sorted(schema.types.items())},
        "fields": {k: {"name": n, "kind": kind, "default": d} for k, (n, kind, d) in sorted(schema.fields.items())},
        "errors": {str(k): v for k, v in sorted(schema.errors.items())},
        "feeds": {str(k): v for k, v in sorted(schema.feeds.items())},
    }
    with open(os.path.join(FIXTURES, "schema.json"), "w", encoding="utf-8") as f:
        json.dump(expanded, f, indent=1)
