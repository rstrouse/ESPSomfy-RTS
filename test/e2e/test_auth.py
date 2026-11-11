"""The socket under the device's security mode (docs/adr/0006-two-security-modes-eleven-http-routes.md).

The suite never changes the device's security itself, so it tests the mode it
finds: pass --password or --pin for a Secured device.
"""
import hashlib
import hmac
import json
import socket
import time
import urllib.error
import urllib.request

import pytest

from proto_client import ProtoClient


def test_none_lets_anyone_connect(host, secured):
    if secured:
        pytest.skip("the device is Secured")
    c = ProtoClient(host)
    assert c.status == 101
    c.close()


def test_secured_refuses_a_socket_without_a_token(host, secured):
    if not secured:
        pytest.skip("the device's security is None")
    c = ProtoClient(host)
    assert c.status == 401


def test_secured_refuses_a_wrong_token(host, secured):
    if not secured:
        pytest.skip("the device's security is None")
    c = ProtoClient(host, params={"apikey": "0" * 64})
    assert c.status == 401


PASSWORD = 2  # the security type /loginContext reports for a password


def test_secured_refuses_a_token_keyed_with_the_server_id(request, host, secured, login_context, token):
    """The token key used to be the public serverId, so anyone who saw a token could check PIN guesses against it.

    It works out the token the old way, for this PC's address as the route to
    the device picks it.  Through NAT or a VPN the device sees another address,
    and the test passes without testing anything.
    """
    if not secured:
        pytest.skip("the device's security is None")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect((host, 80))
        my_ip = s.getsockname()[0]
    if login_context["type"] == PASSWORD:
        payload = f"{request.config.getoption('--username')}:{request.config.getoption('--password')}:{my_ip}"
    else:
        payload = f"{request.config.getoption('--pin')}:{my_ip}"
    server_id_token = hmac.new(login_context["serverId"].encode(), payload.encode(), hashlib.sha256).hexdigest()
    # A bool, so a failure doesn't print the tokens.
    is_device_token = server_id_token == token
    assert not is_device_token, "the device's token is the one keyed with its serverId"
    c = ProtoClient(host, params={"apikey": server_id_token})
    assert c.status == 401


def test_secured_accepts_the_token(host, secured, token):
    if not secured:
        pytest.skip("the device's security is None")
    c = ProtoClient(host, params={"apikey": token})
    assert c.status == 101
    c.close()


def login_status(host, body):
    """/login's status and reply, refusals included."""
    req = urllib.request.Request(
        f"http://{host}/login",
        data=json.dumps(body).encode(),
        method="PUT",
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, json.loads(r.read()), r.headers
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}"), e.headers


def test_failed_logins_lock_this_pc_out(request, host, secured, token):
    """Five wrong logins refuse this IP for 30 s, even with the right ones (SECURITY_PLAN.md, PR C)."""
    if not request.config.getoption("--lockout"):
        pytest.skip("locks this PC out of /login for 30 s: pass --lockout")
    if not secured:
        pytest.skip("the device's security is None")
    password = request.config.getoption("--password")
    pin = request.config.getoption("--pin")
    right = {"username": request.config.getoption("--username"), "password": password or "", "pin": pin or ""}
    wrong = {**right, "password": (password or "") + "x", "pin": "0000" if pin != "0000" else "1111"}
    # The token fixture's login started this PC with a clean count.
    for _ in range(5):
        status, reply, _ = login_status(host, wrong)
        assert status == 401 and reply["success"] is False
    status, reply, headers = login_status(host, right)
    assert status == 429, "the right credentials got through a lockout"
    wait = int(headers["Retry-After"])
    assert 25 <= wait <= 30
    assert reply["retryAfter"] == wait
    time.sleep(wait + 1)
    # Over: the right credentials work, and reset the count.
    status, reply, _ = login_status(host, right)
    assert status == 200 and reply["apiKey"] == token
