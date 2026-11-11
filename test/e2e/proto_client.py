"""A socket protocol 2 client for the e2e suite (docs/PROTOCOL.md).

Only the standard library, so the suite needs nothing but pytest.  Frames are
read on a background thread into a queue; tests take them with recv().
"""
import base64
import json
import os
import queue
import socket
import struct
import threading
import time
import urllib.parse
import urllib.request

PROTO_VERSION = 2


class SocketClosed(Exception):
    pass


class Schema:
    """The device's schema: numbers and short keys to names, and defaults."""

    def __init__(self):
        self.hash = None
        self.types = {}      # number -> name
        self.type_numbers = {}
        self.fields = {}     # key -> (name, kind, default)
        self.keys = {}       # name -> key
        self.errors = {}     # number -> name
        self.error_numbers = {}
        self.feeds = {}
        self.feed_numbers = {}

    def add(self, d):
        for number, name in d.get("T", []):
            self.types[number] = name
            self.type_numbers[name] = number
        for key, name, kind, default in d.get("F", []):
            self.fields[key] = (name, kind, default)
            self.keys[name] = key
        for number, name in d.get("E", []):
            self.errors[number] = name
            self.error_numbers[name] = number
        for number, name in d.get("Q", []):
            self.feeds[number] = name
            self.feed_numbers[name] = number

    def complete(self):
        return bool(self.types and self.fields and self.errors)

    def expand(self, value):
        """Short keys to names, 1/0 to booleans; omitted fields stay omitted."""
        if isinstance(value, list):
            return [self.expand(v) for v in value]
        if not isinstance(value, dict):
            return value
        out = {}
        for key, v in value.items():
            name, kind, _ = self.fields.get(key, (key, None, None))
            if kind == "b" and isinstance(v, int):
                v = bool(v)
            out[name] = self.expand(v)
        return out

    def default(self, name):
        key = self.keys[name]
        _, kind, default = self.fields[key]
        if kind == "b":
            return bool(default)
        if kind == "s":
            return ""
        if kind == "l":
            return []
        return default


class ProtoClient:
    def __init__(self, host, port=80, path="/ws", params=None, timeout=10, headers=None):
        self.host = host
        query = ("?" + urllib.parse.urlencode(params)) if params else ""
        self.sock = socket.create_connection((host, port), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        extra = "".join(f"{k}: {v}\r\n" for k, v in (headers or {}).items())
        self.sock.sendall((
            f"GET {path}{query} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
            f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n{extra}\r\n").encode())
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self.sock.recv(1024)
            if not chunk:
                raise SocketClosed("closed during the handshake")
            head += chunk
        status_line, self._rest = head.split(b"\r\n\r\n", 1)
        self.status = int(status_line.split(b" ")[1])
        self.frames = queue.Queue()
        self.raw = []  # every text frame as received, for fixtures
        self.closed = threading.Event()
        if self.status == 101:
            threading.Thread(target=self._reader, daemon=True).start()
        else:
            self.sock.close()
            self.closed.set()
        self._next_id = 1

    def _recvn(self, n):
        while len(self._rest) < n:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise SocketClosed()
            self._rest += chunk
        out, self._rest = self._rest[:n], self._rest[n:]
        return out

    def _reader(self):
        self.sock.settimeout(None)
        try:
            while True:
                b1, b2 = self._recvn(2)
                n = b2 & 0x7F
                if n == 126:
                    n = struct.unpack(">H", self._recvn(2))[0]
                elif n == 127:
                    n = struct.unpack(">Q", self._recvn(8))[0]
                data = self._recvn(n)
                opcode = b1 & 0x0F
                if opcode == 1:
                    text = data.decode()
                    self.raw.append(text)
                    self.frames.put(json.loads(text))
                elif opcode == 8:
                    break
        except (OSError, SocketClosed, ValueError):
            pass
        self.closed.set()

    def send_text(self, text):
        """A client frame must be masked."""
        payload = text.encode()
        mask = os.urandom(4)
        header = bytes([0x81])
        n = len(payload)
        if n < 126:
            header += bytes([0x80 | n])
        elif n < 65536:
            header += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            header += bytes([0x80 | 127]) + struct.pack(">Q", n)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(header + mask + masked)

    def send(self, type_number, d=None, with_id=False):
        frame = {"v": PROTO_VERSION, "t": type_number}
        msg_id = None
        if with_id:
            msg_id = self._next_id
            self._next_id += 1
            frame["id"] = msg_id
        if d is not None:
            frame["d"] = d
        self.send_text(json.dumps(frame, separators=(",", ":")))
        return msg_id

    def recv(self, timeout=10):
        try:
            return self.frames.get(timeout=timeout)
        except queue.Empty:
            raise TimeoutError("no frame in %ss" % timeout)

    def recv_until(self, predicate, timeout=10):
        """Frames up to and including the first one predicate accepts."""
        end = time.time() + timeout
        seen = []
        while True:
            left = end - time.time()
            if left <= 0:
                raise TimeoutError("not seen in %ss; got types %s" % (timeout, [f.get("t") for f in seen]))
            f = self.recv(left)
            seen.append(f)
            if predicate(f):
                return seen

    def close(self):
        try:
            self.sock.sendall(bytes([0x88, 0x80]) + os.urandom(4))
        except OSError:
            pass
        self.sock.close()


def run_sync(client, schema=None, timeout=20, hello=True):
    """Takes hello (unless hello=False, for a resync) and a whole sync,
    sending next after each sync frame.

    Returns (hello, sync frames, events seen meanwhile).  Fills schema, and
    its hash, from the sync when it carries one.
    """
    first = client.recv(timeout) if hello else None
    if hello:
        assert first["t"] == 1, first
        if schema is not None:
            schema.hash = first["d"]["sh"]
    sync_frames, events = [], []
    end = time.time() + timeout
    while True:
        f = client.recv(max(0.1, end - time.time()))
        if f["t"] == 4:
            sync_frames.append(f)
            if schema is not None:
                schema.add(f["d"])
            client.send(3)
        elif f["t"] == 5:
            return first, sync_frames, events
        else:
            events.append(f)


def http_json(host, path, port=80, method="GET", body=None, headers=None, timeout=20):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(f"http://{host}:{port}{path}", data=data, method=method, headers=headers or {})
    if data is not None:
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read())


def heap(host, token=None):
    """free, min, max and uptime from the status in a sync."""
    c = ProtoClient(host, params={"apikey": token} if token else None)
    try:
        schema = Schema()
        _, frames, _ = run_sync(c, schema)
    finally:
        c.close()
        # The device frees the slot when it sees the disconnect; the caller
        # may need every slot.
        time.sleep(0.5)
    key = schema.keys["status"]
    status = schema.expand(next(f["d"][key] for f in frames if key in f["d"]))
    return {"free": status["freeHeap"], "min": status["minHeap"], "max": status["maxHeap"], "uptime": status["uptime"]}
