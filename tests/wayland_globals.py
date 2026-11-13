#!/usr/bin/env python3
"""Print the globals a Wayland compositor advertises, one "interface version"
per line.

Speaks the wire protocol directly (wl_display.get_registry + sync), so the
session test needs no extra package to see which protocols are offered.
Uses $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY like every Wayland client.
"""
import os
import socket
import struct
import sys


def main():
    display = os.environ.get("WAYLAND_DISPLAY", "wayland-0")
    path = display if os.path.isabs(display) else os.path.join(
        os.environ.get("XDG_RUNTIME_DIR", ""), display)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(10)
    sock.connect(path)
    # wl_display (object 1): get_registry (opcode 1) -> object 2,
    # sync (opcode 0) -> callback object 3.
    sock.sendall(struct.pack("<IHHI", 1, 1, 12, 2) + struct.pack("<IHHI", 1, 0, 12, 3))
    data = b""
    while True:
        while len(data) >= 8:
            obj, opcode, size = struct.unpack_from("<IHH", data)
            if size < 8:
                raise SystemExit("malformed message from the compositor")
            if len(data) < size:
                break
            body, data = data[8:size], data[size:]
            if obj == 2 and opcode == 0:  # wl_registry.global
                length = struct.unpack_from("<I", body, 4)[0]
                interface = body[8:8 + length - 1].decode()
                version = struct.unpack_from("<I", body, 8 + ((length + 3) & ~3))[0]
                print(interface, version)
            elif obj == 3 and opcode == 0:  # wl_callback.done: the list is complete
                return 0
            elif obj == 1 and opcode == 0:  # wl_display.error
                raise SystemExit("the compositor reported a protocol error")
        chunk = sock.recv(65536)
        if not chunk:
            raise SystemExit("the compositor closed the connection")
        data += chunk


if __name__ == "__main__":
    sys.exit(main())
