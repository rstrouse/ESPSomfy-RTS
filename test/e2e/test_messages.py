"""What the device answers to client frames: errors and feed subscriptions."""
from proto_client import run_sync


def synced(connect, schema):
    c = connect(schema=schema.hash)
    run_sync(c)
    return c


def reply_to(c, msg_id, timeout=5):
    return c.recv_until(lambda f: f["t"] == 2 and f.get("id") == msg_id, timeout)[-1]


def error_name(schema, reply):
    return schema.errors[reply["d"][schema.keys["code"]]]


def test_malformed_json_is_a_bad_request(connect, schema):
    c = synced(connect, schema)
    c.send_text("not json")
    reply = c.recv_until(lambda f: f["t"] == 2)[-1]
    assert error_name(schema, reply) == "badRequest"


def test_a_wrong_version_is_a_bad_request(connect, schema):
    c = synced(connect, schema)
    c.send_text('{"v":1,"t":3,"id":7}')
    assert error_name(schema, reply_to(c, 7)) == "badRequest"


def test_an_event_type_from_a_client_is_unknown(connect, schema):
    c = synced(connect, schema)
    msg_id = c.send(schema.type_numbers["shadeState"], {}, with_id=True)
    assert error_name(schema, reply_to(c, msg_id)) == "unknownOp"


def test_next_and_sync_with_an_id_are_answered(connect, schema):
    """A message with an id gets one reply, control messages too."""
    c = synced(connect, schema)
    msg_id = c.send(schema.type_numbers["next"], with_id=True)
    assert reply_to(c, msg_id)["d"].get(schema.keys["ok"]) == 1
    msg_id = c.send(schema.type_numbers["sync"], with_id=True)
    assert reply_to(c, msg_id)["d"].get(schema.keys["ok"]) == 1


def test_a_long_message_gets_one_reply(connect, schema):
    """A frame bigger than a TCP segment reaches the device in pieces, which it
    can't put together yet: still one reply for the whole message.  The message
    is wrong either way, so a reply is due however it arrives."""
    c = synced(connect, schema)
    c.send_text('{"v":2,"t":%d,"id":5,"x":"%s"}' % (schema.type_numbers["shadeState"], "a" * 3000))
    replies = []
    try:
        while True:
            f = c.recv(timeout=3)
            if f["t"] == 2:
                replies.append(f)
    except TimeoutError:
        pass
    assert len(replies) == 1, replies
    # In pieces it can't be read, so its id isn't known; whole, it is an event.
    assert error_name(schema, replies[0]) in ("badRequest", "unknownOp")


def test_subscribing_to_a_feed_is_ok(connect, schema):
    c = synced(connect, schema)
    for feed in ("frames", "frequencyScan"):
        msg_id = c.send(schema.type_numbers["subscribe"], {schema.keys["feed"]: schema.feed_numbers[feed]}, with_id=True)
        reply = reply_to(c, msg_id)
        assert reply["d"].get(schema.keys["ok"]) == 1
        msg_id = c.send(schema.type_numbers["unsubscribe"], {schema.keys["feed"]: schema.feed_numbers[feed]}, with_id=True)
        assert reply_to(c, msg_id)["d"].get(schema.keys["ok"]) == 1


def test_an_unknown_feed_is_a_bad_request(connect, schema):
    c = synced(connect, schema)
    msg_id = c.send(schema.type_numbers["subscribe"], {schema.keys["feed"]: 99}, with_id=True)
    assert error_name(schema, reply_to(c, msg_id)) == "badRequest"


def test_memory_status_arrives_within_the_status_period(connect, schema):
    """The device sends its status every 30 s."""
    c = synced(connect, schema)
    f = c.recv_until(lambda f: f["t"] == schema.type_numbers["memory"], timeout=40)[-1]
    memory = schema.expand(f["d"])
    assert memory["freeHeap"] > 0
    assert memory["uptime"] > 0
