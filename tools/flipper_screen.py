#!/usr/bin/env python3
"""Capture the real Flipper Zero screen over the official RPC screen stream.

Opens the USB CLI, switches it to an RPC session (`start_rpc_session`), starts the screen
stream (PB.Main field 20) and writes received ScreenFrames (field 22) as PNG. Optional
button presses are sent as PB_Gui.SendInputEventRequest (field 23), so a navigation path
is captured as a sequence of genuine device frames, never drawn on the host.

  tools/flipper_screen.py capture out.png [--port P] [--scale 4]
  tools/flipper_screen.py walk out_dir ok,back,... [--port P]   frame before and after each key
  tools/flipper_screen.py cli "loader info" [--port P]          one CLI command, its output

Needs pyserial: the uFBT toolchain's python has it (just screenshot uses that one).
Protocol: github.com/flipperdevices/flipperzero-protobuf (flipper.proto, gui.proto).
"""

import argparse
import glob
import struct
import sys
import time
import zlib
from pathlib import Path

WIDTH, HEIGHT = 128, 64
KEYS = {"up": 0, "down": 1, "right": 2, "left": 3, "ok": 4, "back": 5}
PRESS, RELEASE, SHORT, LONG = 0, 1, 2, 3
# Flipper's LCD: dark pixels on the backlit orange panel.
FG, BG = (0x00, 0x00, 0x00), (0xFF, 0x82, 0x00)


def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def field(num, wire, payload):
    tag = varint((num << 3) | wire)
    if wire == 0:
        return tag + varint(payload)
    return tag + varint(len(payload)) + payload


def main_msg(command_id, content_field, content):
    body = field(1, 0, command_id) + field(content_field, 2, content)
    return varint(len(body)) + body


def parse_fields(buf):
    """Yield (field number, wire type, value) from one protobuf message."""
    i = 0
    while i < len(buf):
        key, i = read_varint(buf, i)
        num, wire = key >> 3, key & 7
        if wire == 0:
            val, i = read_varint(buf, i)
        elif wire == 2:
            ln, i = read_varint(buf, i)
            val, i = buf[i : i + ln], i + ln
        elif wire == 5:
            val, i = buf[i : i + 4], i + 4
        elif wire == 1:
            val, i = buf[i : i + 8], i + 8
        else:
            raise ValueError(f"unsupported wire type {wire}")
        yield num, wire, val


def read_varint(buf, i):
    shift = n = 0
    while True:
        if i >= len(buf):
            raise IndexError("truncated varint")
        b = buf[i]
        i += 1
        n |= (b & 0x7F) << shift
        if not b & 0x80:
            return n, i
        shift += 7


