#!/usr/bin/env python3
"""Exercise the wallet-disabled Qt Options dialog under Xvfb."""

import ctypes
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time


def check_executable(path):
    if not Path(path).is_file() or not os.access(path, os.X_OK):
        raise RuntimeError("Missing executable: " + path)


def find_windows():
    output = subprocess.check_output(["xwininfo", "-root", "-tree"], text=True)
    return [
        (int(match.group(1), 16), match.group(2))
        for line in output.splitlines()
        if (match := re.search(r'^\s*(0x[0-9a-f]+) "([^"]+)"', line))
    ]


def wait_window(title, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        matches = [ident for ident, name in find_windows() if title in name]
        if matches:
            return matches[-1]
        time.sleep(0.25)
    raise RuntimeError("Window did not appear: " + title)


def window_origin(ident):
    output = subprocess.check_output(["xwininfo", "-id", hex(ident)], text=True)
    x = re.search(r"Absolute upper-left X:\s*(-?\d+)", output)
    y = re.search(r"Absolute upper-left Y:\s*(-?\d+)", output)
    if not x or not y:
        raise RuntimeError("Cannot locate Qt window")
    return int(x.group(1)), int(y.group(1))


class XInput:
    def __init__(self):
        x11 = ctypes.CDLL("libX11.so.6")
        xtst = ctypes.CDLL("libXtst.so.6")
        display_type = ctypes.c_void_p
        window_type = ctypes.c_ulong
        x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        x11.XOpenDisplay.restype = display_type
        x11.XSetInputFocus.argtypes = [
            display_type, window_type, ctypes.c_int, window_type
        ]
        x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
        x11.XStringToKeysym.restype = window_type
        x11.XKeysymToKeycode.argtypes = [display_type, window_type]
        x11.XKeysymToKeycode.restype = ctypes.c_uint
        x11.XSync.argtypes = [display_type, ctypes.c_int]
        xtst.XTestFakeKeyEvent.argtypes = [
            display_type, ctypes.c_uint, ctypes.c_int, window_type
        ]
        xtst.XTestFakeMotionEvent.argtypes = [
            display_type, ctypes.c_int, ctypes.c_int, ctypes.c_int, window_type
        ]
        xtst.XTestFakeButtonEvent.argtypes = [
            display_type, ctypes.c_uint, ctypes.c_int, window_type
        ]
        self.display = x11.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError("X11 display is unavailable")
        self.x11 = x11
        self.xtst = xtst

    def focus(self, ident):
        self.x11.XSetInputFocus(self.display, ident, 2, 0)
        self.x11.XSync(self.display, 0)

    def key(self, name):
        alt = name.startswith("Alt+")
        name = name[4:] if alt else name
        alt_code = self.x11.XKeysymToKeycode(
            self.display, self.x11.XStringToKeysym(b"Alt_L")
        )
        code = self.x11.XKeysymToKeycode(
            self.display, self.x11.XStringToKeysym(name.encode())
        )
        if not code:
            raise RuntimeError("Unknown X11 key: " + name)
        if alt:
            self.xtst.XTestFakeKeyEvent(self.display, alt_code, 1, 0)
        self.xtst.XTestFakeKeyEvent(self.display, code, 1, 0)
        self.xtst.XTestFakeKeyEvent(self.display, code, 0, 0)
        if alt:
            self.xtst.XTestFakeKeyEvent(self.display, alt_code, 0, 0)
        self.x11.XSync(self.display, 0)
        time.sleep(0.3)

    def click(self, x, y):
        self.xtst.XTestFakeMotionEvent(self.display, -1, x, y, 0)
        self.xtst.XTestFakeButtonEvent(self.display, 1, 1, 0)
        self.xtst.XTestFakeButtonEvent(self.display, 1, 0, 0)
        self.x11.XSync(self.display, 0)
        time.sleep(0.3)


def main(qt_binary, cli_binary):
    check_executable(qt_binary)
    check_executable(cli_binary)
    if not shutil.which("xwininfo"):
        raise RuntimeError("xwininfo is required")
    if not os.environ.get("DISPLAY"):
        raise RuntimeError("Run this test under Xvfb")

    work = Path(tempfile.mkdtemp(prefix="raven-qt-no-wallet-options."))
    data = work / "data"
    config = work / "config"
    data.mkdir()
    config.mkdir()
    with socket.socket() as port_socket:
        port_socket.bind(("127.0.0.1", 0))
        port = port_socket.getsockname()[1]
    cli = [
        cli_binary, f"-datadir={data}", "-regtest",
        f"-rpcport={port}", "-rpcclienttimeout=3"
    ]
    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "xcb"
    env["XDG_CONFIG_HOME"] = str(config)
    qt_log = work / "raven-qt.log"
    print(f"Qt options smoke evidence: {work}", flush=True)

    with qt_log.open("w") as log:
        qt = subprocess.Popen(
            [
                qt_binary, f"-datadir={data}", "-regtest", "-disablewallet",
                "-server=1", "-listen=0", "-connect=0", "-dnsseed=0",
                f"-rpcport={port}", "-splash=0", "-lang=en",
            ],
            env=env, stdout=log, stderr=subprocess.STDOUT,
        )
        try:
            deadline = time.monotonic() + 120
            while time.monotonic() < deadline:
                if qt.poll() is not None:
                    raise RuntimeError(f"Qt exited before RPC readiness: {qt.returncode}")
                response = subprocess.run(
                    cli + ["getblockchaininfo"], capture_output=True, timeout=5
                )
                if response.returncode == 0:
                    break
                time.sleep(1)
            else:
                raise RuntimeError("Qt RPC did not become ready")

            main_window = wait_window("Raven Core - Node [regtest]", 30)
            xinput = XInput()
            xinput.focus(main_window)
            left, top = window_origin(main_window)
            xinput.click(left + 63, top + 12)
            xinput.key("Down")
            xinput.key("Return")

            options_window = wait_window("Options", 10)
            xinput.focus(options_window)
            for key in ("Alt+w", "Alt+i", "Tab", "space", "Alt+o"):
                xinput.key(key)
            time.sleep(2)
            if qt.poll() is not None:
                raise RuntimeError(f"Qt crashed after Options submit: {qt.returncode}")

            settings = list(config.rglob("*.conf"))
            if not any(
                "fToolbarIconsOnly=true" in file.read_text() for file in settings
            ):
                raise RuntimeError("Icons only option was not saved")
            stopped = subprocess.run(cli + ["stop"], capture_output=True, timeout=10)
            if stopped.returncode != 0:
                raise RuntimeError("Qt RPC stop failed")
            if qt.wait(timeout=30) != 0:
                raise RuntimeError(f"Qt shutdown failed: {qt.returncode}")
            print("Qt no-wallet Options interaction passed", flush=True)
        finally:
            if qt.poll() is None:
                subprocess.run(cli + ["stop"], capture_output=True, timeout=5)
                try:
                    qt.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    qt.kill()
                    qt.wait()


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: test-qt-no-wallet-options.py RAVEN_QT RAVEN_CLI", file=sys.stderr)
        sys.exit(2)
    try:
        main(sys.argv[1], sys.argv[2])
    except Exception as error:
        print(f"Qt no-wallet Options smoke failed: {error}", file=sys.stderr)
        sys.exit(1)
