#!/usr/bin/env python3
"""Send JSON requests to a running DAW's control socket and print the replies.

The DAW only opens the socket when `assets/settings.json` (relative to its
working directory, e.g. `build/assets/settings.json`) has `"controlEnabled": true`.
The DAW reads no env vars for this. `DAW_CONTROL_SOCKET` only overrides which
socket this script connects to.

Usage:
    dawctl.py '{"op":"ping"}'
    dawctl.py '{"op":"query","query":"graph"}'
    echo '{"op":"list_actions"}' | dawctl.py
    dawctl.py --socket /tmp/other.sock '{"op":"ping"}'
"""
import argparse
import json
import os
import socket
import sys

DEFAULT_SOCKET = "/tmp/microtonal-daw.sock"


def resolve_socket(cli_socket, settings_path):
    if cli_socket:
        return cli_socket
    env = os.environ.get("DAW_CONTROL_SOCKET")
    if env:
        return env
    try:
        with open(settings_path) as f:
            return json.load(f).get("controlSocketPath") or DEFAULT_SOCKET
    except (OSError, ValueError):
        return DEFAULT_SOCKET


def send_lines(sock_path, lines, timeout=30.0):
    """Sends each line, then reads one reply line per request."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(timeout)
        s.connect(sock_path)
        buf = b""
        replies = []
        for line in lines:
            s.sendall(line.encode() + b"\n")
            while b"\n" not in buf:
                chunk = s.recv(65536)
                if not chunk:
                    raise ConnectionError("DAW closed the connection")
                buf += chunk
            reply, buf = buf.split(b"\n", 1)
            replies.append(reply.decode())
        return replies


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("request", nargs="?", help="a JSON request string (or read from stdin)")
    ap.add_argument("--socket", dest="sock", help="control socket path")
    ap.add_argument("--settings", default="build/assets/settings.json",
                    help="settings.json to read controlSocketPath from")
    ap.add_argument("--raw", action="store_true", help="print replies without pretty-printing")
    args = ap.parse_args()

    if args.request:
        lines = [args.request]
    else:
        lines = [ln.strip() for ln in sys.stdin if ln.strip()]
    if not lines:
        ap.error("no request given (pass one as an argument or on stdin)")

    sock_path = resolve_socket(args.sock, args.settings)
    try:
        replies = send_lines(sock_path, lines)
    except (FileNotFoundError, ConnectionRefusedError, FileNotFoundError) as e:
        sys.exit(f"could not connect to {sock_path}: {e}\n"
                 f"Is the DAW running with \"controlEnabled\": true in the settings.json "
                 f"under its working directory (e.g. build/assets/settings.json)?")
    except socket.timeout:
        sys.exit(f"timed out talking to {sock_path} (DAW busy or blocked?)")

    for reply in replies:
        if args.raw:
            print(reply)
        else:
            try:
                print(json.dumps(json.loads(reply), indent=2))
            except ValueError:
                print(reply)


if __name__ == "__main__":
    main()