def frame_to_rows(data):
    """ScreenFrame.data is u8g2 page layout: byte x + (y // 8) * 128, bit y % 8."""
    if len(data) != WIDTH * HEIGHT // 8:
        raise ValueError(f"frame has {len(data)} bytes, expected {WIDTH * HEIGHT // 8}")
    return [
        [bool(data[x + (y // 8) * WIDTH] >> (y % 8) & 1) for x in range(WIDTH)]
        for y in range(HEIGHT)
    ]


def png(rows, scale):
    raw = bytearray()
    for row in rows:
        line = bytearray()
        for on in row:
            line += bytes(FG if on else BG) * scale
        for _ in range(scale):
            raw.append(0)
            raw += line

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    w, h = len(rows[0]) * scale, len(rows) * scale
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


class Rpc:
    def __init__(self, port):
        import serial  # deferred: only the device path needs pyserial

        self.s = serial.Serial(port, 230400, timeout=0.2)
        self.buf = b""
        self.cid = 0
        self._cli_until(b">: ", send=b"\x03\r")  # Ctrl-C ends any command left running
        self.s.reset_input_buffer()
        self.s.write(b"start_rpc_session\r")
        self._cli_until(b"\n")  # the echoed command line; RPC framing starts after it

    def _cli_until(self, token, send=None, timeout=5.0):
        if send:
            self.s.write(send)
        end, got = time.time() + timeout, b""
        while time.time() < end:
            got += self.s.read(self.s.in_waiting or 1)
            if token in got:
                return got
        raise TimeoutError(f"device did not answer with {token!r} within {timeout}s")

    def send(self, content_field, content=b""):
        self.cid += 1
        self.s.write(main_msg(self.cid, content_field, content))

    def next_message(self, timeout=5.0):
        end = time.time() + timeout
        while time.time() < end:
            try:
                ln, i = read_varint(self.buf, 0)
                if len(self.buf) - i >= ln:
                    msg, self.buf = self.buf[i : i + ln], self.buf[i + ln :]
                    return dict((n, v) for n, _w, v in parse_fields(msg))
            except IndexError:
                pass
            self.buf += self.s.read(self.s.in_waiting or 1)
        raise TimeoutError("no RPC message within timeout")

    def frame(self, timeout=5.0, settle=0.4):
        """Newest frame after `settle` seconds: the stream sends one per screen change."""
        end, last = time.time() + timeout, None
        settle_at = time.time() + settle
        while time.time() < end:
            try:
                msg = self.next_message(timeout=max(0.05, settle_at - time.time()))
            except TimeoutError:
                if last is not None:
                    return last
                continue
            if 22 in msg:
                data = dict((n, v) for n, _w, v in parse_fields(msg[22])).get(1, b"")
                last = data
        if last is None:
            raise TimeoutError("no screen frame received")
        return last

    def press(self, key, long=False):
        for kind in (PRESS, LONG if long else SHORT, RELEASE):
            self.send(23, field(1, 0, KEYS[key]) + field(2, 0, kind))

    def close(self):
        try:
            self.send(21)
            self.send(19)  # StopSession: back to the CLI
        finally:
            self.s.close()


def cli(port, command, timeout=10.0):
    """Run one CLI command and return what it printed before the next prompt."""
    import serial  # deferred: only the device path needs pyserial

    s = serial.Serial(port, 230400, timeout=0.2)
    try:
        end, got = time.time() + timeout, b""
        s.write(b"\x03\r")
        while b">: " not in got and time.time() < end:
            got += s.read(s.in_waiting or 1)
        s.reset_input_buffer()
        s.write(command.encode("ascii") + b"\r")
        got = b""
        while time.time() < end:
            got += s.read(s.in_waiting or 1)
            if got.count(b">: ") >= 1 and got.rstrip().endswith(b">:"):
                break
        else:
            raise TimeoutError(f"no prompt after {command!r} within {timeout}s")
        text = got.decode("ascii", "replace").replace("\r", "")
        lines = text.split("\n")
        return "\n".join(line for line in lines[1:] if line.strip() and line.strip() != ">:")
    finally:
        s.close()


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodemflip_*") + glob.glob("/dev/serial/by-id/*Flipper*"))
    if not ports:
        sys.exit("no Flipper serial port found (is it connected and out of DFU mode?)")
    return ports[0]


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capture")
    c.add_argument("out")
    w = sub.add_parser("walk")
    w.add_argument("out_dir")
    w.add_argument("keys")
    k = sub.add_parser("cli")
    k.add_argument("command")
    k.add_argument("--port")
    for p in (c, w):
        p.add_argument("--port")
        p.add_argument("--scale", type=int, default=4)
        p.add_argument("--settle", type=float, default=0.4,
                       help="seconds of quiet before a frame counts as the screen")
    args = ap.parse_args(argv)
    if args.cmd == "cli":
        print(cli(args.port or find_port(), args.command))
        return 0

    rpc = Rpc(args.port or find_port())
    try:
        rpc.send(20)
        if args.cmd == "capture":
            Path(args.out).write_bytes(png(frame_to_rows(rpc.frame(settle=args.settle)), args.scale))
            print(args.out)
            return 0
        out = Path(args.out_dir)
        out.mkdir(parents=True, exist_ok=True)
        steps = [k.strip() for k in args.keys.split(",") if k.strip()]
        for k in steps:
            if k.removesuffix(":long") not in KEYS:
                sys.exit(f"unknown key {k!r}; keys: {', '.join(KEYS)} (add :long to hold)")
        shot = out / "00-start.png"
        shot.write_bytes(png(frame_to_rows(rpc.frame(settle=args.settle)), args.scale))
        print(shot)
        for n, k in enumerate(steps, 1):
            rpc.press(k.removesuffix(":long"), long=k.endswith(":long"))
            shot = out / f"{n:02d}-{k.replace(':', '-')}.png"
            shot.write_bytes(png(frame_to_rows(rpc.frame(settle=args.settle)), args.scale))
            print(shot)
        return 0
    finally:
        rpc.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
