"""Options and fixtures for the e2e suite.  See README.md."""
import os
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from proto_client import ProtoClient, Schema, http_json, run_sync  # noqa: E402


def pytest_addoption(parser):
    parser.addoption("--device", required=True, help="the device's address, e.g. 192.168.1.120")
    parser.addoption("--password", default=None, help="the device's password, when it is Secured with one")
    parser.addoption("--username", default="", help="the username that goes with --password")
    parser.addoption("--pin", default=None, help="the device's PIN, when it is Secured with one")
    parser.addoption("--record", action="store_true", help="save what the device sends to test/fixtures/proto2")
    parser.addoption("--radio", action="store_true", help="also run the tests that send commands on air")
    parser.addoption("--shade", type=int, default=None, help="with --radio: the id of the shade the commands may move")
    parser.addoption("--edit", action="store_true", help="also run the edits that change the shade model, then undo them")
    parser.addoption("--reboot", action="store_true", help="also run the tests that reboot the device")
    parser.addoption("--firmware", default=None, help="also update the device to this firmware.bin, built for its chip")
    parser.addoption("--lockout", action="store_true", help="also run the test that locks this PC out of /login for 30 s")


@pytest.fixture(scope="session")
def host(request):
    return request.config.getoption("--device")


@pytest.fixture(scope="session")
def login_context(host):
    return http_json(host, "/loginContext")


@pytest.fixture(scope="session")
def secured(login_context):
    return login_context.get("type", 0) != 0


@pytest.fixture(scope="session")
def token(request, host, secured):
    """The token for this PC, or None when the device isn't Secured."""
    if not secured:
        return None
    password = request.config.getoption("--password")
    pin = request.config.getoption("--pin")
    if password is None and pin is None:
        pytest.skip("the device is Secured: pass --password or --pin")
    body = {"username": request.config.getoption("--username"), "password": password or "", "pin": pin or ""}
    return http_json(host, "/login", method="PUT", body=body)["apiKey"]


@pytest.fixture
def connect(host, token):
    """Opens protocol 2 clients and closes them after the test."""
    clients = []

    def open_client(**params):
        if token and "apikey" not in params:
            params["apikey"] = token
        c = ProtoClient(host, params=params or None)
        clients.append(c)
        return c

    yield open_client
    for c in clients:
        c.close()
    # The device frees a slot when it sees the disconnect; the next test may
    # need every slot.
    time.sleep(0.5)


@pytest.fixture(scope="session")
def schema(host, token):
    """The device's schema, from one full sync."""
    s = Schema()
    c = ProtoClient(host, params={"apikey": token} if token else None)
    try:
        assert c.status == 101, "the device has no /ws: is it running protocol 2 firmware?"
        run_sync(c, s)
    finally:
        c.close()
    assert s.complete() and s.hash
    return s


@pytest.fixture(scope="session")
def recording(request):
    return request.config.getoption("--record")


@pytest.fixture(scope="session")
def radio_shade(request):
    """The shade --radio tests may move, or a skip."""
    if not request.config.getoption("--radio"):
        pytest.skip("sends on air: pass --radio --shade <id>")
    shade = request.config.getoption("--shade")
    if shade is None:
        pytest.skip("--radio needs --shade <id>, a shade that may move")
    return shade


@pytest.fixture(scope="session")
def editing(request):
    """For tests that change the shade model and change it back, or a skip."""
    if not request.config.getoption("--edit"):
        pytest.skip("changes the shade model and back: pass --edit")
    return True
