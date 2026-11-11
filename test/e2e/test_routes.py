"""The HTTP routes and the socket's Origin check under the device's security
mode (docs/adr/0006-two-security-modes-eleven-http-routes.md).

Like test_auth, it tests the mode it finds: pass --password or --pin for a
Secured device.
"""
import json
import time
import urllib.error
import urllib.request

import pytest

from proto_client import ProtoClient, run_sync
from test_settings import header, uptime

# Every route that needs the token under Secured, sent without a body or
# parameters, so a route that let one through would still change nothing.
TOKEN_ROUTES = [
    ("GET", "/backup"),
    ("GET", "/shadeCommand"),
    ("GET", "/groupCommand"),
    ("GET", "/tiltCommand"),
    ("PUT", "/shadeCommand"),
    ("PUT", "/groupCommand"),
    ("PUT", "/tiltCommand"),
]
PUBLIC_ROUTES = ["/", "/index.html", "/index.js", "/loginContext", "/upnp.xml"]
SHADE_FILES = ["/shades.cfg", "/shades.tmp"]


def http(host, path, method="GET", body=None, headers=None):
    """The status, headers and body of a request, refusals included."""
    req = urllib.request.Request(f"http://{host}{path}", data=body, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, r.headers, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.headers, e.read()


def auth(token):
    """The header that carries the token, if there is one."""
    return {"apikey": token} if token else {}


def upload(host, path, headers=None, file=b"\0" * 256, fields=None):
    """Posts a form as the web UI does: the file, then the fields.

    The file is by default a small one that isn't a backup or an image, and
    file=None leaves it out.
    """
    boundary = "e2e-upload"
    parts = []
    if file is not None:
        parts.append(
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"e2e.bin\"\r\n"
            "Content-Type: application/octet-stream\r\n\r\n".encode() + file)
    for name, value in (fields or {}).items():
        parts.append(f"--{boundary}\r\nContent-Disposition: form-data; name=\"{name}\"\r\n\r\n{value}".encode())
    body = b"\r\n".join(parts) + f"\r\n--{boundary}--\r\n".encode()
    return http(host, path, "POST", body, {"Content-Type": f"multipart/form-data; boundary={boundary}", **(headers or {})})


@pytest.mark.parametrize("method,path", TOKEN_ROUTES)
def test_secured_refuses_a_route_without_a_token(host, secured, method, path):
    if not secured:
        pytest.skip("the device's security is None")
    body = b"{}" if method == "PUT" else None
    status, _, _ = http(host, path, method, body, {"Content-Type": "application/json"} if body else None)
    assert status == 401


@pytest.mark.parametrize("method,path", TOKEN_ROUTES)
def test_secured_refuses_a_route_with_a_wrong_token(host, secured, method, path):
    if not secured:
        pytest.skip("the device's security is None")
    body = b"{}" if method == "PUT" else None
    headers = {"apikey": "0" * 64}
    if body:
        headers["Content-Type"] = "application/json"
    status, _, _ = http(host, path, method, body, headers)
    assert status == 401


@pytest.mark.parametrize("method,path", TOKEN_ROUTES + [
    ("POST", "/restore"), ("POST", "/updateFirmware"), ("POST", "/updateApplication")])
def test_secured_refuses_a_web_asset_name_below_a_route(host, secured, method, path):
    """/backup/a.js once reached /backup and passed the token check as a web asset.

    The uploads are sent without a body: one that got through would reboot
    the device.
    """
    if not secured:
        pytest.skip("the device's security is None")
    body = b"{}" if method == "PUT" else None
    status, _, _ = http(host, path + "/a.js", method, body, {"Content-Type": "application/json"} if body else None)
    assert status in (401, 404)


def test_secured_accepts_the_token_on_a_route(host, secured, token):
    if not secured:
        pytest.skip("the device's security is None")
    status, _, _ = http(host, "/backup", headers=auth(token))
    assert status == 200


@pytest.mark.parametrize("path", ["/restore", "/updateFirmware"])
def test_secured_refuses_an_upload_without_a_token(host, secured, path):
    """/updateApplication isn't sent: one that got through would write over the file system."""
    if not secured:
        pytest.skip("the device's security is None")
    status, _, _ = upload(host, path)
    assert status == 401


def restore_without_a_file(host, token):
    status, _, body = upload(host, "/restore", auth(token), file=None, fields={"e2e": "1"})
    return status, json.loads(body or b"{}").get("desc", "")


def test_secured_drops_the_chunks_of_an_upload_without_a_token(host, secured, token):
    """The token check runs once the body is in, so /restore's chunks check the token themselves.

    A chunk that got through would mark the upload complete.  /restore with
    the token and no file would then find the refused file and call it
    invalid, instead of saying no upload completed.  Neither restores anything.
    """
    if not secured:
        pytest.skip("the device's security is None")
    status, desc = restore_without_a_file(host, token)
    if status != 400 or "didn't complete" not in desc:
        pytest.skip("an earlier upload is still marked complete: reboot the device")
    status, _, _ = upload(host, "/restore")
    assert status == 401
    status, desc = restore_without_a_file(host, token)
    assert status == 400
    assert "didn't complete" in desc, desc


@pytest.mark.parametrize("path", PUBLIC_ROUTES)
def test_public_routes_need_no_token(host, path):
    status, _, _ = http(host, path)
    assert status == 200


@pytest.mark.parametrize("path", SHADE_FILES)
def test_the_shade_files_are_not_served(host, path):
    """Their rolling codes leave the device only through /backup."""
    status, _, _ = http(host, path)
    assert status in (401, 404)


def test_replies_carry_no_cors_headers(host):
    _, headers, _ = http(host, "/loginContext", headers={"Origin": "http://example.com"})
    assert headers.get("Access-Control-Allow-Origin") is None


def test_a_socket_from_another_site_is_refused(host, token):
    params = {"apikey": token} if token else None
    c = ProtoClient(host, params=params, headers={"Origin": "http://example.com"})
    assert c.status == 401


def test_a_socket_from_the_page_the_device_served_is_accepted(host, token):
    # ProtoClient's Host names the port, and so does an Origin that matches it.
    params = {"apikey": token} if token else None
    c = ProtoClient(host, params=params, headers={"Origin": f"http://{host}:80"})
    assert c.status == 101
    c.close()
    # The device frees the slot when it sees the disconnect.
    time.sleep(0.5)


def test_a_firmware_upload_without_an_ota_slot_is_refused(host, token, connect, schema):
    """Builds with one app slot (C6, debug) say so in the header, and refuse
    the upload without stopping the radio or rebooting."""
    _, frames, _ = run_sync(connect(schema=schema.hash))
    # Left out at its default, true, as is a field older firmware doesn't have.
    if header(schema, frames).get("hasOtaSlot", True):
        pytest.skip("the device has an OTA slot: an upload would start an update")
    uptime_before = uptime(schema, frames)
    status, _, body = upload(host, "/updateFirmware", auth(token))
    assert status == 400, body
    assert "no OTA slot" in json.loads(body)["desc"]
    # The old reply rebooted half a second later.
    time.sleep(2)
    _, frames, _ = run_sync(connect(schema=schema.hash))
    assert uptime(schema, frames) > uptime_before, "the device restarted"
